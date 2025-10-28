# KV存储RAFT项目 - 针对性分层Perf测试说明

## 📋 测试概述

本测试脚本 (`targeted_perf_test.sh`) 针对RAFT项目的**4个关键层**进行深度性能分析：

### ✅ 测试1: 网络层 Perf 分析 (60秒)
**目标函数:**
- `send()` / `recv()` - 协程Hook后的网络I/O
- `MprpcChannel::CallMethod()` - RPC调用
- `ConnectionPool::getConnection()` - 连接池获取
- `MprpcChannel::sendRpcRequest()` - 请求发送

**测试方法:**
- 启动2节点RAFT集群
- 用 `perf record -F 999 -g` 采样 Leader 节点
- 发送大量 PUT 请求产生网络流量
- 生成网络层火焰图

**输出:**
- `01_network_layer/network_flamegraph.svg` - 网络层火焰图
- `01_network_layer/network_perf_report.txt` - 详细Perf报告
- `01_network_layer/network_hotspots.txt` - 热点函数Top 30

---

### ✅ 测试2: 日志层 & I/O 层 Perf 分析 (60秒)
**目标函数:**
- `Persister::saveRaftState()` - 保存RAFT状态
- `Persister::readRaftState()` - 读取RAFT状态
- `write()` / `fsync()` - 系统调用
- `std::ofstream::write()` - 文件写入

**测试方法:**
- 用 `perf record` 采样日志写入
- 同时用 `iostat -x 5 12` 监控磁盘I/O
- 发送大量 PUT 请求触发日志刷盘
- 分析实际的 Persister 数据目录大小

**输出:**
- `02_log_io_layer/log_io_flamegraph.svg` - 日志I/O火焰图
- `02_log_io_layer/log_io_perf_report.txt` - 详细Perf报告
- `02_log_io_layer/iostat_during_test.txt` - iostat统计
- `02_log_io_layer/persister_analysis.txt` - Persister数据分析

---

### ✅ 测试3: 共识层 Perf 分析 (60秒)
**目标函数:**
- `Raft::AppendEntries1()` - 处理日志复制
- `Raft::RequestVote()` - 处理投票请求
- `Raft::leaderElectionTicker()` - Leader选举
- `Raft::sendAppendEntries()` - 发送日志复制
- `Raft::applier()` - 应用状态机

**测试方法:**
- 对**所有节点**同时采样（Leader和Follower）
- 发送混合请求触发共识协议
- 分析Leader和Follower的热点差异

**输出:**
- `03_consensus_layer/consensus_node0_flamegraph.svg` - 节点0火焰图（Leader）
- `03_consensus_layer/consensus_node1_flamegraph.svg` - 节点1火焰图（Follower）
- `03_consensus_layer/consensus_node0_report.txt` - 节点0详细报告
- `03_consensus_layer/consensus_node1_report.txt` - 节点1详细报告
- `03_consensus_layer/consensus_hotspots.txt` - 共识层热点函数

---

### ✅ 测试4: 专项 I/O 刷盘性能测试 (60秒)
**目标:**
- 统计 `write()` 系统调用次数
- 统计 `fsync()` / `fdatasync()` 调用次数
- 分析实际刷盘开销
- 监控进程级别的I/O

**测试方法:**
- 用 `strace -c` 统计系统调用
- 用 `iotop` 监控实时I/O（如果可用）
- 发送大量 PUT 请求触发刷盘
- 对比测试前后磁盘使用量

**输出:**
- `04_disk_io_dedicated/strace_node0.txt` - 系统调用统计
- `04_disk_io_dedicated/iotop_node0.txt` - iotop监控（如需要）
- `04_disk_io_dedicated/syscall_analysis.txt` - 系统调用分析
- `04_disk_io_dedicated/disk_before.txt` - 测试前磁盘
- `04_disk_io_dedicated/disk_after.txt` - 测试后磁盘

---

## 🔧 运行前准备

### 1. 安装必要工具

```bash
# iostat (推荐，用于I/O监控)
sudo apt install -y sysstat

# perf 和 strace 已安装 ✓
```

### 2. 确保没有残留进程

```bash
# 杀掉可能残留的RAFT进程
pkill -f raftCoreRun
pkill -f kv_raft_performance_test

# 清理旧的Persister数据
cd /home/ric/projects/work/KVstorageBaseRaft-cpp-main
rm -rf persisterData*
```

### 3. 检查端口占用

```bash
# 确保测试端口没有被占用
netstat -tuln | grep -E "21920|21921"
```

---

## 🚀 运行测试

### 方式1: 直接运行（推荐）

```bash
cd /home/ric/projects/work/KVstorageBaseRaft-cpp-main
./targeted_perf_test.sh
```

### 方式2: 后台运行（如果时间太长）

```bash
cd /home/ric/projects/work/KVstorageBaseRaft-cpp-main
nohup ./targeted_perf_test.sh > targeted_perf_test.log 2>&1 &

# 查看进度
tail -f targeted_perf_test.log
```

---

## ⏱️ 预计时间

- **总时间**: 约 **5-8分钟**
  - 测试1（网络层）: 60秒
  - 测试2（日志I/O层）: 60秒
  - 测试3（共识层）: 60秒
  - 测试4（I/O刷盘）: 60秒
  - 启动/清理: 约1-2分钟

---

## 📊 测试结果

测试完成后，所有结果保存在:

```
targeted_perf_results/session_YYYYMMDD_HHMMSS/
├── 01_network_layer/
│   ├── network_flamegraph.svg          ⭐ 网络层火焰图
│   ├── network_perf_report.txt         ⭐ 详细Perf报告
│   ├── network_hotspots.txt            ⭐ 热点函数
│   ├── node0.log
│   └── node1.log
├── 02_log_io_layer/
│   ├── log_io_flamegraph.svg           ⭐ 日志I/O火焰图
│   ├── log_io_perf_report.txt          ⭐ 详细Perf报告
│   ├── iostat_during_test.txt          ⭐ I/O统计
│   └── persister_analysis.txt          ⭐ Persister分析
├── 03_consensus_layer/
│   ├── consensus_node0_flamegraph.svg  ⭐ Leader火焰图
│   ├── consensus_node1_flamegraph.svg  ⭐ Follower火焰图
│   ├── consensus_node0_report.txt      ⭐ Leader详细报告
│   ├── consensus_node1_report.txt      ⭐ Follower详细报告
│   └── consensus_hotspots.txt          ⭐ 共识层热点
├── 04_disk_io_dedicated/
│   ├── strace_node0.txt                ⭐ 系统调用统计
│   ├── syscall_analysis.txt            ⭐ 系统调用分析
│   ├── disk_before.txt
│   └── disk_after.txt
└── TARGETED_PERF_ANALYSIS_REPORT.md    ⭐⭐⭐ 综合分析报告
```

---

## 📖 查看报告

### 查看综合报告

```bash
cd /home/ric/projects/work/KVstorageBaseRaft-cpp-main
less targeted_perf_results/session_*/TARGETED_PERF_ANALYSIS_REPORT.md
```

### 查看火焰图（在浏览器中）

```bash
# 网络层
firefox targeted_perf_results/session_*/01_network_layer/network_flamegraph.svg

# 日志I/O层
firefox targeted_perf_results/session_*/02_log_io_layer/log_io_flamegraph.svg

# 共识层
firefox targeted_perf_results/session_*/03_consensus_layer/consensus_node0_flamegraph.svg
```

### 查看热点函数

```bash
# 网络层热点
cat targeted_perf_results/session_*/01_network_layer/network_hotspots.txt

# 日志I/O层热点
cat targeted_perf_results/session_*/02_log_io_layer/log_io_hotspots.txt

# 共识层热点
cat targeted_perf_results/session_*/03_consensus_layer/consensus_hotspots.txt
```

### 查看系统调用统计

```bash
cat targeted_perf_results/session_*/04_disk_io_dedicated/syscall_analysis.txt
```

---

## 🎯 关键分析点

### 1. 网络层分析

**期望看到的函数:**
- `MprpcChannel::CallMethod` - RPC调用入口
- `MprpcChannel::sendRpcRequest` - 发送请求
- `ConnectionPool::getConnection` - 获取连接
- `send` / `recv` - 网络I/O（Hook后）

**关键指标:**
- send/recv 占比 < 5%（Hook后应该很低）
- ConnectionPool 锁竞争占比
- RPC序列化/反序列化占比

### 2. 日志I/O层分析

**期望看到的函数:**
- `Persister::saveRaftState` - 保存状态
- `write` / `fsync` - 系统调用
- `std::ofstream::write` - 文件写入

**关键指标:**
- fsync 调用次数（应该适中，不要过高）
- 每次刷盘平均大小
- I/O等待时间占比

### 3. 共识层分析

**期望看到的函数:**
- `Raft::AppendEntries1` - 日志复制
- `Raft::sendAppendEntries` - 发送日志
- `Raft::applier` - 应用状态机
- `Raft::leaderHearBeatTicker` - 心跳

**关键指标:**
- Leader vs Follower 热点差异
- 锁竞争占比（m_mtx）
- 心跳频率是否合理

### 4. I/O刷盘分析

**期望看到的系统调用:**
- `write` - 写入次数
- `fsync` / `fdatasync` - 刷盘次数
- `lseek` / `read` - 读取次数

**关键指标:**
- fsync 每秒调用次数
- 平均每次 fsync 耗时
- write vs fsync 比例

---

## ❓ 常见问题

### Q1: perf 权限不足怎么办？

```bash
# 临时允许（需要root）
sudo sysctl -w kernel.perf_event_paranoid=-1

# 或者使用 sudo 运行脚本
sudo ./targeted_perf_test.sh
```

### Q2: 端口被占用怎么办？

```bash
# 查找占用进程
lsof -i:21920
lsof -i:21921

# 杀掉进程
kill -9 <PID>
```

### Q3: 如果 iostat 没有安装？

脚本会自动跳过 iostat 部分，不影响其他测试。

### Q4: 火焰图为空怎么办？

可能原因:
1. perf 采样时间太短（已设置60秒）
2. 程序没有实际运行
3. 权限不足

解决:
- 检查 node0.log 和 node1.log 确认集群启动成功
- 使用 `sudo` 运行脚本

---

## 🔍 与之前测试的对比

| 方面 | 之前的测试 | 本次测试 |
|------|----------|---------|
| **Perf目标** | simple_stress_test（纯CPU） | 真实RAFT集群 |
| **网络层** | ❌ 未测试 | ✅ 专项测试 + 火焰图 |
| **日志层** | ❌ 未测试 | ✅ 专项测试 + iostat |
| **共识层** | ❌ 未测试 | ✅ 专项测试 + 多节点 |
| **I/O刷盘** | 只测试了dd | ✅ strace + iotop + 实际业务 |
| **火焰图** | 空的 | ✅ 4个针对性火焰图 |
| **热点函数** | 无 | ✅ 每层Top 30热点 |

---

## 📝 测试后分析建议

### 1. 网络层优化

查看 `network_hotspots.txt`，如果发现:
- `send/recv` 占比 > 10% → Hook 可能未生效
- `ConnectionPool::getConnection` 占比 > 5% → 锁竞争严重，需要优化
- `protobuf::SerializeToString` 占比 > 10% → 序列化开销大

### 2. 日志I/O层优化

查看 `log_io_hotspots.txt` 和 `iostat_during_test.txt`，如果发现:
- `fsync` 每秒调用 > 1000次 → 刷盘过于频繁
- I/O await > 10ms → 磁盘性能瓶颈
- Persister 数据增长速度异常 → 日志压缩问题

### 3. 共识层优化

查看 `consensus_hotspots.txt`，如果发现:
- `m_mtx` 锁占比 > 20% → 锁粒度过大
- `leaderHearBeatTicker` 占比 > 15% → 心跳过于频繁
- `applier` 占比 < 5% → 应用状态机太慢

### 4. I/O刷盘优化

查看 `syscall_analysis.txt`，如果发现:
- `fsync` 占总时间 > 30% → 考虑批量刷盘
- `write` 调用次数过多 → 使用缓冲写入
- `fsync` vs `write` 比例 > 1:10 → 每次写入都刷盘（不合理）

---

## ✅ 准备好了吗？

如果准备好运行测试，执行:

```bash
cd /home/ric/projects/work/KVstorageBaseRaft-cpp-main
./targeted_perf_test.sh
```

测试时间约 **5-8分钟**，请耐心等待！

---

**测试结束后，请查看综合报告:**
```bash
less targeted_perf_results/session_*/TARGETED_PERF_ANALYSIS_REPORT.md
```

