#include "connectionpool.h"
#include <sstream>
#include "util.h"

ConnectionPool& ConnectionPool::GetInstance() {
  static ConnectionPool instance;
  return instance;
}

std::string ConnectionPool::MakeKey(const std::string& ip, uint16_t port) const {
  std::ostringstream oss;
  oss << ip << ":" << port;
  return oss.str();
}

std::shared_ptr<MprpcChannel> ConnectionPool::GetConnection(const std::string& ip, uint16_t port) {
  std::string key = MakeKey(ip, port);
  std::shared_ptr<MprpcChannel> channel;

  {
    std::lock_guard<std::mutex> lock(mutex_);

    // 查找对应地址的连接池
    auto it = pools_.find(key);
    if (it != pools_.end() && !it->second.empty()) {
      // 从池中获取连接
      channel = it->second.front();
      it->second.pop();

      // 检查连接是否健康
      if (channel && channel->IsHealthy()) {
        DPrintf("[ConnectionPool] Reusing connection to %s (pool size: %zu)", key.c_str(), it->second.size());
        total_connections_reused_++;
        return channel;
      } else {
        // 连接不健康，丢弃
        DPrintf("[ConnectionPool] Discarding unhealthy connection to %s", key.c_str());
        total_connections_discarded_++;
        channel.reset();
      }
    }
  }

  // 池中没有可用连接，创建新连接
  DPrintf("[ConnectionPool] Creating new connection to %s", key.c_str());
  channel = std::make_shared<MprpcChannel>(ip, port, false);  // 延迟连接
  total_connections_created_++;

  return channel;
}

void ConnectionPool::ReturnConnection(std::shared_ptr<MprpcChannel> channel, const std::string& ip, uint16_t port) {
  if (!channel) {
    return;
  }

  std::string key = MakeKey(ip, port);

  // 只有健康的连接才放回池中
  if (!channel->IsHealthy()) {
    DPrintf("[ConnectionPool] Not returning unhealthy connection to %s", key.c_str());
    total_connections_discarded_++;
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  // 将连接放回池中
  pools_[key].push(channel);
  DPrintf("[ConnectionPool] Returned connection to %s (pool size: %zu)", key.c_str(), pools_[key].size());
}

void ConnectionPool::ClearPool(const std::string& ip, uint16_t port) {
  std::string key = MakeKey(ip, port);
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = pools_.find(key);
  if (it != pools_.end()) {
    size_t count = it->second.size();
    // 清空队列
    while (!it->second.empty()) {
      it->second.pop();
    }
    pools_.erase(it);
    DPrintf("[ConnectionPool] Cleared pool for %s (%zu connections removed)", key.c_str(), count);
  }
}

void ConnectionPool::ClearAllPools() {
  std::lock_guard<std::mutex> lock(mutex_);

  size_t total_count = 0;
  for (auto& pair : pools_) {
    total_count += pair.second.size();
  }

  pools_.clear();
  DPrintf("[ConnectionPool] Cleared all pools (%zu connections removed)", total_count);
}

size_t ConnectionPool::GetPoolSize(const std::string& ip, uint16_t port) {
  std::string key = MakeKey(ip, port);
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = pools_.find(key);
  if (it != pools_.end()) {
    return it->second.size();
  }
  return 0;
}

std::string ConnectionPool::GetStats() const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::ostringstream oss;
  oss << "=== ConnectionPool Statistics ===" << std::endl;
  oss << "Total connections created: " << total_connections_created_ << std::endl;
  oss << "Total connections reused: " << total_connections_reused_ << std::endl;
  oss << "Total connections discarded: " << total_connections_discarded_ << std::endl;
  oss << "Active pools: " << pools_.size() << std::endl;

  if (!pools_.empty()) {
    oss << "Pool details:" << std::endl;
    for (const auto& pair : pools_) {
      oss << "  " << pair.first << ": " << pair.second.size() << " connections" << std::endl;
    }
  }

  return oss.str();
}


