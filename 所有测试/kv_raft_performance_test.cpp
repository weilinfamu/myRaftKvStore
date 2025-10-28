// kv_raft_performance_test.cpp - KV存储RAFT项目综合性能测试（协程版本）
#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <atomic>
#include <mutex>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <map>
#include <algorithm>
#include <numeric>
#include <memory>
#include "src/raftClerk/include/clerk.h"
#include "src/common/include/util.h"
#include "fiber/iomanager.hpp"  // monsoon 协程库

class PerformanceTester {
private:
    int fiberCount;  // 协程数（原 threadCount）
    int operationsPerFiber;  // 每协程操作数（原 operationsPerThread）
    std::atomic<long> totalOperations{0};
    std::atomic<long> successfulOperations{0};
    std::atomic<long> failedOperations{0};
    std::mutex statsMutex;
    std::map<std::string, std::chrono::nanoseconds> operationLatencies;
    std::map<std::string, long> operationCounts;
    std::string configFile_;
    
public:
    PerformanceTester(int fibers, int ops, const std::string& configFile) 
        : fiberCount(fibers), operationsPerFiber(ops), configFile_(configFile) {}
    
    void RecordOperation(const std::string& opType, std::chrono::nanoseconds duration) {
        std::lock_guard<std::mutex> lock(statsMutex);
        operationLatencies[opType] += duration;
        operationCounts[opType]++;
    }
    
    void RunPutTest() {
        std::cout << "开始PUT操作测试（协程模式）..." << std::endl;
        
        // 创建 IOManager（4个工作线程，main线程也参与调度）
        monsoon::IOManager iom(4, true, "put_test_iom");
        
        auto start = std::chrono::high_resolution_clock::now();
        
        // 调度协程任务
        for (int i = 0; i < fiberCount; ++i) {
            iom.scheduler([this, i]() {
                // 每个协程创建独立的 Clerk 实例
                auto clerk = std::make_shared<Clerk>();
                clerk->Init(configFile_);
                
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> dis(1000, 9999);
                
                for (int j = 0; j < operationsPerFiber; ++j) {
                    std::string key = "key_" + std::to_string(i) + "_" + std::to_string(j);
                    std::string value = "value_" + std::to_string(dis(gen));
                    
                    auto op_start = std::chrono::high_resolution_clock::now();
                    try {
                        clerk->Put(key, value);
                        auto op_end = std::chrono::high_resolution_clock::now();
                        RecordOperation("PUT", op_end - op_start);
                        successfulOperations++;
                    } catch (const std::exception& e) {
                        failedOperations++;
                        std::cerr << "PUT操作失败: " << e.what() << std::endl;
                    }
                    totalOperations++;
                }
            });
        }
        
        // IOManager 析构时会自动等待所有协程完成
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        PrintTestResults("PUT测试", duration);
    }
    
    void RunGetTest() {
        std::cout << "开始GET操作测试（协程模式）..." << std::endl;
        
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
                    
                    auto op_start = std::chrono::high_resolution_clock::now();
                    try {
                        std::string result = clerk->Get(key);
                        auto op_end = std::chrono::high_resolution_clock::now();
                        RecordOperation("GET", op_end - op_start);
                        if (!result.empty()) {
                            successfulOperations++;
                        } else {
                            failedOperations++;
                        }
                    } catch (const std::exception& e) {
                        failedOperations++;
                        std::cerr << "GET操作失败: " << e.what() << std::endl;
                    }
                    totalOperations++;
                }
            });
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        PrintTestResults("GET测试", duration);
    }
    
    void RunMixedTest(double putRatio = 0.7) {
        std::cout << "开始混合操作测试（协程模式） (PUT: " << (putRatio * 100) << "%, GET: " << ((1-putRatio) * 100) << "%)..." << std::endl;
        
        monsoon::IOManager iom(4, true, "mixed_test_iom");
        
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < fiberCount; ++i) {
            iom.scheduler([this, i, putRatio]() {
                auto clerk = std::make_shared<Clerk>();
                clerk->Init(configFile_);
                
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> dis(1000, 9999);
                std::uniform_real_distribution<> op_dis(0.0, 1.0);
                
                for (int j = 0; j < operationsPerFiber; ++j) {
                    std::string key = "mixed_key_" + std::to_string(i) + "_" + std::to_string(j);
                    
                    auto op_start = std::chrono::high_resolution_clock::now();
                    try {
                        if (op_dis(gen) < putRatio) { // PUT操作
                            std::string value = "mixed_value_" + std::to_string(dis(gen));
                            clerk->Put(key, value);
                            auto op_end = std::chrono::high_resolution_clock::now();
                            RecordOperation("PUT", op_end - op_start);
                        } else { // GET操作
                            std::string result = clerk->Get(key);
                            auto op_end = std::chrono::high_resolution_clock::now();
                            RecordOperation("GET", op_end - op_start);
                        }
                        successfulOperations++;
                    } catch (const std::exception& e) {
                        failedOperations++;
                        std::cerr << "混合操作失败: " << e.what() << std::endl;
                    }
                    totalOperations++;
                }
            });
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        PrintTestResults("混合测试", duration);
    }
    
    void RunLatencyTest(int sampleCount = 1000) {
        std::cout << "开始延迟测试（协程模式），采样 " << sampleCount << " 次..." << std::endl;
        
        // 创建单个 Clerk 用于延迟测试
        Clerk clerk;
        clerk.Init(configFile_);
        
        std::vector<std::chrono::nanoseconds> putLatencies;
        std::vector<std::chrono::nanoseconds> getLatencies;
        
        // 预热
        for (int i = 0; i < 10; ++i) {
            std::string key = "warmup_" + std::to_string(i);
            clerk.Put(key, "warmup_value");
            clerk.Get(key);
        }
        
        // 测试PUT延迟
        for (int i = 0; i < sampleCount; ++i) {
            std::string key = "latency_put_" + std::to_string(i);
            std::string value = "latency_value_" + std::to_string(i);
            
            auto start = std::chrono::high_resolution_clock::now();
            clerk.Put(key, value);
            auto end = std::chrono::high_resolution_clock::now();
            
            putLatencies.push_back(end - start);
        }
        
        // 测试GET延迟
        for (int i = 0; i < sampleCount; ++i) {
            std::string key = "latency_put_" + std::to_string(i);
            
            auto start = std::chrono::high_resolution_clock::now();
            std::string result = clerk.Get(key);
            auto end = std::chrono::high_resolution_clock::now();
            
            getLatencies.push_back(end - start);
        }
        
        // 计算统计信息
        auto putStats = CalculateLatencyStats(putLatencies);
        auto getStats = CalculateLatencyStats(getLatencies);
        
        std::cout << "延迟测试结果:" << std::endl;
        std::cout << "PUT操作 - 平均: " << putStats.avg << "ns, P50: " << putStats.p50 
                  << "ns, P95: " << putStats.p95 << "ns, P99: " << putStats.p99 << "ns" << std::endl;
        std::cout << "GET操作 - 平均: " << getStats.avg << "ns, P50: " << getStats.p50 
                  << "ns, P95: " << getStats.p95 << "ns, P99: " << getStats.p99 << "ns" << std::endl;
    }
    
    void PrintDetailedStatistics() {
        std::cout << "\n详细统计信息:" << std::endl;
        std::cout << "==========================================" << std::endl;
        
        for (const auto& [opType, count] : operationCounts) {
            if (count > 0) {
                auto avgLatency = operationLatencies[opType].count() / count;
                std::cout << opType << "操作:" << std::endl;
                std::cout << "  总次数: " << count << std::endl;
                std::cout << "  平均延迟: " << avgLatency << "ns (" << (avgLatency / 1000.0) << "μs)" << std::endl;
            }
        }
        
        std::cout << "==========================================" << std::endl;
        std::cout << "总操作数: " << totalOperations.load() << std::endl;
        std::cout << "成功操作数: " << successfulOperations.load() << std::endl;
        std::cout << "失败操作数: " << failedOperations.load() << std::endl;
        if (totalOperations.load() > 0) {
            std::cout << "成功率: " << (successfulOperations.load() * 100.0 / totalOperations.load()) << "%" << std::endl;
        }
    }
    
private:
    void PrintTestResults(const std::string& testName, std::chrono::milliseconds duration) {
        double throughput = totalOperations.load() * 1000.0 / duration.count();
        std::cout << testName << "完成:" << std::endl;
        std::cout << "  总操作数: " << totalOperations.load() << std::endl;
        std::cout << "  耗时: " << duration.count() << "ms" << std::endl;
        std::cout << "  吞吐量: " << std::fixed << std::setprecision(2) << throughput << " ops/s" << std::endl;
        if (totalOperations.load() > 0) {
            std::cout << "  成功率: " << (successfulOperations.load() * 100.0 / totalOperations.load()) << "%" << std::endl;
        }
    }
    
    struct LatencyStats {
        double avg;
        double p50;
        double p95;
        double p99;
    };
    
    LatencyStats CalculateLatencyStats(const std::vector<std::chrono::nanoseconds>& latencies) {
        if (latencies.empty()) return {0, 0, 0, 0};
        
        // 复制并排序
        auto sorted = latencies;
        std::sort(sorted.begin(), sorted.end());
        
        LatencyStats stats;
        stats.avg = std::accumulate(sorted.begin(), sorted.end(), 0.0, 
            [](double sum, const auto& latency) { return sum + latency.count(); }) / sorted.size();
        
        stats.p50 = sorted[sorted.size() * 0.5].count();
        stats.p95 = sorted[sorted.size() * 0.95].count();
        stats.p99 = sorted[sorted.size() * 0.99].count();
        
        return stats;
    }
};

void PrintUsage(const char* programName) {
    std::cout << "KV存储RAFT项目综合性能测试工具（协程版本）" << std::endl;
    std::cout << "用法: " << programName << " <config_file> <fibers> <operations_per_fiber> [test_type]" << std::endl;
    std::cout << std::endl;
    std::cout << "参数说明:" << std::endl;
    std::cout << "  config_file: RAFT集群配置文件路径" << std::endl;
    std::cout << "  fibers: 并发协程数（原线程数）" << std::endl;
    std::cout << "  operations_per_fiber: 每个协程的操作数" << std::endl;
    std::cout << "  test_type: 测试类型 (可选)" << std::endl;
    std::cout << "    all - 运行所有测试 (默认)" << std::endl;
    std::cout << "    put - 只运行PUT测试" << std::endl;
    std::cout << "    get - 只运行GET测试" << std::endl;
    std::cout << "    mixed - 只运行混合测试" << std::endl;
    std::cout << "    latency - 只运行延迟测试" << std::endl;
    std::cout << std::endl;
    std::cout << "示例:" << std::endl;
    std::cout << "  " << programName << " test.conf 4 1000 all" << std::endl;
    std::cout << "  " << programName << " test.conf 8 500 latency" << std::endl;
    std::cout << "  " << programName << " test.conf 2 2000 put" << std::endl;
    std::cout << std::endl;
    std::cout << "注意:" << std::endl;
    std::cout << "  - 此版本使用 monsoon 协程库，RPC调用会自动异步化" << std::endl;
    std::cout << "  - Hook 机制会将阻塞的网络I/O转换为协程友好的操作" << std::endl;
    std::cout << "  - 4个工作线程可以高效调度大量协程" << std::endl;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        PrintUsage(argv[0]);
        return 1;
    }
    
    std::string configFile = argv[1];
    int fibers = std::stoi(argv[2]);
    int ops = std::stoi(argv[3]);
    std::string testType = "all";
    
    if (argc >= 5) {
        testType = argv[4];
    }
    
    std::cout << "==========================================" << std::endl;
    std::cout << "KV存储RAFT项目综合性能测试（协程版本）" << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "配置: " << fibers << " 协程, " << ops << " 操作/协程" << std::endl;
    std::cout << "测试类型: " << testType << std::endl;
    std::cout << "配置文件: " << configFile << std::endl;
    std::cout << "IOManager: 4 个工作线程 + main线程参与调度" << std::endl;
    std::cout << "==========================================" << std::endl;
    
    PerformanceTester tester(fibers, ops, configFile);
    
    if (testType == "all" || testType == "put") {
        std::cout << "\n[1] PUT操作性能测试" << std::endl;
        tester.RunPutTest();
    }
    
    if (testType == "all" || testType == "get") {
        std::cout << "\n[2] GET操作性能测试" << std::endl;
        tester.RunGetTest();
    }
    
    if (testType == "all" || testType == "mixed") {
        std::cout << "\n[3] 混合操作性能测试" << std::endl;
        tester.RunMixedTest();
    }
    
    if (testType == "all" || testType == "latency") {
        std::cout << "\n[4] 延迟测试" << std::endl;
        tester.RunLatencyTest();
    }
    
    if (testType == "all") {
        std::cout << "\n[5] 详细统计信息" << std::endl;
        tester.PrintDetailedStatistics();
    }
    
    std::cout << "\n==========================================" << std::endl;
    std::cout << "性能测试完成!" << std::endl;
    std::cout << "==========================================" << std::endl;
    
    return 0;
}
