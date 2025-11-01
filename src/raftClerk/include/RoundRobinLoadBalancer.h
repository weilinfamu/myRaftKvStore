#ifndef ROUND_ROBIN_LOAD_BALANCER_H
#define ROUND_ROBIN_LOAD_BALANCER_H

#include <atomic>
#include "ILoadBalancer.h"

/**
 * @brief 轮询负载均衡器
 * 
 * 简单的轮询策略，依次选择下一个服务器。
 * 这是最简单的负载均衡策略，适合大多数场景。
 */
class RoundRobinLoadBalancer : public ILoadBalancer {
private:
    std::atomic<int> m_current;
    int m_serverCount;
    int m_recentLeader;  // 记录最近成功的leader
    
public:
    explicit RoundRobinLoadBalancer(int serverCount, int initialLeader = 0)
        : m_current(initialLeader),
          m_serverCount(serverCount),
          m_recentLeader(initialLeader) {}
    
    int SelectServer() override {
        // 优先尝试最近成功的leader
        return m_recentLeader;
    }
    
    void MarkSuccess(int serverIndex) override {
        // 更新最近成功的leader
        m_recentLeader = serverIndex;
    }
    
    void MarkFailure(int serverIndex) override {
        // 失败后轮询下一个
        m_recentLeader = (serverIndex + 1) % m_serverCount;
    }
    
    int GetServerCount() const override {
        return m_serverCount;
    }
};

#endif  // ROUND_ROBIN_LOAD_BALANCER_H

