#ifndef RAFT_RPC_ADAPTER_H
#define RAFT_RPC_ADAPTER_H

#include <memory>
#include "IRaftRpcChannel.h"
#include "raftRpcUtil.h"

/**
 * @brief RaftRpc适配器
 * 
 * 将现有的RaftRpcUtil类包装为IRaftRpcChannel接口
 */
class RaftRpcAdapter : public IRaftRpcChannel {
private:
    std::shared_ptr<RaftRpcUtil> m_rpcUtil;
    
public:
    explicit RaftRpcAdapter(std::shared_ptr<RaftRpcUtil> rpcUtil)
        : m_rpcUtil(std::move(rpcUtil)) {}
    
    bool SendRequestVote(
        const raftRpcProctoc::RequestVoteArgs& args,
        raftRpcProctoc::RequestVoteReply* reply
    ) override {
        // RaftRpcUtil的方法需要指针参数
        raftRpcProctoc::RequestVoteArgs* argsPtr = 
            const_cast<raftRpcProctoc::RequestVoteArgs*>(&args);
        return m_rpcUtil->RequestVote(argsPtr, reply);
    }
    
    bool SendAppendEntries(
        const raftRpcProctoc::AppendEntriesArgs& args,
        raftRpcProctoc::AppendEntriesReply* reply
    ) override {
        raftRpcProctoc::AppendEntriesArgs* argsPtr = 
            const_cast<raftRpcProctoc::AppendEntriesArgs*>(&args);
        return m_rpcUtil->AppendEntries(argsPtr, reply);
    }
    
    bool SendInstallSnapshot(
        const raftRpcProctoc::InstallSnapshotRequest& args,
        raftRpcProctoc::InstallSnapshotResponse* reply
    ) override {
        raftRpcProctoc::InstallSnapshotRequest* argsPtr = 
            const_cast<raftRpcProctoc::InstallSnapshotRequest*>(&args);
        return m_rpcUtil->InstallSnapshot(argsPtr, reply);
    }
};

#endif  // RAFT_RPC_ADAPTER_H

