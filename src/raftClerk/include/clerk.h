//
// Created by swx on 23-6-4.
//

#ifndef SKIP_LIST_ON_RAFT_CLERK_H
#define SKIP_LIST_ON_RAFT_CLERK_H
#include <arpa/inet.h>
#include <netinet/in.h>
#include <raftServerRpcUtil.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <cerrno>
#include <memory>
#include <string>
#include <vector>
#include "kvServerRPC.pb.h"
#include "mprpcconfig.h"
#include "IKvRpcClient.h"  // 新增：RPC客户端接口
#include "ILoadBalancer.h"  // 新增：负载均衡接口
#include "KvRpcClientAdapter.h"  // 新增：适配器
#include "RoundRobinLoadBalancer.h"  // 新增：轮询策略
class Clerk {
 private:
  // ==================== 保留原有字段（兼容） ====================
  std::vector<std::shared_ptr<raftServerRpcUtil>>
      m_servers;  //保存所有raft节点的fd //todo：全部初始化为-1，表示没有连接上
  std::string m_clientId;
  int m_requestId;
  int m_recentLeaderId;  //只是有可能是领导
  
  // ==================== 新架构：使用接口 ====================
  std::vector<std::unique_ptr<IKvRpcClient>> m_rpcClients;  // RPC客户端接口列表
  std::unique_ptr<ILoadBalancer> m_loadBalancer;  // 负载均衡器

  std::string Uuid() {
    return std::to_string(rand()) + std::to_string(rand()) + std::to_string(rand()) + std::to_string(rand());
  }  //用于返回随机的clientId

  //    MakeClerk  todo
  void PutAppend(std::string key, std::string value, std::string op);

 public:
  //对外暴露的三个功能和初始化
  void Init(std::string configFileName);
  std::string Get(std::string key);

  void Put(std::string key, std::string value);
  void Append(std::string key, std::string value);

 public:
  Clerk();
};

#endif  // SKIP_LIST_ON_RAFT_CLERK_H
