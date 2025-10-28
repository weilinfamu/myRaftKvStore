/**
 * @file rpc_coroutine_example.cpp
 * @brief 演示如何在 monsoon 协程环境中使用 MprpcChannel 进行 RPC 调用
 * 
 * 这个示例展示了：
 * 1. 如何创建 IOManager 实例（自动启用 Hook）
 * 2. 如何在协程中进行 RPC 调用
 * 3. Hook 如何自动将阻塞的 send/recv 转换为协程友好的异步操作
 * 4. 如何处理 RPC 超时和错误
 */

#include <iostream>
#include <memory>
#include "iomanager.hpp"
#include "hook.hpp"
#include "mprpcchannel.h"
#include "mprpccontroller.h"

// 注意：这里需要包含你的 protobuf 生成的头文件
// #include "your_service.pb.h"

/**
 * @brief 在协程中执行单个 RPC 调用的示例
 * 
 * 这个函数会被调度到 IOManager 的协程中执行
 * Hook 已经自动启用，所以 send/recv 会被转换为异步操作
 */
void single_rpc_call(const std::string& ip, uint16_t port) {
    std::cout << "[Fiber-" << monsoon::Fiber::GetFiberId() 
              << "] Starting RPC call to " << ip << ":" << port << std::endl;
    
    // 验证 Hook 已启用
    if (monsoon::is_hook_enable()) {
        std::cout << "[Fiber-" << monsoon::Fiber::GetFiberId() 
                  << "] Hook is enabled - I/O will be asynchronous!" << std::endl;
    }
    
    // 创建 RPC Channel（延迟连接）
    MprpcChannel channel(ip, port, false);
    
    // 示例：创建 stub 和调用 RPC
    // 在实际使用中，替换为你的 Service Stub
    /*
    YourService_Stub stub(&channel);
    MprpcController controller;
    YourRequest request;
    YourResponse response;
    
    // 设置请求参数
    request.set_xxx("value");
    
    // 调用 RPC 方法
    // 注意：这里的 send/recv 会被 Hook 自动转换为协程友好的异步操作
    // 当网络 I/O 阻塞时，当前协程会自动 yield，物理线程可以去执行其他协程
    stub.YourMethod(&controller, &request, &response, nullptr);
    
    if (controller.Failed()) {
        std::cout << "[Fiber-" << monsoon::Fiber::GetFiberId() 
                  << "] RPC call failed: " << controller.ErrorText() << std::endl;
    } else {
        std::cout << "[Fiber-" << monsoon::Fiber::GetFiberId() 
                  << "] RPC call succeeded!" << std::endl;
        // 处理响应
        std::cout << "Response: " << response.DebugString() << std::endl;
    }
    */
    
    std::cout << "[Fiber-" << monsoon::Fiber::GetFiberId() 
              << "] RPC call completed" << std::endl;
}

/**
 * @brief 在协程中执行多个并发 RPC 调用的示例
 * 
 * 展示如何在一个协程中发起多个 RPC 调用
 * 由于使用了协程，即使 RPC 调用看起来是"阻塞"的，
 * 实际上当等待网络 I/O 时，协程会自动让出 CPU
 */
void concurrent_rpc_calls(const std::string& ip, uint16_t port) {
    std::cout << "[Fiber-" << monsoon::Fiber::GetFiberId() 
              << "] Starting concurrent RPC calls" << std::endl;
    
    const int num_calls = 3;
    
    for (int i = 0; i < num_calls; ++i) {
        std::cout << "[Fiber-" << monsoon::Fiber::GetFiberId() 
                  << "] Making RPC call #" << i + 1 << std::endl;
        
        MprpcChannel channel(ip, port, false);
        
        // 在实际使用中，这里会调用真实的 RPC 方法
        // 每次调用时，如果需要等待网络 I/O，协程会自动 yield
        // 允许其他协程运行
        
        /*
        YourService_Stub stub(&channel);
        MprpcController controller;
        YourRequest request;
        YourResponse response;
        
        request.set_index(i);
        stub.YourMethod(&controller, &request, &response, nullptr);
        
        if (!controller.Failed()) {
            std::cout << "[Fiber-" << monsoon::Fiber::GetFiberId() 
                      << "] RPC call #" << i + 1 << " succeeded" << std::endl;
        }
        */
    }
    
    std::cout << "[Fiber-" << monsoon::Fiber::GetFiberId() 
              << "] All concurrent RPC calls completed" << std::endl;
}

/**
 * @brief 演示 RPC 超时处理
 * 
 * MprpcChannel 已配置了 5 秒超时
 * 如果 RPC 调用超过 5 秒，Hook 机制会自动取消等待
 */
void rpc_timeout_demo(const std::string& ip, uint16_t port) {
    std::cout << "[Fiber-" << monsoon::Fiber::GetFiberId() 
              << "] Testing RPC timeout (5 seconds)" << std::endl;
    
    MprpcChannel channel(ip, port, false);
    
    // 尝试连接到一个不存在或很慢的服务
    // 如果超时，会收到相应的错误
    
    /*
    YourService_Stub stub(&channel);
    MprpcController controller;
    YourRequest request;
    YourResponse response;
    
    auto start = std::chrono::steady_clock::now();
    stub.SlowMethod(&controller, &request, &response, nullptr);
    auto end = std::chrono::steady_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
    
    if (controller.Failed()) {
        std::cout << "[Fiber-" << monsoon::Fiber::GetFiberId() 
                  << "] RPC timed out after " << duration.count() 
                  << " seconds: " << controller.ErrorText() << std::endl;
    }
    */
}

int main(int argc, char** argv) {
    std::cout << "=== RPC Coroutine Integration Example ===" << std::endl;
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
    
    // 创建 IOManager
    // - 使用 2 个工作线程
    // - use_caller = true：让主线程也参与协程调度
    // IOManager 在启动时会自动为其调度线程启用 Hook
    monsoon::IOManager iom(2, true, "RPC-IOManager");
    
    std::cout << "IOManager created with 2 worker threads" << std::endl;
    std::cout << "Hook is automatically enabled for all scheduled fibers" << std::endl;
    std::cout << std::endl;
    
    // 示例 1：单个 RPC 调用
    std::cout << "--- Example 1: Single RPC Call ---" << std::endl;
    iom.scheduler([server_ip, server_port]() {
        single_rpc_call(server_ip, server_port);
    });
    
    // 示例 2：多个并发 RPC 调用
    std::cout << "--- Example 2: Concurrent RPC Calls ---" << std::endl;
    iom.scheduler([server_ip, server_port]() {
        concurrent_rpc_calls(server_ip, server_port);
    });
    
    // 示例 3：启动多个协程，每个协程执行一个 RPC 调用
    // 这些协程会在可用的物理线程上并发执行
    std::cout << "--- Example 3: Multiple Fibers Making RPC Calls ---" << std::endl;
    const int num_fibers = 5;
    for (int i = 0; i < num_fibers; ++i) {
        iom.scheduler([server_ip, server_port, i]() {
            std::cout << "[Fiber-" << monsoon::Fiber::GetFiberId() 
                      << "] Task #" << i + 1 << " started" << std::endl;
            single_rpc_call(server_ip, server_port);
            std::cout << "[Fiber-" << monsoon::Fiber::GetFiberId() 
                      << "] Task #" << i + 1 << " finished" << std::endl;
        });
    }
    
    // 示例 4：超时演示（如果需要）
    // std::cout << "--- Example 4: RPC Timeout Demo ---" << std::endl;
    // iom.scheduler([server_ip, server_port]() {
    //     rpc_timeout_demo(server_ip, server_port);
    // });
    
    std::cout << std::endl;
    std::cout << "All tasks scheduled. Waiting for completion..." << std::endl;
    std::cout << "Note: Physical threads are NOT blocked during RPC I/O waits!" << std::endl;
    std::cout << "      Fibers automatically yield when waiting for network data." << std::endl;
    std::cout << std::endl;
    
    // 主线程会在这里阻塞，直到所有任务完成
    // 但工作线程在等待网络 I/O 时不会阻塞，它们会去执行其他就绪的协程
    
    return 0;
}

/*
 * 编译命令（示例）：
 * 
 * g++ -std=c++17 -o rpc_coroutine_example \
 *     rpc_coroutine_example.cpp \
 *     -I../src/fiber/include \
 *     -I../src/rpc/include \
 *     -L../build/lib \
 *     -lmonsoon \
 *     -lmprpc \
 *     -lprotobuf \
 *     -lpthread
 * 
 * 运行：
 * ./rpc_coroutine_example [server_ip] [server_port]
 * 
 * 例如：
 * ./rpc_coroutine_example 192.168.1.100 8080
 */


