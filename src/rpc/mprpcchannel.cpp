#include "mprpcchannel.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <string>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include "hook.hpp"
#include "iomanager.hpp"
#include "mprpccontroller.h"
#include "rpcheader.pb.h"
#include "util.h"

// ==================== 辅助函数 ====================

uint64_t MprpcChannel::GetCurrentTimeMs() {
  auto now = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

// ==================== 构造和析构 ====================

MprpcChannel::MprpcChannel(string ip, short port, bool connectNow)
    : m_ip(ip),
      m_port(port),
      m_clientFd(-1),
      m_state(ConnectionState::HEALTHY),
      m_failure_count(0),
      m_last_active_time(GetCurrentTimeMs()) {
  // 如果需要立即连接
  if (connectNow) {
    std::string errMsg;
    auto rt = newConnect(ip.c_str(), port, &errMsg);
    int tryCount = 3;
    while (!rt && tryCount--) {
      std::cout << errMsg << std::endl;
      rt = newConnect(ip.c_str(), port, &errMsg);
    }
    
    if (rt) {
      // 连接成功，调度心跳
      ScheduleHeartbeat();
    } else {
      // 连接失败，标记为断开
      m_state.store(ConnectionState::DISCONNECTED);
    }
  }
}

MprpcChannel::~MprpcChannel() {
  // 取消心跳定时器
  CancelHeartbeat();
  
  // 关闭连接
  if (m_clientFd != -1) {
    close(m_clientFd);
    m_clientFd = -1;
  }
}

// ==================== 连接管理 ====================

bool MprpcChannel::newConnect(const char* ip, uint16_t port, string* errMsg) {
  int clientfd = socket(AF_INET, SOCK_STREAM, 0);
  if (-1 == clientfd) {
    char errtxt[512] = {0};
    sprintf(errtxt, "create socket error! errno:%d", errno);
    m_clientFd = -1;
    *errMsg = errtxt;
    return false;
  }

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  server_addr.sin_addr.s_addr = inet_addr(ip);
  
  // 连接rpc服务节点
  if (-1 == connect(clientfd, (struct sockaddr*)&server_addr, sizeof(server_addr))) {
    close(clientfd);
    char errtxt[512] = {0};
    sprintf(errtxt, "connect fail! errno:%d", errno);
    m_clientFd = -1;
    *errMsg = errtxt;
    return false;
  }

  // 设置接收和发送超时，配合 monsoon Hook 机制实现协程级别的超时控制
  struct timeval timeout;
  timeout.tv_sec = 5;   // 5秒超时
  timeout.tv_usec = 0;
  
  if (-1 == setsockopt(clientfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout))) {
    char errtxt[512] = {0};
    sprintf(errtxt, "setsockopt SO_RCVTIMEO error! errno:%d", errno);
    DPrintf("[WARNING-MprpcChannel::newConnect] %s", errtxt);
  }
  
  if (-1 == setsockopt(clientfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout))) {
    char errtxt[512] = {0};
    sprintf(errtxt, "setsockopt SO_SNDTIMEO error! errno:%d", errno);
    DPrintf("[WARNING-MprpcChannel::newConnect] %s", errtxt);
  }

  m_clientFd = clientfd;
  
  // 重置状态
  m_state.store(ConnectionState::HEALTHY);
  m_failure_count.store(0);
  UpdateLastActiveTime();
  
  return true;
}

// ==================== 状态管理 ====================

void MprpcChannel::HandleSuccess() {
  // 重置失败计数
  m_failure_count.store(0);
  
  // 确保状态为 HEALTHY
  ConnectionState expected = ConnectionState::PROBING;
  m_state.compare_exchange_strong(expected, ConnectionState::HEALTHY);
  
  // 更新活跃时间
  UpdateLastActiveTime();
  
  // 重新调度心跳
  ScheduleHeartbeat();
}

void MprpcChannel::HandleFailure() {
  ConnectionState current_state = m_state.load();
  int failure_count = m_failure_count.fetch_add(1) + 1;
  
  DPrintf("[MprpcChannel::HandleFailure] Connection to %s:%d failed (count: %d, state: %d)",
          m_ip.c_str(), m_port, failure_count, static_cast<int>(current_state));
  
  if (current_state == ConnectionState::HEALTHY) {
    // 从 HEALTHY 切换到 PROBING
    m_state.store(ConnectionState::PROBING);
    DPrintf("[MprpcChannel] Connection to %s:%d entering PROBING state", m_ip.c_str(), m_port);
    
    // 调度更短间隔的探测
    ScheduleHeartbeat();
    
  } else if (current_state == ConnectionState::PROBING) {
    // 已经在 PROBING 状态，检查是否达到失败阈值
    if (failure_count >= MAX_FAILURE_COUNT) {
      // 切换到 DISCONNECTED
      m_state.store(ConnectionState::DISCONNECTED);
      DPrintf("[MprpcChannel] Connection to %s:%d marked as DISCONNECTED", m_ip.c_str(), m_port);
      
      // 取消心跳
      CancelHeartbeat();
      
      // 关闭连接
      if (m_clientFd != -1) {
        close(m_clientFd);
        m_clientFd = -1;
      }
    } else {
      // 继续探测
      ScheduleHeartbeat();
    }
  }
}

void MprpcChannel::UpdateLastActiveTime() {
  m_last_active_time.store(GetCurrentTimeMs());
}

// ==================== 心跳机制 ====================

void MprpcChannel::ScheduleHeartbeat() {
  // 检查是否在 IOManager 环境中
  auto iom = monsoon::IOManager::GetThis();
  if (!iom) {
    DPrintf("[MprpcChannel::ScheduleHeartbeat] Not in IOManager context, skipping heartbeat");
    return;
  }
  
  // 取消旧的心跳定时器
  CancelHeartbeat();
  
  // 根据当前状态选择间隔
  uint64_t interval_ms = (m_state.load() == ConnectionState::PROBING) ? 
                         PROBE_INTERVAL_MS : HEARTBEAT_INTERVAL_MS;
  
  // 使用 weak_ptr 避免循环引用
  std::weak_ptr<MprpcChannel> weak_self = shared_from_this();
  
  std::lock_guard<std::mutex> lock(m_timer_mutex);
  m_heartbeat_timer = iom->addTimer(interval_ms, [weak_self]() {
    auto self = weak_self.lock();
    if (self) {
      self->CheckIdleConnection();
    }
  });
  
  DPrintf("[MprpcChannel::ScheduleHeartbeat] Scheduled for %s:%d (interval: %lu ms)",
          m_ip.c_str(), m_port, interval_ms);
}

void MprpcChannel::CancelHeartbeat() {
  std::lock_guard<std::mutex> lock(m_timer_mutex);
  if (m_heartbeat_timer) {
    m_heartbeat_timer->cancel();
    m_heartbeat_timer.reset();
  }
}

void MprpcChannel::CheckIdleConnection() {
  DPrintf("[MprpcChannel::CheckIdleConnection] Checking connection to %s:%d (state: %d)",
          m_ip.c_str(), m_port, static_cast<int>(m_state.load()));
  
  // 发送心跳
  bool success = SendHeartbeat();
  
  if (success) {
    HandleSuccess();
  } else {
    HandleFailure();
  }
}

bool MprpcChannel::SendHeartbeat() {
  // 简单的心跳：发送一个很小的数据包并期望回应
  // 这里使用一个特殊的 0 字节发送来检测连接
  // 如果连接断开，send 会返回错误
  
  if (m_clientFd == -1) {
    return false;
  }
  
  // 发送一个空数据包（仅用于探测）
  char dummy = 0;
  ssize_t ret = send(m_clientFd, &dummy, 0, MSG_NOSIGNAL);
  
  if (ret == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
    DPrintf("[MprpcChannel::SendHeartbeat] Heartbeat failed to %s:%d, errno: %d",
            m_ip.c_str(), m_port, errno);
    return false;
  }
  
  DPrintf("[MprpcChannel::SendHeartbeat] Heartbeat successful to %s:%d", m_ip.c_str(), m_port);
  return true;
}

// ==================== RPC 调用（使用动态缓冲区）====================

void MprpcChannel::CallMethod(const google::protobuf::MethodDescriptor* method,
                              google::protobuf::RpcController* controller,
                              const google::protobuf::Message* request,
                              google::protobuf::Message* response,
                              google::protobuf::Closure* done) {
  // 确保在协程环境中启用了 Hook
  if (!monsoon::is_hook_enable()) {
    DPrintf(
        "[WARNING-MprpcChannel::CallMethod] Hook is not enabled! "
        "RPC calls should run in a coroutine with hook enabled for better performance.");
  }
  
  // 检查连接状态
  if (m_state.load() == ConnectionState::DISCONNECTED) {
    controller->SetFailed("Connection is DISCONNECTED");
    return;
  }
  
  // 如果未连接，尝试连接
  if (m_clientFd == -1) {
    std::string errMsg;
    bool rt = newConnect(m_ip.c_str(), m_port, &errMsg);
    if (!rt) {
      DPrintf("[MprpcChannel::CallMethod] Failed to connect to %s:%d - %s",
              m_ip.c_str(), m_port, errMsg.c_str());
      controller->SetFailed(errMsg);
      HandleFailure();
      return;
    }
    DPrintf("[MprpcChannel::CallMethod] Connected to %s:%d", m_ip.c_str(), m_port);
  }
  
  // ========== 构造请求 ==========
  const google::protobuf::ServiceDescriptor* sd = method->service();
  std::string service_name = sd->name();
  std::string method_name = method->name();
  
  // 序列化请求参数
  uint32_t args_size{};
  std::string args_str;
  if (!request->SerializeToString(&args_str)) {
    controller->SetFailed("serialize request error!");
    return;
  }
  args_size = args_str.size();
  
  // 构造 RPC 头部
  RPC::RpcHeader rpcHeader;
  rpcHeader.set_service_name(service_name);
  rpcHeader.set_method_name(method_name);
  rpcHeader.set_args_size(args_size);
  
  std::string rpc_header_str;
  if (!rpcHeader.SerializeToString(&rpc_header_str)) {
    controller->SetFailed("serialize rpc header error!");
    return;
  }
  
  // 使用 protobuf 的 CodedOutputStream 构建发送数据
  std::string send_rpc_str;
  {
    google::protobuf::io::StringOutputStream string_output(&send_rpc_str);
    google::protobuf::io::CodedOutputStream coded_output(&string_output);
    
    // 写入头部长度（变长编码）
    coded_output.WriteVarint32(static_cast<uint32_t>(rpc_header_str.size()));
    // 写入头部内容
    coded_output.WriteString(rpc_header_str);
  }
  // 附加请求参数
  send_rpc_str += args_str;
  
  // ========== 发送请求 ==========
  ssize_t sent = 0;
  size_t total_to_send = send_rpc_str.size();
  const char* data_ptr = send_rpc_str.c_str();
  
  while (sent < total_to_send) {
    ssize_t ret = send(m_clientFd, data_ptr + sent, total_to_send - sent, 0);
    if (ret == -1) {
      if (errno == EINTR) {
        continue;  // 被信号中断，重试
      }
      
      // 发送失败
      char errtxt[512] = {0};
      sprintf(errtxt, "send error! errno:%d", errno);
      DPrintf("[MprpcChannel::CallMethod] %s", errtxt);
      
      close(m_clientFd);
      m_clientFd = -1;
      controller->SetFailed(errtxt);
      HandleFailure();
      return;
    }
    sent += ret;
  }
  
  // ========== 接收响应（动态缓冲区）==========
  
  // 第一步：读取头部长度（变长编码）
  uint32_t header_size = 0;
  {
    // 为了读取变长编码，我们需要逐字节读取直到完整
    // protobuf 的变长编码最多 5 个字节（对于 uint32）
    uint8_t varint_buf[10] = {0};
    int varint_bytes = 0;
    
    for (int i = 0; i < 10; ++i) {
      ssize_t ret = recv(m_clientFd, &varint_buf[i], 1, 0);
      if (ret <= 0) {
        char errtxt[512] = {0};
        sprintf(errtxt, "recv header size error! errno:%d", errno);
        close(m_clientFd);
        m_clientFd = -1;
        controller->SetFailed(errtxt);
        HandleFailure();
        return;
      }
      varint_bytes++;
      
      // 检查是否是最后一个字节（最高位为 0）
      if ((varint_buf[i] & 0x80) == 0) {
        break;
      }
    }
    
    // 解码变长整数
    google::protobuf::io::ArrayInputStream array_input(varint_buf, varint_bytes);
    google::protobuf::io::CodedInputStream coded_input(&array_input);
    if (!coded_input.ReadVarint32(&header_size)) {
      controller->SetFailed("parse header size error!");
      HandleFailure();
      return;
    }
  }
  
  // 第二步：读取头部内容
  std::vector<char> header_buf(header_size);
  {
    size_t received = 0;
    while (received < header_size) {
      ssize_t ret = recv(m_clientFd, header_buf.data() + received, header_size - received, 0);
      if (ret <= 0) {
        char errtxt[512] = {0};
        sprintf(errtxt, "recv header content error! errno:%d", errno);
        close(m_clientFd);
        m_clientFd = -1;
        controller->SetFailed(errtxt);
        HandleFailure();
        return;
      }
      received += ret;
    }
  }
  
  // 第三步：反序列化头部
  RPC::RpcHeader resp_header;
  if (!resp_header.ParseFromArray(header_buf.data(), header_size)) {
    controller->SetFailed("parse response header error!");
    HandleFailure();
    return;
  }
  
  // 第四步：读取业务数据
  uint32_t response_args_size = resp_header.args_size();
  std::vector<char> response_buf(response_args_size);
  {
    size_t received = 0;
    while (received < response_args_size) {
      ssize_t ret = recv(m_clientFd, response_buf.data() + received, response_args_size - received, 0);
      if (ret <= 0) {
        char errtxt[512] = {0};
        sprintf(errtxt, "recv response data error! errno:%d", errno);
        close(m_clientFd);
        m_clientFd = -1;
        controller->SetFailed(errtxt);
        HandleFailure();
        return;
      }
      received += ret;
    }
  }
  
  // 第五步：反序列化业务数据
  if (!response->ParseFromArray(response_buf.data(), response_args_size)) {
    char errtxt[1050] = {0};
    sprintf(errtxt, "parse response data error!");
    controller->SetFailed(errtxt);
    HandleFailure();
    return;
  }
  
  // ========== 调用成功 ==========
  HandleSuccess();
}
