#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <random>
#include <string>
#include <vector>
#include "monsoon.h"
#include "src/common/include/util.h"
#include "src/raftClerk/include/clerk.h"

namespace {
struct LatencyStats {
  double avg{0};
  double p50{0};
  double p95{0};
  double p99{0};
};

LatencyStats CalculateLatencyStats(const std::vector<long long>& latenciesNs) {
  if (latenciesNs.empty()) {
    return {};
  }
  auto sorted = latenciesNs;
  std::sort(sorted.begin(), sorted.end());
  LatencyStats stats;
  stats.avg = std::accumulate(sorted.begin(), sorted.end(), 0.0) / sorted.size();
  auto IndexOf = [&](double percentile) {
    auto idx = static_cast<size_t>(std::clamp(percentile * sorted.size(), 0.0, static_cast<double>(sorted.size() - 1)));
    return static_cast<double>(sorted[idx]);
  };
  stats.p50 = IndexOf(0.50);
  stats.p95 = IndexOf(0.95);
  stats.p99 = IndexOf(0.99);
  return stats;
}
}  // namespace

class FiberStressTester {
 public:
  FiberStressTester(int workerThreads, int fiberCount, int opsPerFiber, double putRatio)
      : workerThreads_(workerThreads),
        fiberCount_(fiberCount),
        opsPerFiber_(opsPerFiber),
        putRatio_(putRatio) {}

  void Init(const std::string& configFile) {
    std::cout << "[FiberStressTester] 初始化客户端，配置文件: " << configFile << std::endl;
    client_.Init(configFile);
    std::cout << "[FiberStressTester] 客户端初始化完成" << std::endl;
  }

  void Run(const std::string& mode) {
    ResetStats();
    if (mode == "get" || mode == "mixed" || mode == "append") {
      PrefillKeyspace();
    }

    monsoon::Scheduler scheduler(workerThreads_, false, "FiberStressScheduler");
    auto start = std::chrono::steady_clock::now();

    for (int fiberId = 0; fiberId < fiberCount_; ++fiberId) {
      scheduler.scheduler([this, mode, fiberId]() { FiberWorker(mode, fiberId); });
    }

    scheduler.start();
    scheduler.stop();

    auto finish = std::chrono::steady_clock::now();
    auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(finish - start);

    auto stats = CalculateLatencyStats(latenciesNs_);
    std::cout << "\n========== 协程压测结果 ==========\n";
    std::cout << "模式: " << mode << "\n";
    std::cout << "调度线程数: " << workerThreads_ << ", 协程数: " << fiberCount_ << ", 每协程操作数: " << opsPerFiber_
              << "\n";
    std::cout << "总操作数: " << totalOperations_.load() << ", 成功数: " << successfulOperations_.load()
              << ", 失败数: " << failedOperations_.load() << "\n";
    if (durationMs.count() > 0) {
      double throughput = totalOperations_.load() * 1000.0 / durationMs.count();
      std::cout << "耗时: " << durationMs.count() << " ms, 吞吐量: " << std::fixed << std::setprecision(2) << throughput
                << " ops/s\n";
    }
    std::cout << "延迟 (ns): 平均 " << std::fixed << std::setprecision(0) << stats.avg << ", P50 " << stats.p50
              << ", P95 " << stats.p95 << ", P99 " << stats.p99 << "\n";
    std::cout << "=================================\n";
  }

 private:
  void ResetStats() {
    totalOperations_.store(0);
    successfulOperations_.store(0);
    failedOperations_.store(0);
    std::lock_guard<std::mutex> lock(latencyMutex_);
    latenciesNs_.clear();
  }

  void PrefillKeyspace() {
    std::cout << "[FiberStressTester] 预填充键空间以支持读取/追加测试..." << std::endl;
    for (int fiberId = 0; fiberId < fiberCount_; ++fiberId) {
      for (int j = 0; j < opsPerFiber_; ++j) {
        const std::string key = MakeKey(fiberId, j);
        const std::string value = "prefill_" + std::to_string(j);
        client_.Put(key, value);
      }
    }
  }

  void FiberWorker(const std::string& mode, int fiberId) {
    std::mt19937 rng(fiberId + std::random_device{}());
    std::uniform_int_distribution<int> valueDist(1000, 9999);
    std::uniform_real_distribution<double> ratioDist(0.0, 1.0);
    std::uniform_int_distribution<int> keyDist(0, opsPerFiber_ - 1);

    for (int opIndex = 0; opIndex < opsPerFiber_; ++opIndex) {
      const std::string key = (mode == "get") ? MakeKey(fiberId, keyDist(rng)) : MakeKey(fiberId, opIndex);
      const std::string value = "fiber_value_" + std::to_string(valueDist(rng));
      auto opStart = std::chrono::high_resolution_clock::now();
      bool success = false;

      try {
        if (mode == "put") {
          client_.Put(key, value);
          success = true;
        } else if (mode == "append") {
          client_.Append(key, value);
          success = true;
        } else if (mode == "get") {
          std::string result = client_.Get(key);
          success = !result.empty();
        } else {  // mixed
          if (ratioDist(rng) < putRatio_) {
            client_.Put(key, value);
          } else {
            std::string result = client_.Get(MakeKey(fiberId, keyDist(rng)));
            (void)result;  // 与线程版一致：无论是否命中都视为成功，保持统计口径
            success = true;
            RecordOperation(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - opStart),
                success);
            continue;
          }
          success = true;
        }
      } catch (const std::exception& e) {
        std::cerr << "[FiberStressTester] 操作失败: " << e.what() << std::endl;
        success = false;
      } catch (...) {
        std::cerr << "[FiberStressTester] 操作失败: 未知异常" << std::endl;
        success = false;
      }

      RecordOperation(
          std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - opStart),
          success);
    }
  }

  void RecordOperation(std::chrono::nanoseconds duration, bool success) {
    totalOperations_.fetch_add(1);
    if (success) {
      successfulOperations_.fetch_add(1);
    } else {
      failedOperations_.fetch_add(1);
    }
    std::lock_guard<std::mutex> lock(latencyMutex_);
    latenciesNs_.push_back(duration.count());
  }

  std::string MakeKey(int fiberId, int opIndex) const {
    return "fiber_key_" + std::to_string(fiberId) + "_" + std::to_string(opIndex);
  }

 private:
    Clerk client_;
    int workerThreads_;
    int fiberCount_;
    int opsPerFiber_;
    double putRatio_;

    std::atomic<long> totalOperations_{0};
    std::atomic<long> successfulOperations_{0};
    std::atomic<long> failedOperations_{0};

    std::mutex latencyMutex_;
    std::vector<long long> latenciesNs_;
};

void PrintUsage(const char* programName) {
  std::cout << "协程压测客户端用法:\n";
  std::cout << "  " << programName
            << " <config_file> <scheduler_threads> <fiber_count> <ops_per_fiber> [mode] [put_ratio]\n";
  std::cout << "参数说明:\n";
  std::cout << "  config_file: RAFT集群配置文件路径\n";
  std::cout << "  scheduler_threads: 协程调度线程数\n";
  std::cout << "  fiber_count: 并发协程数\n";
  std::cout << "  ops_per_fiber: 每个协程执行的操作数\n";
  std::cout << "  mode: put|get|append|mixed (默认 mixed)\n";
  std::cout << "  put_ratio: mixed 模式下 PUT 比例 (默认 0.7)\n";
}

int main(int argc, char** argv) {
  if (argc < 5) {
    PrintUsage(argv[0]);
    return 1;
  }

  std::string configFile = argv[1];
  int schedulerThreads = std::stoi(argv[2]);
  int fiberCount = std::stoi(argv[3]);
  int opsPerFiber = std::stoi(argv[4]);
  std::string mode = (argc >= 6) ? argv[5] : "mixed";
  double putRatio = (argc >= 7) ? std::stod(argv[6]) : 0.7;

  if (fiberCount <= 0 || opsPerFiber <= 0 || schedulerThreads <= 0) {
    std::cerr << "协程数、操作数以及线程数必须为正整数\n";
    return 1;
  }
  if (mode == "mixed" && (putRatio < 0.0 || putRatio > 1.0)) {
    std::cerr << "put_ratio 必须处于 [0,1] 区间\n";
    return 1;
  }

  FiberStressTester tester(schedulerThreads, fiberCount, opsPerFiber, putRatio);
  tester.Init(configFile);
  tester.Run(mode);
  return 0;
}
