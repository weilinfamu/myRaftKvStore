//
// Created by swx on 23-6-4.
//
#include "clerk.h"

#include "raftServerRpcUtil.h"

#include "util.h"

#include <string>
#include <vector>
std::string Clerk::Get(std::string key) {
  m_requestId++;
  auto requestId = m_requestId;
  
  raftKVRpcProctoc::GetArgs args;
  args.set_key(key);
  args.set_clientid(m_clientId);
  args.set_requestid(requestId);

  while (true) {
    // ==================== 新架构：使用负载均衡器选择服务器 ====================
    int server = m_loadBalancer->SelectServer();
    
    raftKVRpcProctoc::GetReply reply;
    bool ok = m_rpcClients[server]->Get(args, &reply);
    
    if (!ok || reply.err() == ErrWrongLeader) {
      // 标记失败，负载均衡器会选择下一个
      m_loadBalancer->MarkFailure(server);
      continue;
    }
    
    if (reply.err() == ErrNoKey) {
      m_loadBalancer->MarkSuccess(server);
      return "";
    }
    
    if (reply.err() == OK) {
      m_loadBalancer->MarkSuccess(server);
      return reply.value();
    }
  }
  return "";
}

void Clerk::PutAppend(std::string key, std::string value, std::string op) {
  m_requestId++;
  auto requestId = m_requestId;
  
  while (true) {
    // ==================== 新架构：使用负载均衡器选择服务器 ====================
    int server = m_loadBalancer->SelectServer();
    
    raftKVRpcProctoc::PutAppendArgs args;
    args.set_key(key);
    args.set_value(value);
    args.set_op(op);
    args.set_clientid(m_clientId);
    args.set_requestid(requestId);
    
    raftKVRpcProctoc::PutAppendReply reply;
    bool ok = m_rpcClients[server]->PutAppend(args, &reply);
    
    if (!ok || reply.err() == ErrWrongLeader) {
      DPrintf("【Clerk::PutAppend】原以为的leader：{%d}请求失败，向新leader重试，操作：{%s}", server, op.c_str());
      if (!ok) {
        DPrintf("重试原因：rpc失败");
      }
      if (reply.err() == ErrWrongLeader) {
        DPrintf("重试原因：非leader");
      }
      m_loadBalancer->MarkFailure(server);
      continue;
    }
    
    if (reply.err() == OK) {
      m_loadBalancer->MarkSuccess(server);
      return;
    }
  }
}

void Clerk::Put(std::string key, std::string value) { PutAppend(key, value, "Put"); }

void Clerk::Append(std::string key, std::string value) { PutAppend(key, value, "Append"); }
//初始化客户端
void Clerk::Init(std::string configFileName) {
  //获取所有raft节点ip、port ，并进行连接
  MprpcConfig config;
  config.LoadConfigFile(configFileName.c_str());
  std::vector<std::pair<std::string, short>> ipPortVt;
  for (int i = 0; i < INT_MAX - 1; ++i) {
    std::string node = "node" + std::to_string(i);

    std::string nodeIp = config.Load(node + "ip");
    std::string nodePortStr = config.Load(node + "port");
    if (nodeIp.empty()) {
      break;
    }
    ipPortVt.emplace_back(nodeIp, atoi(nodePortStr.c_str()));  //沒有atos方法，可以考慮自己实现
  }
  //进行连接
  for (const auto& item : ipPortVt) {
    std::string ip = item.first;
    short port = item.second;
    // 2024-01-04 todo：bug fix
    auto* rpc = new raftServerRpcUtil(ip, port);
    m_servers.push_back(std::shared_ptr<raftServerRpcUtil>(rpc));
    
    // ==================== 新架构：创建RPC客户端接口 ====================
    m_rpcClients.push_back(
      std::make_unique<KvRpcClientAdapter>(std::shared_ptr<raftServerRpcUtil>(m_servers.back()))
    );
  }
  
  // ==================== 新架构：创建负载均衡器 ====================
  m_loadBalancer = std::make_unique<RoundRobinLoadBalancer>(m_servers.size(), 0);
}

Clerk::Clerk() : m_clientId(Uuid()), m_requestId(0), m_recentLeaderId(0) {}
