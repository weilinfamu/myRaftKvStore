#ifndef MPRPCCHANNEL_H
#define MPRPCCHANNEL_H

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <google/protobuf/service.h>
#include <algorithm>
#include <algorithm>  // 包含 std::generate_n() 和 std::generate() 函数的头文件
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <random>  // 包含 std::uniform_int_distribution 类型的头文件
#include <string>
#include <unordered_map>
#include <vector>
#include "hook.hpp"  // monsoon 协程库的 Hook 支持
#include "timer.hpp"  // IOManager 定时器支持
using namespace std;

// 连接状态枚举
enum class ConnectionState {
  HEALTHY,      // 健康状态，连接正常工作
  PROBING,      // 探测状态，连接可能有问题，正在检测
  DISCONNECTED  // 断开状态，连接已失效
};

// 真正负责发送和接受的前后处理工作
//  如消息的组织方式，向哪个节点发送等等
class MprpcChannel : public google::protobuf::RpcChannel, public std::enable_shared_from_this<MprpcChannel> {
 public:
  // 所有通过stub代理对象调用的rpc方法，都走到这里了，统一做rpc方法调用的数据数据序列化和网络发送 那一步
  void CallMethod(const google::protobuf::MethodDescriptor *method, google::protobuf::RpcController *controller,
                  const google::protobuf::Message *request, google::protobuf::Message *response,
                  google::protobuf::Closure *done) override;
  MprpcChannel(string ip, short port, bool connectNow);
  ~MprpcChannel();

  /**
   * @brief 检查连接是否健康
   * @return true 如果连接状态为 HEALTHY
   */
  bool IsHealthy() const { return m_state.load() == ConnectionState::HEALTHY; }

  /**
   * @brief 检查连接是否已断开
   * @return true 如果连接状态为 DISCONNECTED
   */
  bool IsDisconnected() const { return m_state.load() == ConnectionState::DISCONNECTED; }

  /**
   * @brief 获取当前连接状态
   * @return ConnectionState 当前状态
   */
  ConnectionState GetState() const { return m_state.load(); }

  /**
   * @brief 获取目标地址信息
   */
  const std::string& GetIp() const { return m_ip; }
  uint16_t GetPort() const { return m_port; }

 private:
  int m_clientFd;
  const std::string m_ip;  // 保存ip和端口，如果断了可以尝试重连
  const uint16_t m_port;

  // 连接状态管理
  std::atomic<ConnectionState> m_state;  // 连接状态
  std::atomic<int> m_failure_count;      // 连续失败计数
  std::atomic<uint64_t> m_last_active_time;  // 最后活跃时间（毫秒时间戳）

  // 心跳定时器相关
  std::shared_ptr<monsoon::Timer> m_heartbeat_timer;  // 心跳定时器
  std::mutex m_timer_mutex;  // 保护定时器的互斥锁

  // 配置参数
  static constexpr int MAX_FAILURE_COUNT = 3;      // 最大失败次数
  static constexpr uint64_t HEARTBEAT_INTERVAL_MS = 10000;  // 心跳间隔 10 秒
  static constexpr uint64_t PROBE_INTERVAL_MS = 5000;       // 探测间隔 5 秒

  /// @brief 连接ip和端口,并设置m_clientFd
  /// @param ip ip地址，本机字节序
  /// @param port 端口，本机字节序
  /// @param errMsg 错误信息输出
  /// @return 成功返回true，否则返回false
  bool newConnect(const char *ip, uint16_t port, string *errMsg);

  /**
   * @brief 调度心跳检查任务
   * 
   * 在连接空闲超过一定时间后执行心跳检查
   * 每次成功的 RPC 调用后都会重置心跳定时器
   */
  void ScheduleHeartbeat();

  /**
   * @brief 取消心跳检查任务
   */
  void CancelHeartbeat();

  /**
   * @brief 检查空闲连接的健康状态
   * 
   * 发送心跳包检测连接是否仍然可用
   * 根据检测结果更新连接状态
   */
  void CheckIdleConnection();

  /**
   * @brief 处理 RPC 调用失败
   * 
   * 根据当前状态和失败次数，决定是否进入 PROBING 或 DISCONNECTED 状态
   */
  void HandleFailure();

  /**
   * @brief 处理 RPC 调用成功
   * 
   * 重置失败计数，确保状态为 HEALTHY，并重新调度心跳
   */
  void HandleSuccess();

  /**
   * @brief 发送心跳包
   * @return true 如果心跳成功
   */
  bool SendHeartbeat();

  /**
   * @brief 更新最后活跃时间
   */
  void UpdateLastActiveTime();

  /**
   * @brief 获取当前时间戳（毫秒）
   */
  static uint64_t GetCurrentTimeMs();
};

#endif  // MPRPCCHANNEL_H