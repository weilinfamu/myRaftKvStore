#ifndef IPERSISTENCE_LAYER_H
#define IPERSISTENCE_LAYER_H

#include <string>

/**
 * @brief 持久化层接口 - 抽象Raft状态的持久化操作
 * 
 * 通过这个接口，可以轻松替换持久化方案：
 * - 本地文件存储
 * - 分布式存储（HDFS、Ceph等）
 * - 数据库存储
 * - 内存存储（测试用）
 */
class IPersistenceLayer {
public:
    virtual ~IPersistenceLayer() = default;
    
    /**
     * @brief 保存Raft状态数据
     * @param data Raft状态的序列化数据
     */
    virtual void SaveRaftState(const std::string& data) = 0;
    
    /**
     * @brief 读取Raft状态数据
     * @return Raft状态的序列化数据，如果不存在返回空字符串
     */
    virtual std::string ReadRaftState() = 0;
    
    /**
     * @brief 获取Raft状态数据的大小
     * @return 数据大小（字节）
     */
    virtual long long RaftStateSize() = 0;
    
    /**
     * @brief 保存快照数据
     * @param snapshot 快照的序列化数据
     */
    virtual void SaveSnapshot(const std::string& snapshot) = 0;
    
    /**
     * @brief 读取快照数据
     * @return 快照的序列化数据，如果不存在返回空字符串
     */
    virtual std::string ReadSnapshot() = 0;
    
    /**
     * @brief 同时保存Raft状态和快照
     * @param raftState Raft状态数据
     * @param snapshot 快照数据
     */
    virtual void Save(const std::string& raftState, const std::string& snapshot) = 0;
    
    /**
     * @brief 强制刷盘（确保数据写入磁盘）
     */
    virtual void Flush() = 0;
};

#endif  // IPERSISTENCE_LAYER_H

