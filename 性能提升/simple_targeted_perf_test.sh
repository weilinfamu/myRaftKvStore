#!/bin/bash

################################################################################
# 简化版针对性Perf测试
################################################################################

set -e

PROJECT_ROOT="/home/ric/projects/work/KVstorageBaseRaft-cpp-main"
OUTPUT_DIR="${PROJECT_ROOT}/targeted_perf_results"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
SESSION_DIR="${OUTPUT_DIR}/session_${TIMESTAMP}"

mkdir -p "${SESSION_DIR}"

echo "==============================================================================="
echo "         KV存储RAFT项目 - 简化版针对性分层Perf测试"
echo "==============================================================================="
echo "测试时间: $(date)"
echo "输出目录: ${SESSION_DIR}"
echo "==============================================================================="
echo ""

# 清理环境
echo ">>> 清理环境..."
pkill -f raftCoreRun 2>/dev/null || true
pkill -f kv_raft_performance_test 2>/dev/null || true
rm -rf persisterData*
sleep 2

cd "${PROJECT_ROOT}"

################################################################################
# 测试1: 网络层 Perf 分析
################################################################################

echo ""
echo "==============================================================================="
echo "测试1: 网络层 Perf 分析"
echo "==============================================================================="

TEST1_DIR="${SESSION_DIR}/01_network_layer"
mkdir -p "${TEST1_DIR}"

# 启动RAFT集群
echo ">>> 启动RAFT集群..."
./bin/raftCoreRun 0 test.conf > "${TEST1_DIR}/node0.log" 2>&1 &
NODE0_PID=$!
echo "  节点0 PID: ${NODE0_PID}"
sleep 2

./bin/raftCoreRun 1 test.conf > "${TEST1_DIR}/node1.log" 2>&1 &
NODE1_PID=$!
echo "  节点1 PID: ${NODE1_PID}"

echo "  等待Leader选举..."
sleep 10

# Perf采样
echo ">>> 执行Perf采样 (60秒)..."
echo "  目标: 网络层 (send/recv, RPC, ConnectionPool)"

(perf record -F 99 -g -o "${TEST1_DIR}/network_perf.data" -p ${NODE0_PID} sleep 60) &
PERF_PID=$!

# 产生网络流量
echo "  发送PUT请求产生网络流量..."
./bin/kv_raft_performance_test test.conf 4 500 put > "${TEST1_DIR}/client_output.txt" 2>&1 &
CLIENT_PID=$!

# 等待perf完成
wait ${PERF_PID} 2>/dev/null || true
echo "  ✓ Perf采样完成"

# 生成报告
echo ">>> 生成Perf报告..."
perf report -i "${TEST1_DIR}/network_perf.data" --stdio > "${TEST1_DIR}/network_perf_report.txt" 2>&1

# 生成火焰图
echo ">>> 生成火焰图..."
perf script -i "${TEST1_DIR}/network_perf.data" 2>/dev/null | \
    ./FlameGraph/stackcollapse-perf.pl > "${TEST1_DIR}/network_perf.folded"
./FlameGraph/flamegraph.pl --title "网络层火焰图" \
    "${TEST1_DIR}/network_perf.folded" > "${TEST1_DIR}/network_flamegraph.svg"

# 提取热点
echo ">>> 提取网络层热点函数..."
grep -E "(send|recv|MprpcChannel|ConnectionPool|getConnection|CallMethod)" \
    "${TEST1_DIR}/network_perf_report.txt" | head -30 > "${TEST1_DIR}/network_hotspots.txt" 2>&1 || true

echo "✓ 测试1完成"
echo "  报告: ${TEST1_DIR}/network_perf_report.txt"
echo "  火焰图: ${TEST1_DIR}/network_flamegraph.svg"

################################################################################
# 测试2: 日志层 & I/O 层 Perf 分析
################################################################################

echo ""
echo "==============================================================================="
echo "测试2: 日志层 & I/O 层 Perf 分析"
echo "==============================================================================="

TEST2_DIR="${SESSION_DIR}/02_log_io_layer"
mkdir -p "${TEST2_DIR}"

# Perf采样
echo ">>> 执行Perf采样 (60秒)..."
echo "  目标: 日志层 (Persister, saveRaftState, fsync)"

(perf record -F 99 -g -o "${TEST2_DIR}/log_io_perf.data" -p ${NODE0_PID} sleep 60) &
PERF_PID=$!

# 产生大量日志写入
echo "  发送大量PUT请求产生日志写入..."
./bin/kv_raft_performance_test test.conf 8 1000 put > "${TEST2_DIR}/client_put_output.txt" 2>&1 &
CLIENT_PID=$!

# 等待perf完成
wait ${PERF_PID} 2>/dev/null || true
echo "  ✓ Perf采样完成"

# 生成报告
echo ">>> 生成Perf报告..."
perf report -i "${TEST2_DIR}/log_io_perf.data" --stdio > "${TEST2_DIR}/log_io_perf_report.txt" 2>&1

# 生成火焰图
echo ">>> 生成火焰图..."
perf script -i "${TEST2_DIR}/log_io_perf.data" 2>/dev/null | \
    ./FlameGraph/stackcollapse-perf.pl > "${TEST2_DIR}/log_io_perf.folded"
./FlameGraph/flamegraph.pl --title "日志I/O层火焰图" \
    "${TEST2_DIR}/log_io_perf.folded" > "${TEST2_DIR}/log_io_flamegraph.svg"

# 提取热点
echo ">>> 提取日志I/O层热点函数..."
grep -E "(Persister|saveRaftState|readRaftState|write|fsync|flush)" \
    "${TEST2_DIR}/log_io_perf_report.txt" | head -30 > "${TEST2_DIR}/log_io_hotspots.txt" 2>&1 || true

# 分析Persister数据
echo ">>> 分析Persister数据..."
echo "=== Persister数据目录 ===" > "${TEST2_DIR}/persister_analysis.txt"
ls -lh persisterData* 2>/dev/null >> "${TEST2_DIR}/persister_analysis.txt" || echo "无数据" >> "${TEST2_DIR}/persister_analysis.txt"
du -sh persisterData* 2>/dev/null >> "${TEST2_DIR}/persister_analysis.txt" || true

echo "✓ 测试2完成"
echo "  报告: ${TEST2_DIR}/log_io_perf_report.txt"
echo "  火焰图: ${TEST2_DIR}/log_io_flamegraph.svg"

################################################################################
# 测试3: 共识层 Perf 分析
################################################################################

echo ""
echo "==============================================================================="
echo "测试3: 共识层 Perf 分析"
echo "==============================================================================="

TEST3_DIR="${SESSION_DIR}/03_consensus_layer"
mkdir -p "${TEST3_DIR}"

# 对两个节点同时采样
echo ">>> 执行Perf采样 (60秒)..."
echo "  目标: 共识层 (AppendEntries, RequestVote, applier)"

(perf record -F 99 -g -o "${TEST3_DIR}/consensus_node0_perf.data" -p ${NODE0_PID} sleep 60) &
PERF0_PID=$!

(perf record -F 99 -g -o "${TEST3_DIR}/consensus_node1_perf.data" -p ${NODE1_PID} sleep 60) &
PERF1_PID=$!

# 产生共识层活动
echo "  发送混合请求触发共识协议..."
./bin/kv_raft_performance_test test.conf 4 500 mixed > "${TEST3_DIR}/client_mixed_output.txt" 2>&1 &
CLIENT_PID=$!

# 等待perf完成
wait ${PERF0_PID} 2>/dev/null || true
wait ${PERF1_PID} 2>/dev/null || true
echo "  ✓ Perf采样完成"

# 生成报告 - 节点0
echo ">>> 生成节点0 Perf报告..."
perf report -i "${TEST3_DIR}/consensus_node0_perf.data" --stdio > "${TEST3_DIR}/consensus_node0_report.txt" 2>&1

perf script -i "${TEST3_DIR}/consensus_node0_perf.data" 2>/dev/null | \
    ./FlameGraph/stackcollapse-perf.pl > "${TEST3_DIR}/consensus_node0.folded"
./FlameGraph/flamegraph.pl --title "共识层火焰图-节点0(Leader)" \
    "${TEST3_DIR}/consensus_node0.folded" > "${TEST3_DIR}/consensus_node0_flamegraph.svg"

# 生成报告 - 节点1
echo ">>> 生成节点1 Perf报告..."
perf report -i "${TEST3_DIR}/consensus_node1_perf.data" --stdio > "${TEST3_DIR}/consensus_node1_report.txt" 2>&1

perf script -i "${TEST3_DIR}/consensus_node1_perf.data" 2>/dev/null | \
    ./FlameGraph/stackcollapse-perf.pl > "${TEST3_DIR}/consensus_node1.folded"
./FlameGraph/flamegraph.pl --title "共识层火焰图-节点1(Follower)" \
    "${TEST3_DIR}/consensus_node1.folded" > "${TEST3_DIR}/consensus_node1_flamegraph.svg"

# 提取热点
echo ">>> 提取共识层热点函数..."
{
    echo "=== 节点0 (Leader) 热点 ==="
    grep -E "(AppendEntries|RequestVote|leaderElection|sendAppendEntries|applier|commitIndex)" \
        "${TEST3_DIR}/consensus_node0_report.txt" | head -30
    echo ""
    echo "=== 节点1 (Follower) 热点 ==="
    grep -E "(AppendEntries|RequestVote|leaderElection|sendAppendEntries|applier|commitIndex)" \
        "${TEST3_DIR}/consensus_node1_report.txt" | head -30
} > "${TEST3_DIR}/consensus_hotspots.txt" 2>&1 || true

echo "✓ 测试3完成"
echo "  节点0报告: ${TEST3_DIR}/consensus_node0_report.txt"
echo "  节点0火焰图: ${TEST3_DIR}/consensus_node0_flamegraph.svg"
echo "  节点1报告: ${TEST3_DIR}/consensus_node1_report.txt"
echo "  节点1火焰图: ${TEST3_DIR}/consensus_node1_flamegraph.svg"

################################################################################
# 测试4: 系统调用分析 (strace)
################################################################################

echo ""
echo "==============================================================================="
echo "测试4: 系统调用分析 (I/O刷盘)"
echo "==============================================================================="

TEST4_DIR="${SESSION_DIR}/04_disk_io_dedicated"
mkdir -p "${TEST4_DIR}"

echo ">>> 使用strace监控系统调用 (60秒)..."
timeout 60 strace -c -p ${NODE0_PID} 2> "${TEST4_DIR}/strace_node0.txt" &
STRACE_PID=$!

# 产生大量写入
echo "  发送大量PUT请求触发刷盘..."
./bin/kv_raft_performance_test test.conf 16 2000 put > "${TEST4_DIR}/client_heavy_put.txt" 2>&1 &
CLIENT_PID=$!

# 等待strace完成
wait ${STRACE_PID} 2>/dev/null || true
echo "  ✓ strace采集完成"

# 分析系统调用
echo ">>> 分析系统调用..."
{
    echo "=== 系统调用统计 ==="
    cat "${TEST4_DIR}/strace_node0.txt"
} > "${TEST4_DIR}/syscall_analysis.txt" 2>&1

echo "✓ 测试4完成"
echo "  系统调用统计: ${TEST4_DIR}/syscall_analysis.txt"

################################################################################
# 清理
################################################################################

echo ""
echo "==============================================================================="
echo "清理环境"
echo "==============================================================================="

echo ">>> 停止RAFT集群..."
kill ${NODE0_PID} ${NODE1_PID} 2>/dev/null || true
pkill -f raftCoreRun 2>/dev/null || true
pkill -f kv_raft_performance_test 2>/dev/null || true

echo "✓ 清理完成"

################################################################################
# 生成综合报告
################################################################################

echo ""
echo "==============================================================================="
echo "生成综合报告"
echo "==============================================================================="

FINAL_REPORT="${SESSION_DIR}/TARGETED_PERF_ANALYSIS_REPORT.md"

cat > "${FINAL_REPORT}" << 'EOFMARKER'
# KV存储RAFT项目 - 针对性分层Perf分析报告

## 测试信息

EOFMARKER

cat >> "${FINAL_REPORT}" << EOF
- **测试时间**: $(date)
- **测试会话**: ${TIMESTAMP}
- **输出目录**: ${SESSION_DIR}

---

## 一、网络层 Perf 分析

### 火焰图
\`\`\`
file://${TEST1_DIR}/network_flamegraph.svg
\`\`\`

### 热点函数（Top 30）
\`\`\`
$(cat "${TEST1_DIR}/network_hotspots.txt" 2>/dev/null || echo "未找到数据")
\`\`\`

### Top 20 函数（按CPU占用）
\`\`\`
$(grep -E "^[[:space:]]*[0-9]+\.[0-9]+%" "${TEST1_DIR}/network_perf_report.txt" 2>/dev/null | head -20 || echo "未找到数据")
\`\`\`

---

## 二、日志层 & I/O 层 Perf 分析

### 火焰图
\`\`\`
file://${TEST2_DIR}/log_io_flamegraph.svg
\`\`\`

### 热点函数（Top 30）
\`\`\`
$(cat "${TEST2_DIR}/log_io_hotspots.txt" 2>/dev/null || echo "未找到数据")
\`\`\`

### Top 20 函数（按CPU占用）
\`\`\`
$(grep -E "^[[:space:]]*[0-9]+\.[0-9]+%" "${TEST2_DIR}/log_io_perf_report.txt" 2>/dev/null | head -20 || echo "未找到数据")
\`\`\`

### Persister 数据分析
\`\`\`
$(cat "${TEST2_DIR}/persister_analysis.txt" 2>/dev/null || echo "未找到数据")
\`\`\`

---

## 三、共识层 Perf 分析

### 节点0 (Leader) 火焰图
\`\`\`
file://${TEST3_DIR}/consensus_node0_flamegraph.svg
\`\`\`

### 节点1 (Follower) 火焰图
\`\`\`
file://${TEST3_DIR}/consensus_node1_flamegraph.svg
\`\`\`

### 热点函数对比
\`\`\`
$(cat "${TEST3_DIR}/consensus_hotspots.txt" 2>/dev/null || echo "未找到数据")
\`\`\`

### 节点0 Top 20 函数
\`\`\`
$(grep -E "^[[:space:]]*[0-9]+\.[0-9]+%" "${TEST3_DIR}/consensus_node0_report.txt" 2>/dev/null | head -20 || echo "未找到数据")
\`\`\`

### 节点1 Top 20 函数
\`\`\`
$(grep -E "^[[:space:]]*[0-9]+\.[0-9]+%" "${TEST3_DIR}/consensus_node1_report.txt" 2>/dev/null | head -20 || echo "未找到数据")
\`\`\`

---

## 四、系统调用分析

### 系统调用统计
\`\`\`
$(cat "${TEST4_DIR}/syscall_analysis.txt" 2>/dev/null || echo "未找到数据")
\`\`\`

---

## 五、测试文件清单

\`\`\`
$(find "${SESSION_DIR}" -type f \( -name "*.txt" -o -name "*.svg" -o -name "*.data" \) | sort)
\`\`\`

---

## 报告结束

**完整结果目录**: ${SESSION_DIR}

查看火焰图:
- firefox ${TEST1_DIR}/network_flamegraph.svg
- firefox ${TEST2_DIR}/log_io_flamegraph.svg
- firefox ${TEST3_DIR}/consensus_node0_flamegraph.svg
- firefox ${TEST3_DIR}/consensus_node1_flamegraph.svg
EOF

echo "✓ 综合报告已生成: ${FINAL_REPORT}"

################################################################################
# 总结
################################################################################

echo ""
echo "==============================================================================="
echo "                    测试完成！"
echo "==============================================================================="
echo ""
echo "📊 测试结果目录: ${SESSION_DIR}"
echo ""
echo "📄 主要报告:"
echo "  综合报告:    ${FINAL_REPORT}"
echo "  网络层:      ${TEST1_DIR}/network_flamegraph.svg"
echo "  日志I/O层:   ${TEST2_DIR}/log_io_flamegraph.svg"
echo "  共识层-节点0: ${TEST3_DIR}/consensus_node0_flamegraph.svg"
echo "  共识层-节点1: ${TEST3_DIR}/consensus_node1_flamegraph.svg"
echo ""
echo "🔍 查看报告:"
echo "  less ${FINAL_REPORT}"
echo ""
echo "==============================================================================="

