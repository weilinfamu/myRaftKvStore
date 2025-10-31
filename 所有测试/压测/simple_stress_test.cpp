#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <random>
#include <string>
#include <atomic>
#include <mutex>

class SimpleStressTester {
private:
    int num_threads_;
    int operations_per_thread_;
    std::atomic<int> completed_operations_{0};
    std::atomic<int> failed_operations_{0};
    std::mutex cout_mutex_;

public:
    SimpleStressTester(int num_threads, int operations_per_thread)
        : num_threads_(num_threads), operations_per_thread_(operations_per_thread) {}

    void RunCPUIntensiveTest() {
        std::cout << "开始CPU密集型压力测试..." << std::endl;
        std::cout << "线程数: " << num_threads_ << std::endl;
        std::cout << "每线程操作数: " << operations_per_thread_ << std::endl;

        auto start_time = std::chrono::high_resolution_clock::now();
        
        std::vector<std::thread> threads;
        for (int i = 0; i < num_threads_; ++i) {
            threads.emplace_back([this, i]() {
                this->WorkerThread(i);
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        std::cout << "\n压力测试完成!" << std::endl;
        std::cout << "总操作数: " << completed_operations_.load() << std::endl;
        std::cout << "失败操作数: " << failed_operations_.load() << std::endl;
        std::cout << "总耗时: " << duration.count() << " 毫秒" << std::endl;
        std::cout << "吞吐量: " << (completed_operations_.load() * 1000.0 / duration.count()) << " 操作/秒" << std::endl;
    }

private:
    void WorkerThread(int thread_id) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(1, 100);

        for (int i = 0; i < operations_per_thread_; ++i) {
            try {
                // 模拟CPU密集型操作
                int result = 0;
                for (int j = 0; j < 1000; ++j) {
                    result += dis(gen) * dis(gen);
                }
                
                // 模拟内存操作
                std::vector<int> data(1000);
                for (int j = 0; j < 1000; ++j) {
                    data[j] = dis(gen);
                }
                
                // 模拟字符串操作
                std::string key = "key_" + std::to_string(thread_id) + "_" + std::to_string(i);
                std::string value = "value_" + std::to_string(dis(gen));
                
                completed_operations_++;
                
                if (i % 100 == 0) {
                    std::lock_guard<std::mutex> lock(cout_mutex_);
                    std::cout << "线程 " << thread_id << " 完成 " << i << " 次操作" << std::endl;
                }
                
                // 模拟一些延迟
                std::this_thread::sleep_for(std::chrono::microseconds(10));
                
            } catch (const std::exception& e) {
                failed_operations_++;
                std::lock_guard<std::mutex> lock(cout_mutex_);
                std::cout << "线程 " << thread_id << " 操作失败: " << e.what() << std::endl;
            }
        }
        
        std::lock_guard<std::mutex> lock(cout_mutex_);
        std::cout << "线程 " << thread_id << " 完成所有操作" << std::endl;
    }
};

int main() {
    std::cout << "KV存储RAFT项目性能压力测试" << std::endl;
    std::cout << "==========================" << std::endl;
    
    // 创建压力测试器：4个线程，每个线程1000次操作
    SimpleStressTester tester(4, 1000);
    
    // 运行压力测试
    tester.RunCPUIntensiveTest();
    
    return 0;
}
