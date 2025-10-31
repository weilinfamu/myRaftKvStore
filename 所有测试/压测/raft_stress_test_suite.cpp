#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <string>
#include <atomic>
#include <mutex>
#include <memory>
#include <fstream>
#include <sstream>

// RAFT项目头文件
#include "kvServer.h"
#include "clerk.h"
#include "skipList.h"
#include "raft.h"
#include "Persister.h"
#include "fiber/iomanager.hpp"  // monsoon 协程库

class RaftStressTester {
private:
    std::atomic<int> completed_operations_{0};
    std::atomic<int> failed_operations_{0};
    std::mutex cout_mutex_;
    std::string config_file_;

public:
    RaftStressTester(const std::string& config_file) : config_file_(config_file) {}

    // ==================== 存储层压力测试（不使用 RPC，保留原线程模式）====================
    void RunSkipListStressTest() {
        std::cout << "\n=== 存储层压力测试 (SkipList) ===" << std::endl;
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // 创建跳表实例
        SkipList<std::string, std::string> skipList(16);
        
        // 注意：存储层测试不涉及 RPC，可以使用协程或线程
        // 这里使用协程来展示 monsoon 的能力
        monsoon::IOManager iom(4, true, "skiplist_test_iom");
        
        int num_fibers = 8;
        int operations_per_fiber = 1000;
        
        for (int i = 0; i < num_fibers; ++i) {
            iom.scheduler([this, &skipList, i, operations_per_fiber]() {
                this->SkipListWorkerFiber(skipList, i, operations_per_fiber);
            });
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        std::cout << "存储层测试完成!" << std::endl;
        std::cout << "跳表大小: " << skipList.size() << std::endl;
        std::cout << "总操作数: " << completed_operations_.load() << std::endl;
        std::cout << "失败操作数: " << failed_operations_.load() << std::endl;
        std::cout << "总耗时: " << duration.count() << " 毫秒" << std::endl;
        std::cout << "吞吐量: " << (completed_operations_.load() * 1000.0 / duration.count()) << " 操作/秒" << std::endl;
    }

private:
    void SkipListWorkerFiber(SkipList<std::string, std::string>& skipList, int fiber_id, int operations) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> op_dis(0, 2); // 0:插入, 1:查找, 2:删除
        std::uniform_int_distribution<> key_dis(1, 10000);
        
        for (int i = 0; i < operations; ++i) {
            try {
                std::string key = "key_" + std::to_string(fiber_id) + "_" + std::to_string(key_dis(gen));
                std::string value = "value_" + std::to_string(key_dis(gen));
                
                int op_type = op_dis(gen);
                
                switch (op_type) {
                    case 0: // 插入
                        skipList.insert_element(key, value);
                        break;
                    case 1: // 查找
                        {
                            std::string found_value;
                            skipList.search_element(key, found_value);
                        }
                        break;
                    case 2: // 删除
                        skipList.delete_element(key);
                        break;
                }
                
                completed_operations_++;
                
                if (i % 200 == 0) {
                    std::lock_guard<std::mutex> lock(cout_mutex_);
                    std::cout << "存储层协程 " << fiber_id << " 完成 " << i << " 次操作" << std::endl;
                }
                
            } catch (const std::exception& e) {
                failed_operations_++;
                std::lock_guard<std::mutex> lock(cout_mutex_);
                std::cout << "存储层协程 " << fiber_id << " 操作失败: " << e.what() << std::endl;
            }
        }
        
        std::lock_guard<std::mutex> lock(cout_mutex_);
        std::cout << "存储层协程 " << fiber_id << " 完成所有操作" << std::endl;
    }

public:
    // ==================== 客户端压力测试（使用协程）====================
    void RunClientStressTest() {
        std::cout << "\n=== 客户端压力测试 (Clerk) ===" << std::endl;
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // 使用协程进行客户端测试
        monsoon::IOManager iom(4, true, "client_test_iom");
        
        int num_fibers = 4;
        int operations_per_fiber = 500;
        
        for (int i = 0; i < num_fibers; ++i) {
            iom.scheduler([this, i, operations_per_fiber]() {
                this->ClientWorkerFiber(i, operations_per_fiber);
            });
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        std::cout << "客户端测试完成!" << std::endl;
        std::cout << "总操作数: " << completed_operations_.load() << std::endl;
        std::cout << "失败操作数: " << failed_operations_.load() << std::endl;
        std::cout << "总耗时: " << duration.count() << " 毫秒" << std::endl;
        std::cout << "吞吐量: " << (completed_operations_.load() * 1000.0 / duration.count()) << " 操作/秒" << std::endl;
    }

private:
    void ClientWorkerFiber(int fiber_id, int operations) {
        try {
            // 每个协程创建独立的 Clerk
            auto clerk = std::make_shared<Clerk>();
            clerk->Init(config_file_);
            
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> op_dis(0, 2); // 0:Put, 1:Get, 2:Append
            std::uniform_int_distribution<> key_dis(1, 1000);
            
            for (int i = 0; i < operations; ++i) {
                std::string key = "client_key_" + std::to_string(fiber_id) + "_" + std::to_string(key_dis(gen));
                std::string value = "client_value_" + std::to_string(key_dis(gen));
                
                int op_type = op_dis(gen);
                
                switch (op_type) {
                    case 0: // Put
                        clerk->Put(key, value);
                        break;
                    case 1: // Get
                        {
                            std::string result = clerk->Get(key);
                            // 忽略结果，只测试性能
                        }
                        break;
                    case 2: // Append
                        clerk->Append(key, value);
                        break;
                }
                
                completed_operations_++;
                
                if (i % 100 == 0) {
                    std::lock_guard<std::mutex> lock(cout_mutex_);
                    std::cout << "客户端协程 " << fiber_id << " 完成 " << i << " 次操作" << std::endl;
                }
            }
            
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(cout_mutex_);
            std::cout << "客户端协程 " << fiber_id << " 初始化失败: " << e.what() << std::endl;
            return;
        }
        
        std::lock_guard<std::mutex> lock(cout_mutex_);
        std::cout << "客户端协程 " << fiber_id << " 完成所有操作" << std::endl;
    }

public:
    // ==================== Worst Case 测试场景 ====================
    void RunWorstCaseTests() {
        std::cout << "\n=== Worst Case 测试场景 ===" << std::endl;
        
        // 1. 高并发写入冲突测试
        RunHighConcurrencyWriteConflictTest();
        
        // 2. 大键值对测试
        RunLargeKeyValueTest();
        
        // 3. 频繁领导选举测试
        RunFrequentLeaderElectionTest();
        
        // 4. 网络分区测试
        RunNetworkPartitionTest();
    }

private:
    void RunHighConcurrencyWriteConflictTest() {
        std::cout << "\n--- 高并发写入冲突测试（协程版本）---" << std::endl;
        
        SkipList<std::string, std::string> skipList(16);
        
        monsoon::IOManager iom(4, true, "conflict_test_iom");
        
        int num_fibers = 16;
        int operations_per_fiber = 100;
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // 所有协程同时写入相同的键
        for (int i = 0; i < num_fibers; ++i) {
            iom.scheduler([&skipList, i, operations_per_fiber]() {
                std::string conflict_key = "conflict_key";
                
                for (int j = 0; j < operations_per_fiber; ++j) {
                    std::string value = "fiber_" + std::to_string(i) + "_value_" + std::to_string(j);
                    skipList.insert_set_element(conflict_key, value);
                    // 协程会自动 yield，不需要显式 sleep
                }
            });
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        std::cout << "高并发写入冲突测试完成" << std::endl;
        std::cout << "协程数: " << num_fibers << std::endl;
        std::cout << "总操作数: " << num_fibers * operations_per_fiber << std::endl;
        std::cout << "耗时: " << duration.count() << " 毫秒" << std::endl;
    }

    void RunLargeKeyValueTest() {
        std::cout << "\n--- 大键值对测试 ---" << std::endl;
        
        SkipList<std::string, std::string> skipList(16);
        
        // 生成大键值对
        std::string large_key(1000, 'K'); // 1KB key
        std::string large_value(10000, 'V'); // 10KB value
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        int operations = 100;
        for (int i = 0; i < operations; ++i) {
            std::string key = large_key + "_" + std::to_string(i);
            std::string value = large_value + "_" + std::to_string(i);
            skipList.insert_element(key, value);
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        std::cout << "大键值对测试完成" << std::endl;
        std::cout << "键大小: " << large_key.size() << " 字节" << std::endl;
        std::cout << "值大小: " << large_value.size() << " 字节" << std::endl;
        std::cout << "操作数: " << operations << std::endl;
        std::cout << "耗时: " << duration.count() << " 毫秒" << std::endl;
        std::cout << "跳表大小: " << skipList.size() << std::endl;
    }

    void RunFrequentLeaderElectionTest() {
        std::cout << "\n--- 频繁领导选举测试 (模拟) ---" << std::endl;
        std::cout << "注意: 此测试需要实际的RAFT集群运行" << std::endl;
        std::cout << "测试场景: 模拟网络不稳定导致频繁领导选举" << std::endl;
        
        std::cout << "建议手动测试方法:" << std::endl;
        std::cout << "1. 启动3节点RAFT集群" << std::endl;
        std::cout << "2. 每隔10秒杀死当前leader进程" << std::endl;
        std::cout << "3. 观察系统恢复时间和数据一致性" << std::endl;
    }

    void RunNetworkPartitionTest() {
        std::cout << "\n--- 网络分区测试 (模拟) ---" << std::endl;
        std::cout << "注意: 此测试需要实际的RAFT集群运行" << std::endl;
        std::cout << "测试场景: 模拟网络分区导致脑裂" << std::endl;
        
        std::cout << "建议手动测试方法:" << std::endl;
        std::cout << "1. 启动5节点RAFT集群" << std::endl;
        std::cout << "2. 模拟网络分区，将集群分为2+3两组" << std::endl;
        std::cout << "3. 观察多数派选举和数据一致性" << std::endl;
        std::cout << "4. 恢复网络，观察数据同步" << std::endl;
    }

public:
    // ==================== 完整压力测试套件 ====================
    void RunCompleteStressTestSuite() {
        std::cout << "RAFT KV存储系统完整压力测试套件（协程版本）" << std::endl;
        std::cout << "======================================" << std::endl;
        
        // 重置计数器
        completed_operations_ = 0;
        failed_operations_ = 0;
        
        // 1. 存储层测试
        RunSkipListStressTest();
        
        // 重置计数器
        completed_operations_ = 0;
        failed_operations_ = 0;
        
        // 2. 客户端测试 (需要RAFT集群运行)
        std::cout << "\n提示: 客户端测试需要RAFT集群正在运行" << std::endl;
        std::cout << "请先使用以下命令启动集群:" << std::endl;
        std::cout << "./bin/raftKvDB -n 3 -f test.conf" << std::endl;
        std::cout << "按回车键继续客户端测试，或Ctrl+C跳过..." << std::endl;
        std::cin.get();
        
        RunClientStressTest();
        
        // 3. Worst Case测试
        RunWorstCaseTests();
        
        std::cout << "\n=== 所有测试完成 ===" << std::endl;
    }
};

// 简单的配置生成器
void GenerateTestConfig(int node_count, const std::string& filename) {
    std::ofstream file(filename, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        std::cerr << "无法创建配置文件: " << filename << std::endl;
        return;
    }
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> port_dis(10000, 20000);
    
    for (int i = 0; i < node_count; ++i) {
        unsigned short port = port_dis(gen);
        file << i << " 127.0.0.1 " << port << std::endl;
    }
    
    file.close();
    std::cout << "已生成测试配置文件: " << filename << " (节点数: " << node_count << ")" << std::endl;
}

int main(int argc, char** argv) {
    std::cout << "RAFT KV存储系统压力测试套件（协程版本）" << std::endl;
    std::cout << "========================================" << std::endl;
    
    std::string config_file = "stress_test.conf";
    
    // 生成测试配置
    GenerateTestConfig(3, config_file);
    
    // 创建压力测试器
    RaftStressTester tester(config_file);
    
    if (argc > 1) {
        std::string test_type = argv[1];
        if (test_type == "storage") {
            tester.RunSkipListStressTest();
        } else if (test_type == "client") {
            tester.RunClientStressTest();
        } else if (test_type == "worstcase") {
            tester.RunWorstCaseTests();
        } else {
            std::cout << "未知测试类型，运行完整测试套件" << std::endl;
            tester.RunCompleteStressTestSuite();
        }
    } else {
        // 运行完整测试套件
        tester.RunCompleteStressTestSuite();
    }
    
    return 0;
}
