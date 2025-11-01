#ifndef ISTATE_MACHINE_H
#define ISTATE_MACHINE_H

#include <string>

/**
 * @brief 状态机接口 - Raft共识层与业务逻辑层的桥梁
 * 
 * Raft负责日志复制和共识，状态机负责执行具体的业务逻辑。
 * 通过这个接口实现解耦：
 * - Raft不需要知道业务逻辑的细节
 * - 业务逻辑不需要知道Raft的实现
 */
class IStateMachine {
public:
    virtual ~IStateMachine() = default;
    
    /**
     * @brief 应用一条已提交的日志条目
     * 
     * 当Raft确认某条日志已被大多数节点复制后，会调用此方法
     * 让状态机执行日志中的命令。
     * 
     * @param command 日志中的命令（序列化后的字符串）
     * @param index 日志索引
     */
    virtual void Apply(const std::string& command, int index) = 0;
    
    /**
     * @brief 获取状态机的快照
     * 
     * 当日志过长需要压缩时，Raft会调用此方法获取状态机的快照。
     * 快照包含了状态机当前的完整状态。
     * 
     * @return 快照数据（序列化后的字符串）
     */
    virtual std::string TakeSnapshot() = 0;
    
    /**
     * @brief 从快照恢复状态机
     * 
     * 当节点需要快速追赶或从崩溃恢复时，会调用此方法
     * 直接从快照恢复状态机，而不是重放所有日志。
     * 
     * @param snapshot 快照数据
     */
    virtual void InstallSnapshot(const std::string& snapshot) = 0;
};

#endif  // ISTATE_MACHINE_H

