#ifndef KV_RPC_CLIENT_ADAPTER_H
#define KV_RPC_CLIENT_ADAPTER_H

#include <memory>
#include "IKvRpcClient.h"
#include "raftServerRpcUtil.h"

/**
 * @brief KV RPC客户端适配器
 * 
 * 将现有的raftServerRpcUtil类包装为IKvRpcClient接口
 */
class KvRpcClientAdapter : public IKvRpcClient {
private:
    std::shared_ptr<raftServerRpcUtil> m_rpcUtil;
    
public:
    explicit KvRpcClientAdapter(std::shared_ptr<raftServerRpcUtil> rpcUtil)
        : m_rpcUtil(std::move(rpcUtil)) {}
    
    bool Get(
        const raftKVRpcProctoc::GetArgs& args,
        raftKVRpcProctoc::GetReply* reply
    ) override {
        raftKVRpcProctoc::GetArgs* argsPtr = 
            const_cast<raftKVRpcProctoc::GetArgs*>(&args);
        return m_rpcUtil->Get(argsPtr, reply);
    }
    
    bool PutAppend(
        const raftKVRpcProctoc::PutAppendArgs& args,
        raftKVRpcProctoc::PutAppendReply* reply
    ) override {
        raftKVRpcProctoc::PutAppendArgs* argsPtr = 
            const_cast<raftKVRpcProctoc::PutAppendArgs*>(&args);
        return m_rpcUtil->PutAppend(argsPtr, reply);
    }
};

#endif  // KV_RPC_CLIENT_ADAPTER_H

