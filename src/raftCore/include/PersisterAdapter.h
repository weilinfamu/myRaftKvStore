#ifndef PERSISTER_ADAPTER_H
#define PERSISTER_ADAPTER_H

#include <memory>
#include "IPersistenceLayer.h"
#include "Persister.h"

/**
 * @brief Persister适配器
 * 
 * 将现有的Persister类包装为IPersistenceLayer接口，
 * 实现无缝迁移。
 */
class PersisterAdapter : public IPersistenceLayer {
private:
    std::shared_ptr<Persister> m_persister;
    
public:
    explicit PersisterAdapter(std::shared_ptr<Persister> persister)
        : m_persister(std::move(persister)) {}
    
    void SaveRaftState(const std::string& data) override {
        m_persister->SaveRaftState(data);
    }
    
    std::string ReadRaftState() override {
        return m_persister->ReadRaftState();
    }
    
    long long RaftStateSize() override {
        return m_persister->RaftStateSize();
    }
    
    void SaveSnapshot(const std::string& snapshot) override {
        // Persister的Save方法需要同时传入raftState和snapshot
        // 这里我们只更新snapshot，保持raftState不变
        std::string currentRaftState = m_persister->ReadRaftState();
        m_persister->Save(currentRaftState, snapshot);
    }
    
    std::string ReadSnapshot() override {
        return m_persister->ReadSnapshot();
    }
    
    void Save(const std::string& raftState, const std::string& snapshot) override {
        m_persister->Save(raftState, snapshot);
    }
    
    void Flush() override {
        m_persister->Flush();
    }
};

#endif  // PERSISTER_ADAPTER_H

