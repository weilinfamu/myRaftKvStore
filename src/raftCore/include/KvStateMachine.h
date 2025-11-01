#ifndef KV_STATE_MACHINE_H
#define KV_STATE_MACHINE_H

#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/serialization/serialization.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>

#include "ApplyMsg.h"
#include "IStateMachine.h"
#include "IStorageEngine.h"
#include "util.h"  // Op类定义在这里

/**
 * @brief KV状态机 - 纯业务逻辑实现
 * 
 * 职责：
 * 1. 执行KV操作（Get/Put/Append）
 * 2. 去重（防止重复执行）
 * 3. 快照管理
 * 
 * 不负责：
 * 1. RPC通信
 * 2. Raft共识
 * 3. 请求路由
 */
class KvStateMachine : public IStateMachine {
private:
    std::unique_ptr<IStorageEngine> m_storage;
    std::unordered_map<std::string, int> m_lastRequestId;  // clientId -> requestId (去重用)
    mutable std::mutex m_mtx;
    
public:
    explicit KvStateMachine(std::unique_ptr<IStorageEngine> storage)
        : m_storage(std::move(storage)) {}
    
    // ==================== IStateMachine接口实现 ====================
    
    /**
     * @brief 应用一条日志
     */
    void Apply(const std::string& command, int index) override {
        Op op = DeserializeOp(command);
        
        std::lock_guard<std::mutex> lock(m_mtx);
        
        // 去重检查
        if (IsDuplicate(op.ClientId, op.RequestId)) {
            return;
        }
        
        // 执行操作
        if (op.Operation == "Put") {
            m_storage->Put(op.Key, op.Value);
        } else if (op.Operation == "Append") {
            m_storage->Append(op.Key, op.Value);
        }
        // Get操作不修改状态，不需要执行
        
        // 更新去重表
        m_lastRequestId[op.ClientId] = op.RequestId;
    }
    
    /**
     * @brief 生成快照
     */
    std::string TakeSnapshot() override {
        std::lock_guard<std::mutex> lock(m_mtx);
        
        std::stringstream ss;
        boost::archive::text_oarchive oa(ss);
        
        // 序列化存储引擎数据
        std::string storageData = m_storage->Serialize();
        oa << storageData;
        
        // 序列化去重表
        oa << m_lastRequestId;
        
        return ss.str();
    }
    
    /**
     * @brief 从快照恢复
     */
    void InstallSnapshot(const std::string& snapshot) override {
        std::lock_guard<std::mutex> lock(m_mtx);
        
        std::stringstream ss(snapshot);
        boost::archive::text_iarchive ia(ss);
        
        // 反序列化存储引擎数据
        std::string storageData;
        ia >> storageData;
        m_storage->Deserialize(storageData);
        
        // 反序列化去重表
        ia >> m_lastRequestId;
    }
    
    // ==================== 业务逻辑接口（供KvServer调用） ====================
    
    /**
     * @brief 执行Get操作
     */
    bool Get(const std::string& key, std::string* value) {
        std::lock_guard<std::mutex> lock(m_mtx);
        return m_storage->Get(key, value);
    }
    
    /**
     * @brief 检查请求是否重复
     */
    bool IsDuplicateRequest(const std::string& clientId, int requestId) {
        std::lock_guard<std::mutex> lock(m_mtx);
        return IsDuplicate(clientId, requestId);
    }
    
    /**
     * @brief 更新去重表（用于Get操作）
     */
    void UpdateRequestId(const std::string& clientId, int requestId) {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_lastRequestId[clientId] = requestId;
    }
    
    // ==================== 辅助方法 ====================
    
    /**
     * @brief 序列化Op
     */
    static std::string SerializeOp(const Op& op) {
        std::stringstream ss;
        boost::archive::text_oarchive oa(ss);
        oa << op;
        return ss.str();
    }
    
    /**
     * @brief 反序列化Op
     */
    static Op DeserializeOp(const std::string& data) {
        std::stringstream ss(data);
        boost::archive::text_iarchive ia(ss);
        Op op;
        ia >> op;
        return op;
    }
    
private:
    /**
     * @brief 检查是否重复（需要持有锁）
     */
    bool IsDuplicate(const std::string& clientId, int requestId) {
        auto it = m_lastRequestId.find(clientId);
        return it != m_lastRequestId.end() && it->second >= requestId;
    }
};

#endif  // KV_STATE_MACHINE_H

