// stress_test.cpp - KV存储压力测试程序（协程版本）
#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <atomic>
#include <memory>
#include "clerk.h"
#include "util.h"
#include "fiber/iomanager.hpp"  // monsoon 协程库

class StressTester {
private:
    int fiberCount;  // 协程数（原 threadCount）
    int operationsPerFiber;  // 每协程操作数（原 operationsPerThread）
    std::atomic<long> totalOperations{0};
    std::atomic<long> successfulOperations{0};
    std::string configFile_;
    
public:
    StressTester(int fibers, int ops, const std::string& configFile) 
        : fiberCount(fibers), operationsPerFiber(ops), configFile_(configFile) {}
    
    void RunPutTest() {
        std::cout << "开始PUT测试（协程模式）..." << std::endl;
        
        // 创建 IOManager（4个工作线程）
        monsoon::IOManager iom(4, true, "put_test_iom");
        
        auto start = std::chrono::high_resolution_clock::now();
        
        // 调度协程
        for (int i = 0; i < fiberCount; ++i) {
            iom.scheduler([this, i]() {
                // 每个协程创建独立的 Clerk
                auto clerk = std::make_shared<Clerk>();
                clerk->Init(configFile_);
                
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> dis(1000, 9999);
                
                for (int j = 0; j < operationsPerFiber; ++j) {
                    std::string key = "key_" + std::to_string(i) + "_" + std::to_string(j);
                    std::string value = "value_" + std::to_string(dis(gen));
                    clerk->Put(key, value);
                    totalOperations++;
                    successfulOperations++;
                }
            });
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        std::cout << "Put测试完成: " << totalOperations.load()
                  << " 次操作, 耗时: " << duration.count() << "ms" 
                  << ", 吞吐量: " << (totalOperations.load() * 1000.0 / duration.count()) << " ops/s" << std::endl;
    }
    
    void RunGetTest() {
        std::cout << "开始GET测试（协程模式）..." << std::endl;
        
        monsoon::IOManager iom(4, true, "get_test_iom");
        
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < fiberCount; ++i) {
            iom.scheduler([this, i]() {
                auto clerk = std::make_shared<Clerk>();
                clerk->Init(configFile_);
                
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> key_dis(0, operationsPerFiber - 1);
                
                for (int j = 0; j < operationsPerFiber; ++j) {
                    int key_index = key_dis(gen);
                    std::string key = "key_" + std::to_string(i) + "_" + std::to_string(key_index);
                    std::string result = clerk->Get(key);
                    totalOperations++;
                    if (!result.empty()) {
                        successfulOperations++;
                    }
                }
            });
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        std::cout << "Get测试完成: " << totalOperations.load()
                  << " 次操作, 耗时: " << duration.count() << "ms"
                  << ", 吞吐量: " << (totalOperations.load() * 1000.0 / duration.count()) << " ops/s" << std::endl;
    }
    
    void RunMixedTest() {
        std::cout << "开始混合测试（协程模式）..." << std::endl;
        
        monsoon::IOManager iom(4, true, "mixed_test_iom");
        
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < fiberCount; ++i) {
            iom.scheduler([this, i]() {
                auto clerk = std::make_shared<Clerk>();
                clerk->Init(configFile_);
                
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> dis(1000, 9999);
                std::uniform_real_distribution<> op_dis(0.0, 1.0);
                
                for (int j = 0; j < operationsPerFiber; ++j) {
                    std::string key = "key_" + std::to_string(i) + "_" + std::to_string(j);
                    
                    if (op_dis(gen) < 0.7) { // 70% PUT操作
                        std::string value = "value_" + std::to_string(dis(gen));
                        clerk->Put(key, value);
                    } else { // 30% GET操作
                        std::string result = clerk->Get(key);
                    }
                    totalOperations++;
                    successfulOperations++;
                }
            });
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        std::cout << "混合测试完成: " << totalOperations.load()
                  << " 次操作, 耗时: " << duration.count() << "ms"
                  << ", 吞吐量: " << (totalOperations.load() * 1000.0 / duration.count()) << " ops/s" << std::endl;
    }
    
    void RunLongRunningTest(int duration_seconds) {
        std::cout << "开始长时间运行测试（协程模式），持续 " << duration_seconds << " 秒..." << std::endl;
        
        monsoon::IOManager iom(4, true, "long_test_iom");
        
        auto start = std::chrono::high_resolution_clock::now();
        auto end_time = start + std::chrono::seconds(duration_seconds);
        
        for (int i = 0; i < fiberCount; ++i) {
            iom.scheduler([this, i, end_time]() {
                auto clerk = std::make_shared<Clerk>();
                clerk->Init(configFile_);
                
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> dis(1000, 9999);
                std::uniform_real_distribution<> op_dis(0.0, 1.0);
                
                long local_ops = 0;
                while (std::chrono::high_resolution_clock::now() < end_time) {
                    std::string key = "long_key_" + std::to_string(i) + "_" + std::to_string(local_ops);
                    
                    if (op_dis(gen) < 0.7) { // 70% PUT操作
                        std::string value = "long_value_" + std::to_string(dis(gen));
                        clerk->Put(key, value);
                    } else { // 30% GET操作
                        std::string result = clerk->Get(key);
                    }
                    totalOperations++;
                    successfulOperations++;
                    local_ops++;
                }
            });
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        std::cout << "长时间运行测试完成: " << totalOperations.load()
                  << " 次操作, 耗时: " << duration.count() << "ms"
                  << ", 吞吐量: " << (totalOperations.load() * 1000.0 / duration.count()) << " ops/s" << std::endl;
    }
};

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cout << "用法: " << argv[0] << " <config_file> <fibers> <operations_per_fiber> [long_duration_seconds]" << std::endl;
        std::cout << "示例:" << std::endl;
        std::cout << "  " << argv[0] << " test.conf 4 1000" << std::endl;
        std::cout << "  " << argv[0] << " test.conf 8 500 30" << std::endl;
        std::cout << std::endl;
        std::cout << "注意: 此版本使用 monsoon 协程库进行高效并发" << std::endl;
        return 1;
    }
    
    std::string configFile = argv[1];
    int fibers = std::stoi(argv[2]);
    int ops = std::stoi(argv[3]);
    int long_duration = 0;
    
    if (argc >= 5) {
        long_duration = std::stoi(argv[4]);
    }
    
    StressTester tester(fibers, ops, configFile);
    
    std::cout << "开始KV存储RAFT压力测试（协程版本）..." << std::endl;
    std::cout << "配置: " << fibers << " 协程, " << ops << " 操作/协程" << std::endl;
    std::cout << "==========================================" << std::endl;
    
    // 运行PUT测试
    std::cout << "1. PUT操作测试:" << std::endl;
    tester.RunPutTest();
    std::cout << std::endl;
    
    // 运行GET测试
    std::cout << "2. GET操作测试:" << std::endl;
    tester.RunGetTest();
    std::cout << std::endl;
    
    // 运行混合测试
    std::cout << "3. 混合操作测试 (70% PUT, 30% GET):" << std::endl;
    tester.RunMixedTest();
    std::cout << std::endl;
    
    // 如果指定了长时间运行，执行长时间测试
    if (long_duration > 0) {
        std::cout << "4. 长时间运行测试 (" << long_duration << "秒):" << std::endl;
        tester.RunLongRunningTest(long_duration);
        std::cout << std::endl;
    }
    
    std::cout << "==========================================" << std::endl;
    std::cout << "压力测试完成!" << std::endl;
    
    return 0;
}
