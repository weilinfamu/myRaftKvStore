//
// Created by swx on 23-5-30.
// Enhanced with compression and batch fsync
//

#include "Persister.h"
#include "util.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <iostream>
#include <sstream>

Persister::Persister(const int me)
    : m_raftStateFileName("raftstatePersist" + std::to_string(me) + ".txt"),
      m_snapshotFileName("snapshotPersist" + std::to_string(me) + ".txt"),
      m_raftStateFd(-1),
      m_snapshotFd(-1),
      m_raftStateSize(0),
      m_snapshotSize(0),
      m_enableCompression(true),  // 默认启用压缩
      m_raftStateCompressionType(Compressor::Type::LZ4),    // RaftState 用 LZ4
      m_snapshotCompressionType(Compressor::Type::ZSTD),    // Snapshot 用 Zstd
      m_lastFlushTime(std::chrono::steady_clock::now()) {
  
  // 初始化压缩统计
  m_compressionStats.totalOriginalBytes = 0;
  m_compressionStats.totalCompressedBytes = 0;
  m_compressionStats.compressionCount = 0;
  
  // 清空旧文件
  std::ofstream ofs_raft(m_raftStateFileName, std::ios::out | std::ios::trunc);
  if (!ofs_raft.is_open()) {
    std::cerr << "[Persister] ERROR: Cannot open " << m_raftStateFileName << std::endl;
  }
  ofs_raft.close();
  
  std::ofstream ofs_snap(m_snapshotFileName, std::ios::out | std::ios::trunc);
  if (!ofs_snap.is_open()) {
    std::cerr << "[Persister] ERROR: Cannot open " << m_snapshotFileName << std::endl;
  }
  ofs_snap.close();
  
  std::cout << "[Persister] Initialized for node " << me << std::endl;
  std::cout << "[Persister]   Compression: " << (m_enableCompression ? "ENABLED" : "DISABLED") << std::endl;
  std::cout << "[Persister]   RaftState compression: LZ4 (2.2x expected)" << std::endl;
  std::cout << "[Persister]   Snapshot compression: Zstd-3 (3.3x expected)" << std::endl;
  std::cout << "[Persister]   Batch flush: " << BATCH_FLUSH_SIZE << " bytes or " 
            << BATCH_FLUSH_INTERVAL_MS << " ms" << std::endl;
}

Persister::~Persister() {
  // 确保所有数据都刷盘
  Flush();
  
  if (m_raftStateFd >= 0) {
    close(m_raftStateFd);
  }
  if (m_snapshotFd >= 0) {
    close(m_snapshotFd);
  }
  
  // 打印最终统计
  PrintCompressionStats();
}

// ==================== 核心保存接口 ====================

void Persister::Save(std::string raftstate, std::string snapshot) {
  std::lock_guard<std::mutex> lg(m_mtx);
  
  // 保存 RaftState
  m_pendingRaftState = raftstate;
  flushRaftState(true);  // 立即刷盘（保证一致性）
  
  // 保存 Snapshot
  m_pendingSnapshot = snapshot;
  flushSnapshot(true);   // 立即刷盘
}

void Persister::SaveRaftState(const std::string& data) {
  std::lock_guard<std::mutex> lg(m_mtx);
  
  // 添加到待刷盘缓冲区
  m_pendingRaftState = data;
  
  // 检查是否需要刷盘
  flushRaftState(false);  // 非强制，根据策略决定
}

std::string Persister::ReadRaftState() {
  std::lock_guard<std::mutex> lg(m_mtx);
  
  // 先刷盘确保数据完整
  flushRaftState(true);
  
  std::string fileData = readFile(m_raftStateFileName);
  if (fileData.empty()) {
    return "";
  }
  
  // 尝试解压
  try {
    Compressor::Type type;
    std::string decompressed = Compressor::decompressAdaptive(fileData, &type);
    
    if (type != Compressor::Type::NONE) {
      std::cout << "[Persister] ReadRaftState: decompressed " << fileData.size() 
                << " -> " << decompressed.size() << " bytes" << std::endl;
    }
    
    return decompressed;
  } catch (const std::exception& e) {
    // 如果解压失败，可能是未压缩的旧数据
    std::cout << "[Persister] ReadRaftState: not compressed or corrupt, "
              << "using original data" << std::endl;
    return fileData;
  }
}

std::string Persister::ReadSnapshot() {
  std::lock_guard<std::mutex> lg(m_mtx);
  
  // 先刷盘确保数据完整
  flushSnapshot(true);
  
  std::string fileData = readFile(m_snapshotFileName);
  if (fileData.empty()) {
    return "";
  }
  
  // 尝试解压
  try {
    Compressor::Type type;
    std::string decompressed = Compressor::decompressAdaptive(fileData, &type);
    
    if (type != Compressor::Type::NONE) {
      std::cout << "[Persister] ReadSnapshot: decompressed " << fileData.size() 
                << " -> " << decompressed.size() << " bytes" << std::endl;
    }
    
    return decompressed;
  } catch (const std::exception& e) {
    std::cout << "[Persister] ReadSnapshot: not compressed or corrupt, "
              << "using original data" << std::endl;
    return fileData;
  }
}

long long Persister::RaftStateSize() {
  std::lock_guard<std::mutex> lg(m_mtx);
  return m_raftStateSize;
}

// ==================== 批量刷盘实现 ====================

bool Persister::shouldFlush(const std::string& pending) const {
  if (pending.empty()) {
    return false;
  }
  
  // 策略1：缓冲区满
  if (pending.size() >= BATCH_FLUSH_SIZE) {
    return true;
  }
  
  // 策略2：距离上次刷盘超过阈值
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      now - m_lastFlushTime
  );
  
  if (elapsed.count() >= BATCH_FLUSH_INTERVAL_MS) {
    return true;
  }
  
  return false;
}

void Persister::flushRaftState(bool force) {
  if (m_pendingRaftState.empty()) {
    return;
  }
  
  if (!force && !shouldFlush(m_pendingRaftState)) {
    return;  // 不需要刷盘
  }
  
  std::string dataToWrite = m_pendingRaftState;
  size_t originalSize = dataToWrite.size();
  
  // 压缩（如果启用）
  if (m_enableCompression) {
    try {
      dataToWrite = Compressor::compressAdaptive(dataToWrite, m_raftStateCompressionType);
      
      // 更新统计
      m_compressionStats.totalOriginalBytes += originalSize;
      m_compressionStats.totalCompressedBytes += dataToWrite.size();
      m_compressionStats.compressionCount++;
      
    } catch (const std::exception& e) {
      std::cerr << "[Persister] Compression failed: " << e.what() 
                << ", using original data" << std::endl;
      // 压缩失败，使用原始数据
    }
  }
  
  // 写入文件
  m_raftStateFd = writeFile(m_raftStateFileName, dataToWrite, true);  // 同步写入
  m_raftStateSize = originalSize;  // 记录原始大小
  m_lastFlushTime = std::chrono::steady_clock::now();
  
  // 清空缓冲区
  m_pendingRaftState.clear();
  
  std::cout << "[Persister] Flushed RaftState: " << originalSize << " bytes" 
            << (m_enableCompression ? " (compressed to " + std::to_string(dataToWrite.size()) + ")" : "")
            << std::endl;
}

void Persister::flushSnapshot(bool force) {
  if (m_pendingSnapshot.empty()) {
    return;
  }
  
  if (!force && !shouldFlush(m_pendingSnapshot)) {
    return;
  }
  
  std::string dataToWrite = m_pendingSnapshot;
  size_t originalSize = dataToWrite.size();
  
  // 压缩（如果启用）
  if (m_enableCompression) {
    try {
      dataToWrite = Compressor::compressAdaptive(dataToWrite, m_snapshotCompressionType);
      
      // 更新统计
      m_compressionStats.totalOriginalBytes += originalSize;
      m_compressionStats.totalCompressedBytes += dataToWrite.size();
      m_compressionStats.compressionCount++;
      
    } catch (const std::exception& e) {
      std::cerr << "[Persister] Compression failed: " << e.what() 
                << ", using original data" << std::endl;
    }
  }
  
  // 写入文件
  m_snapshotFd = writeFile(m_snapshotFileName, dataToWrite, true);  // 同步写入
  m_snapshotSize = originalSize;
  m_lastFlushTime = std::chrono::steady_clock::now();
  
  // 清空缓冲区
  m_pendingSnapshot.clear();
  
  std::cout << "[Persister] Flushed Snapshot: " << originalSize << " bytes" 
            << (m_enableCompression ? " (compressed to " + std::to_string(dataToWrite.size()) + ")" : "")
            << std::endl;
}

void Persister::Flush() {
  std::lock_guard<std::mutex> lg(m_mtx);
  flushRaftState(true);
  flushSnapshot(true);
}

// ==================== 文件I/O辅助函数 ====================

int Persister::writeFile(const std::string& filename, const std::string& data, bool doSync) {
  // 使用 O_WRONLY | O_CREAT | O_TRUNC 打开文件
  int fd = open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    std::cerr << "[Persister] ERROR: Cannot open " << filename 
              << " for writing: " << strerror(errno) << std::endl;
    return -1;
  }
  
  // 写入数据
  ssize_t written = write(fd, data.data(), data.size());
  if (written != static_cast<ssize_t>(data.size())) {
    std::cerr << "[Persister] ERROR: Write incomplete to " << filename 
              << ": wrote " << written << " of " << data.size() << " bytes" << std::endl;
    close(fd);
    return -1;
  }
  
  // fsync（如果需要）
  if (doSync) {
    if (fsync(fd) != 0) {
      std::cerr << "[Persister] WARNING: fsync failed for " << filename 
                << ": " << strerror(errno) << std::endl;
    }
  }
  
  return fd;  // 保持打开状态以便后续 fsync
}

std::string Persister::readFile(const std::string& filename) {
  int fd = open(filename.c_str(), O_RDONLY);
  if (fd < 0) {
    return "";
  }
  
  // 获取文件大小
  struct stat st;
  if (fstat(fd, &st) != 0) {
    close(fd);
    return "";
  }
  
  size_t fileSize = st.st_size;
  if (fileSize == 0) {
    close(fd);
    return "";
  }
  
  // 读取文件内容
  std::string data(fileSize, '\0');
  ssize_t bytesRead = read(fd, &data[0], fileSize);
  
  close(fd);
  
  if (bytesRead != static_cast<ssize_t>(fileSize)) {
    std::cerr << "[Persister] WARNING: Read incomplete from " << filename << std::endl;
    return "";
  }
  
  return data;
}

// ==================== 兼容旧接口 ====================

void Persister::clearRaftState() {
  m_raftStateSize = 0;
  m_pendingRaftState.clear();
  
  // 清空文件
  int fd = open(m_raftStateFileName.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd >= 0) {
    if (m_raftStateFd >= 0 && m_raftStateFd != fd) {
      close(m_raftStateFd);
    }
    m_raftStateFd = fd;
  }
}

void Persister::clearSnapshot() {
  m_snapshotSize = 0;
  m_pendingSnapshot.clear();
  
  // 清空文件
  int fd = open(m_snapshotFileName.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd >= 0) {
    if (m_snapshotFd >= 0 && m_snapshotFd != fd) {
      close(m_snapshotFd);
    }
    m_snapshotFd = fd;
  }
}

void Persister::clearRaftStateAndSnapshot() {
  clearRaftState();
  clearSnapshot();
}

// ==================== 统计信息 ====================

void Persister::PrintCompressionStats() const {
  if (m_compressionStats.compressionCount == 0) {
    std::cout << "[Persister] No compression statistics available" << std::endl;
    return;
  }
  
  std::cout << "\n==================== Compression Statistics ====================" << std::endl;
  std::cout << "Total compressions: " << m_compressionStats.compressionCount << std::endl;
  std::cout << "Original size:      " << m_compressionStats.totalOriginalBytes << " bytes ("
            << (m_compressionStats.totalOriginalBytes / 1024.0) << " KB)" << std::endl;
  std::cout << "Compressed size:    " << m_compressionStats.totalCompressedBytes << " bytes ("
            << (m_compressionStats.totalCompressedBytes / 1024.0) << " KB)" << std::endl;
  std::cout << "Compression ratio:  " << m_compressionStats.getCompressionRatio() << "x" << std::endl;
  std::cout << "Space saved:        " << m_compressionStats.getSavedBytes() << " bytes ("
            << (m_compressionStats.getSavedBytes() / 1024.0) << " KB, "
            << (100.0 * m_compressionStats.getSavedBytes() / m_compressionStats.totalOriginalBytes) << "%)" << std::endl;
  std::cout << "================================================================\n" << std::endl;
}

