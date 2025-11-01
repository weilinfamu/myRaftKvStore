#ifndef IRAFT_RPC_CHANNEL_H
#define IRAFT_RPC_CHANNEL_H

#include "raftRPC.pb.h"

/**
 * @brief Raft RPC通信接口 - 抽象Raft节点间的RPC通信
 * 
 * 通过这个接口，可以轻松替换RPC框架：
 * - 自定义RPC框架
 * - gRPC
 * - Thrift
 * - 本地Mock（测试用）
 */
class IRaftRpcChannel {
public:
    virtual ~IRaftRpcChannel() = default;
    
    /**
     * @brief 发送RequestVote RPC请求
     * @param args 请求参数
     * @param reply 响应结果
     * @return true表示RPC成功，false表示RPC失败（网络错误等）
     */
    virtual bool SendRequestVote(
        const raftRpcProctoc::RequestVoteArgs& args,
        raftRpcProctoc::RequestVoteReply* reply
    ) = 0;
    
    /**
     * @brief 发送AppendEntries RPC请求
     * @param args 请求参数
     * @param reply 响应结果
     * @return true表示RPC成功，false表示RPC失败
     */
    virtual bool SendAppendEntries(
        const raftRpcProctoc::AppendEntriesArgs& args,
        raftRpcProctoc::AppendEntriesReply* reply
    ) = 0;
    
    /**
     * @brief 发送InstallSnapshot RPC请求
     * @param args 请求参数
     * @param reply 响应结果
     * @return true表示RPC成功，false表示RPC失败
     */
    virtual bool SendInstallSnapshot(
        const raftRpcProctoc::InstallSnapshotRequest& args,
        raftRpcProctoc::InstallSnapshotResponse* reply
    ) = 0;
};

#endif  // IRAFT_RPC_CHANNEL_H

