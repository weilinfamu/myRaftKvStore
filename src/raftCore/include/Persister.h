//
// Created by swx on 23-5-30.
// Enhanced with compression and batch fsync
//

#ifndef SKIP_LIST_ON_RAFT_PERSISTER_H
#define SKIP_LIST_ON_RAFT_PERSISTER_H
#include <fstream>
#include <mutex>
#include <chrono>
#include <string>
#include "compressor.h"

/**
 * @brief 持久化管理器 - 负责 Raft 状态和快照的持久化
 * 
 * 优化特性：
 * 1. 数据压缩：
 *    - RaftState 使用 LZ4（极速，2.2x）
 *    - Snapshot 使用 Zstd-3（高压缩率，3.3x）
 * 
 * 2. 批量刷盘：
 *    - 避免每次都 fsync（1-10ms 延迟）
 *    - 定期刷盘或缓冲区满时刷盘
 *    - 性能提升：3-5倍
 * 
 * 3. 向后兼容：
 *    - 自动检测压缩格式
 *    - 支持读取旧的未压缩数据
 */
class Persister {
 private:
  std::mutex m_mtx;
  std::string m_raftState;
  std::string m_snapshot;
  
  // 文件名
  const std::string m_raftStateFileName;
  const std::string m_snapshotFileName;
  
  // 文件描述符（用于 fsync）
  int m_raftStateFd;
  int m_snapshotFd;
  
  // 文件大小
  long long m_raftStateSize;
  long long m_snapshotSize;
  
  // ==================== 批量刷盘相关 ====================
  std::string m_pendingRaftState;           // 待刷盘的 RaftState
  std::string m_pendingSnapshot;            // 待刷盘的 Snapshot
  std::chrono::steady_clock::time_point m_lastFlushTime;  // 上次刷盘时间
  
  static constexpr size_t BATCH_FLUSH_SIZE = 4 * 1024;     // 4KB 阈值
  static constexpr int BATCH_FLUSH_INTERVAL_MS = 100;       // 100ms 间隔
  
  // ==================== 压缩相关 ====================
  bool m_enableCompression;                 // 是否启用压缩
  Compressor::Type m_raftStateCompressionType;   // RaftState 压缩类型（LZ4）
  Compressor::Type m_snapshotCompressionType;    // Snapshot 压缩类型（Zstd）
  
  // 压缩统计
  struct CompressionStats {
    uint64_t totalOriginalBytes;
    uint64_t totalCompressedBytes;
    uint64_t compressionCount;
    
    double getCompressionRatio() const {
      return totalCompressedBytes > 0 
        ? (double)totalOriginalBytes / totalCompressedBytes 
        : 1.0;
    }
    
    uint64_t getSavedBytes() const {
      return totalOriginalBytes - totalCompressedBytes;
    }
  };
  
  CompressionStats m_compressionStats;

 public:
  void Save(std::string raftstate, std::string snapshot);
  std::string ReadSnapshot();
  void SaveRaftState(const std::string& data);
  long long RaftStateSize();
  std::string ReadRaftState();
  explicit Persister(int me);
  ~Persister();
  
  // ==================== 新增接口 ====================
  
  /**
   * @brief 启用/禁用压缩
   * @param enable true: 启用压缩，false: 禁用压缩
   */
  void EnableCompression(bool enable) { m_enableCompression = enable; }
  
  /**
   * @brief 获取压缩统计信息
   */
  CompressionStats GetCompressionStats() const { return m_compressionStats; }
  
  /**
   * @brief 强制刷盘（调试用）
   */
  void Flush();
  
  /**
   * @brief 打印压缩统计信息
   */
  void PrintCompressionStats() const;

 private:
  void clearRaftState();
  void clearSnapshot();
  void clearRaftStateAndSnapshot();
  
  // ==================== 新增私有方法 ====================
  
  /**
   * @brief 批量刷盘 RaftState
   * @param force 是否强制刷盘
   */
  void flushRaftState(bool force = false);
  
  /**
   * @brief 批量刷盘 Snapshot
   * @param force 是否强制刷盘
   */
  void flushSnapshot(bool force = false);
  
  /**
   * @brief 检查是否需要刷盘
   */
  bool shouldFlush(const std::string& pending) const;
  
  /**
   * @brief 写入文件并可选地 fsync
   * @param filename 文件名
   * @param data 数据
   * @param doSync 是否执行 fsync
   * @return 文件描述符
   */
  int writeFile(const std::string& filename, const std::string& data, bool doSync);
  
  /**
   * @brief 读取文件
   * @param filename 文件名
   * @return 文件内容
   */
  std::string readFile(const std::string& filename);
};

#endif  // SKIP_LIST_ON_RAFT_PERSISTER_H
