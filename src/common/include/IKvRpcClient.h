#ifndef IKV_RPC_CLIENT_H
#define IKV_RPC_CLIENT_H

#include "kvServerRPC.pb.h"

/**
 * @brief KV服务RPC客户端接口
 * 
 * 通过这个接口，客户端（Clerk）可以与KV服务器通信，
 * 而不需要依赖具体的RPC实现。
 */
class IKvRpcClient {
public:
    virtual ~IKvRpcClient() = default;
    
    /**
     * @brief 发送Get请求
     * @param args 请求参数
     * @param reply 响应结果
     * @return true表示RPC成功，false表示RPC失败
     */
    virtual bool Get(
        const raftKVRpcProctoc::GetArgs& args,
        raftKVRpcProctoc::GetReply* reply
    ) = 0;
    
    /**
     * @brief 发送PutAppend请求
     * @param args 请求参数
     * @param reply 响应结果
     * @return true表示RPC成功，false表示RPC失败
     */
    virtual bool PutAppend(
        const raftKVRpcProctoc::PutAppendArgs& args,
        raftKVRpcProctoc::PutAppendReply* reply
    ) = 0;
};

#endif  // IKV_RPC_CLIENT_H

