/**
 * @file connection_pool_example.cpp
 * @brief 演示如何使用 ConnectionPool 和改进后的 MprpcChannel
 * 
 * 这个示例展示了：
 * 1. 如何使用 ConnectionPool 获取和归还连接
 * 2. 连接池如何自动管理连接的复用
 * 3. 健康检查和状态机如何工作
 * 4. 心跳机制如何保持连接活跃
 */

#include <iostream>
#include <memory>
#include <vector>
#include "connectionpool.h"
#include "iomanager.hpp"
#include "mprpcchannel.h"
#include "mprpccontroller.h"

// 注意：需要包含你的 protobuf 生成的头文件
// #include "your_service.pb.h"

/**
 * @brief 使用连接池执行单个 RPC 调用
 */
void SingleRpcWithPool(const std::string& ip, uint16_t port) {
    std::cout << "[Fiber-" << monsoon::Fiber::GetFiberId() 
              << "] Starting RPC call with connection pool" << std::endl;
    
    // 1. 从连接池获取连接
    auto& pool = ConnectionPool::GetInstance();
    auto channel = pool.GetConnection(ip, port);
    
    if (!channel) {
        std::cout << "[Fiber-" << monsoon::Fiber::GetFiberId() 
                  << "] Failed to get connection from pool" << std::endl;
        return;
    }
    
    std::cout << "[Fiber-" << monsoon::Fiber::GetFiberId() 
              << "] Got connection (state: " << static_cast<int>(channel->GetState()) << ")" << std::endl;
    
    // 2. 执行 RPC 调用
    /*
    YourService_Stub stub(channel.get());
    MprpcController controller;
    YourRequest request;
    YourResponse response;
    
    request.set_xxx("value");
    stub.YourMethod(&controller, &request, &response, nullptr);
    
    if (controller.Failed()) {
        std::cout << "[Fiber-" << monsoon::Fiber::GetFiberId() 
                  << "] RPC failed: " << controller.ErrorText() << std::endl;
    } else {
        std::cout << "[Fiber-" << monsoon::Fiber::GetFiberId() 
                  << "] RPC succeeded!" << std::endl;
    }
    */
    
    // 3. 归还连接到连接池
    // 只有健康的连接才会被复用，不健康的会被自动丢弃
    pool.ReturnConnection(channel, ip, port);
    
    std::cout << "[Fiber-" << monsoon::Fiber::GetFiberId() 
              << "] Connection returned to pool" << std::endl;
}

/**
 * @brief 演示连接复用
 * 
 * 连续执行多次 RPC 调用，观察连接池的复用行为
 */
void DemonstrateConnectionReuse(const std::string& ip, uint16_t port) {
    auto& pool = ConnectionPool::GetInstance();
    
    std::cout << "\n=== Demonstrating Connection Reuse ===" << std::endl;
    
    for (int i = 0; i < 3; ++i) {
        std::cout << "\n--- Round " << (i + 1) << " ---" << std::endl;
        
        // 获取连接
        auto channel = pool.GetConnection(ip, port);
        if (!channel) {
            std::cout << "Failed to get connection" << std::endl;
            continue;
        }
        
        std::cout << "Got connection (pool size before: " 
                  << pool.GetPoolSize(ip, port) << ")" << std::endl;
        
        // 模拟 RPC 调用
        // ...
        
        // 归还连接
        pool.ReturnConnection(channel, ip, port);
        
        std::cout << "Returned connection (pool size after: " 
                  << pool.GetPoolSize(ip, port) << ")" << std::endl;
    }
}

/**
 * @brief 演示并发 RPC 调用
 * 
 * 启动多个协程同时执行 RPC 调用，展示连接池的并发管理能力
 */
void DemonstrateConcurrentCalls(const std::string& ip, uint16_t port) {
    std::cout << "\n=== Demonstrating Concurrent Calls ===" << std::endl;
    
    monsoon::IOManager iom(4);
    
    // 启动 10 个并发 RPC 调用
    const int num_calls = 10;
    
    for (int i = 0; i < num_calls; ++i) {
        iom.scheduler([ip, port, i]() {
            std::cout << "[Fiber-" << monsoon::Fiber::GetFiberId() 
                      << "] Task #" << i << " starting" << std::endl;
            
            SingleRpcWithPool(ip, port);
            
            std::cout << "[Fiber-" << monsoon::Fiber::GetFiberId() 
                      << "] Task #" << i << " finished" << std::endl;
        });
    }
    
    std::cout << "All " << num_calls << " tasks scheduled" << std::endl;
}

/**
 * @brief 演示连接健康检查
 * 
 * 展示连接状态机如何工作：HEALTHY -> PROBING -> DISCONNECTED
 */
void DemonstrateHealthCheck(const std::string& ip, uint16_t port) {
    std::cout << "\n=== Demonstrating Health Check ===" << std::endl;
    
    auto& pool = ConnectionPool::GetInstance();
    
    // 获取一个连接
    auto channel = pool.GetConnection(ip, port);
    if (!channel) {
        std::cout << "Failed to get connection" << std::endl;
        return;
    }
    
    std::cout << "Initial state: " << static_cast<int>(channel->GetState()) << std::endl;
    
    // 连接会自动进行心跳检查
    // 如果 10 秒内没有活动，会触发心跳
    // 如果心跳失败，状态会变为 PROBING
    // 如果连续失败 3 次，状态会变为 DISCONNECTED
    
    std::cout << "Connection is healthy: " << channel->IsHealthy() << std::endl;
    std::cout << "Connection is disconnected: " << channel->IsDisconnected() << std::endl;
    
    // 归还连接
    pool.ReturnConnection(channel, ip, port);
}

/**
 * @brief 演示连接池统计信息
 */
void DemonstratePoolStats() {
    std::cout << "\n=== Connection Pool Statistics ===" << std::endl;
    
    auto& pool = ConnectionPool::GetInstance();
    std::cout << pool.GetStats() << std::endl;
}

/**
 * @brief 演示动态缓冲区处理
 * 
 * 新的实现可以正确处理任意大小的 RPC 响应
 */
void DemonstrateDynamicBuffer(const std::string& ip, uint16_t port) {
    std::cout << "\n=== Demonstrating Dynamic Buffer ===" << std::endl;
    
    auto& pool = ConnectionPool::GetInstance();
    auto channel = pool.GetConnection(ip, port);
    
    if (!channel) {
        std::cout << "Failed to get connection" << std::endl;
        return;
    }
    
    std::cout << "Old implementation had fixed 1024-byte buffer" << std::endl;
    std::cout << "New implementation uses dynamic buffer based on actual data size" << std::endl;
    std::cout << "Can handle responses of any size!" << std::endl;
    
    /*
    // 示例：处理大响应
    YourService_Stub stub(channel.get());
    MprpcController controller;
    LargeDataRequest request;
    LargeDataResponse response;  // 可能包含数 MB 的数据
    
    stub.GetLargeData(&controller, &request, &response, nullptr);
    
    if (!controller.Failed()) {
        std::cout << "Successfully received large response!" << std::endl;
        std::cout << "Response size: " << response.ByteSizeLong() << " bytes" << std::endl;
    }
    */
    
    pool.ReturnConnection(channel, ip, port);
}

/**
 * @brief 使用 ConnectionPool 的正确模式
 */
void BestPracticeExample(const std::string& ip, uint16_t port) {
    std::cout << "\n=== Best Practice Example ===" << std::endl;
    
    // 使用 RAII 模式管理连接
    class ConnectionGuard {
     public:
        ConnectionGuard(const std::string& ip, uint16_t port) 
            : ip_(ip), port_(port) {
            channel_ = ConnectionPool::GetInstance().GetConnection(ip, port);
        }
        
        ~ConnectionGuard() {
            if (channel_) {
                ConnectionPool::GetInstance().ReturnConnection(channel_, ip_, port_);
            }
        }
        
        std::shared_ptr<MprpcChannel> get() { return channel_; }
        
     private:
        std::string ip_;
        uint16_t port_;
        std::shared_ptr<MprpcChannel> channel_;
    };
    
    // 使用 RAII guard
    {
        ConnectionGuard guard(ip, port);
        auto channel = guard.get();
        
        if (!channel) {
            std::cout << "Failed to get connection" << std::endl;
            return;
        }
        
        // 执行 RPC 调用
        // ...
        
        std::cout << "RPC call completed" << std::endl;
        
        // guard 析构时自动归还连接
    }
    
    std::cout << "Connection automatically returned" << std::endl;
}

int main(int argc, char** argv) {
    std::cout << "=== ConnectionPool and Enhanced MprpcChannel Example ===" << std::endl;
    std::cout << std::endl;
    
    // 默认配置
    std::string server_ip = "127.0.0.1";
    uint16_t server_port = 8080;
    
    if (argc >= 3) {
        server_ip = argv[1];
        server_port = static_cast<uint16_t>(std::atoi(argv[2]));
    }
    
    std::cout << "Target RPC Server: " << server_ip << ":" << server_port << std::endl;
    std::cout << std::endl;
    
    // 创建 IOManager（自动启用 Hook）
    monsoon::IOManager iom(4, true, "ConnectionPool-IOManager");
    
    std::cout << "IOManager created with 4 worker threads" << std::endl;
    std::cout << std::endl;
    
    // 示例 1：基本使用
    std::cout << "--- Example 1: Basic Usage ---" << std::endl;
    iom.scheduler([server_ip, server_port]() {
        SingleRpcWithPool(server_ip, server_port);
    });
    
    // 示例 2：连接复用
    std::cout << "\n--- Example 2: Connection Reuse ---" << std::endl;
    iom.scheduler([server_ip, server_port]() {
        DemonstrateConnectionReuse(server_ip, server_port);
    });
    
    // 示例 3：并发调用
    std::cout << "\n--- Example 3: Concurrent Calls ---" << std::endl;
    DemonstrateConcurrentCalls(server_ip, server_port);
    
    // 示例 4：健康检查
    std::cout << "\n--- Example 4: Health Check ---" << std::endl;
    iom.scheduler([server_ip, server_port]() {
        DemonstrateHealthCheck(server_ip, server_port);
    });
    
    // 示例 5：动态缓冲区
    std::cout << "\n--- Example 5: Dynamic Buffer ---" << std::endl;
    iom.scheduler([server_ip, server_port]() {
        DemonstrateDynamicBuffer(server_ip, server_port);
    });
    
    // 示例 6：最佳实践
    std::cout << "\n--- Example 6: Best Practice ---" << std::endl;
    iom.scheduler([server_ip, server_port]() {
        BestPracticeExample(server_ip, server_port);
    });
    
    // 等待一段时间让任务完成
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // 显示统计信息
    DemonstratePoolStats();
    
    std::cout << std::endl;
    std::cout << "=== Example Completed ===" << std::endl;
    
    return 0;
}

/*
 * 编译命令（示例）：
 * 
 * g++ -std=c++17 -o connection_pool_example \
 *     connection_pool_example.cpp \
 *     -I../src/fiber/include \
 *     -I../src/rpc/include \
 *     -L../build/lib \
 *     -lmonsoon \
 *     -lmprpc \
 *     -lprotobuf \
 *     -lpthread
 * 
 * 运行：
 * ./connection_pool_example [server_ip] [server_port]
 * 
 * 例如：
 * ./connection_pool_example 192.168.1.100 8080
 */


