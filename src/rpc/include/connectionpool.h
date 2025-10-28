#ifndef CONNECTIONPOOL_H
#define CONNECTIONPOOL_H

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include "mprpcchannel.h"

/**
 * @brief 连接池类，管理到不同目标地址的 RPC 连接
 * 
 * 采用单例模式，全局唯一实例
 * 内部维护一个 map，key 为 "ip:port"，value 为该地址的连接队列
 * 支持连接的获取、归还、健康检查等功能
 */
class ConnectionPool {
 public:
  /**
   * @brief 获取连接池的单例实例
   * @return ConnectionPool& 连接池引用
   */
  static ConnectionPool& GetInstance();

  /**
   * @brief 获取到指定地址的连接
   * @param ip 目标 IP 地址
   * @param port 目标端口
   * @return std::shared_ptr<MprpcChannel> 连接对象，如果失败返回 nullptr
   * 
   * 如果池中有可用连接，则复用；否则创建新连接
   * 会自动检查连接健康状态，丢弃不健康的连接
   */
  std::shared_ptr<MprpcChannel> GetConnection(const std::string& ip, uint16_t port);

  /**
   * @brief 归还连接到连接池
   * @param channel 要归还的连接
   * @param ip 目标 IP 地址
   * @param port 目标端口
   * 
   * 只有健康的连接才会被放回池中复用
   * 不健康的连接会被直接丢弃
   */
  void ReturnConnection(std::shared_ptr<MprpcChannel> channel, const std::string& ip, uint16_t port);

  /**
   * @brief 清空指定地址的连接池
   * @param ip 目标 IP 地址
   * @param port 目标端口
   */
  void ClearPool(const std::string& ip, uint16_t port);

  /**
   * @brief 清空所有连接池
   */
  void ClearAllPools();

  /**
   * @brief 获取指定地址的连接池大小
   * @param ip 目标 IP 地址
   * @param port 目标端口
   * @return size_t 连接池中的连接数量
   */
  size_t GetPoolSize(const std::string& ip, uint16_t port);

  /**
   * @brief 获取连接池统计信息
   * @return std::string 统计信息字符串
   */
  std::string GetStats() const;

 private:
  // 私有构造函数，单例模式
  ConnectionPool() = default;
  ~ConnectionPool() = default;

  // 禁止拷贝和赋值
  ConnectionPool(const ConnectionPool&) = delete;
  ConnectionPool& operator=(const ConnectionPool&) = delete;

  /**
   * @brief 生成地址的唯一键
   * @param ip IP 地址
   * @param port 端口
   * @return std::string "ip:port" 格式的键
   */
  std::string MakeKey(const std::string& ip, uint16_t port) const;

  // 连接池数据结构：map<"ip:port", queue<连接>>
  std::map<std::string, std::queue<std::shared_ptr<MprpcChannel>>> pools_;

  // 保护连接池的互斥锁
  mutable std::mutex mutex_;

  // 统计信息
  std::atomic<uint64_t> total_connections_created_{0};   // 总共创建的连接数
  std::atomic<uint64_t> total_connections_reused_{0};    // 总共复用的连接数
  std::atomic<uint64_t> total_connections_discarded_{0}; // 总共丢弃的连接数
};

#endif  // CONNECTIONPOOL_H


