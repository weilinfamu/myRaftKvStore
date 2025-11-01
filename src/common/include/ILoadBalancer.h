#ifndef ILOAD_BALANCER_H
#define ILOAD_BALANCER_H

/**
 * @brief 负载均衡器接口
 * 
 * 支持多种负载均衡策略：
 * - 轮询（Round Robin）
 * - 随机
 * - 最小连接
 * - 一致性哈希
 * - 基于权重
 */
class ILoadBalancer {
public:
    virtual ~ILoadBalancer() = default;
    
    /**
     * @brief 选择一个服务器
     * @return 服务器索引
     */
    virtual int SelectServer() = 0;
    
    /**
     * @brief 标记某个服务器调用成功
     * @param serverIndex 服务器索引
     * 
     * 用于更新服务器健康状态，可以提高该服务器的权重
     */
    virtual void MarkSuccess(int serverIndex) = 0;
    
    /**
     * @brief 标记某个服务器调用失败
     * @param serverIndex 服务器索引
     * 
     * 用于更新服务器健康状态，可能触发熔断
     */
    virtual void MarkFailure(int serverIndex) = 0;
    
    /**
     * @brief 获取服务器总数
     * @return 服务器数量
     */
    virtual int GetServerCount() const = 0;
};

#endif  // ILOAD_BALANCER_H

