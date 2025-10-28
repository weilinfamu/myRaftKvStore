#!/bin/bash

################################################################################
# KV存储RAFT项目 - 针对性能分层测试脚本
#
# 测试目标:
#   1. 网络层 (RPC send/recv, ConnectionPool)
#   2. 日志层 (日志提交/提取, Persister 刷盘)
#   3. 共识层 (AppendEntries, RequestVote, Leader选举)
#   4. I/O 层 (日志刷盘, Snapshot持久化)
#
################################################################################

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
PURPLE='\033[0;35m'
NC='\033[0m'

# 配置参数
PROJECT_ROOT="/home/ric/projects/work/KVstorageBaseRaft-cpp-main"
OUTPUT_DIR="${PROJECT_ROOT}/targeted_perf_results"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
SESSION_DIR="${OUTPUT_DIR}/session_${TIMESTAMP}"

# 创建输出目录
mkdir -p "${SESSION_DIR}"

echo -e "${BLUE}===============================================================================${NC}"
echo -e "${BLUE}         KV存储RAFT项目 - 针对性分层Perf测试${NC}"
echo -e "${BLUE}===============================================================================${NC}"
echo -e "测试时间: ${GREEN}$(date)${NC}"
echo -e "项目路径: ${GREEN}${PROJECT_ROOT}${NC}"
echo -e "输出目录: ${GREEN}${SESSION_DIR}${NC}"
echo -e "${BLUE}===============================================================================${NC}"
echo ""

# 检查工具
echo -e "${CYAN}>>> 检查必要工具...${NC}"

check_tool() {
    if ! command -v $1 &> /dev/null; then
        echo -e "${RED}错误: $1 未安装${NC}"
        echo -e "安装: sudo apt install $2"
        exit 1
    else
        echo -e "  ✓ $1 可用"
    fi
}

check_tool_optional() {
    if ! command -v $1 &> /dev/null; then
        echo -e "${YELLOW}警告: $1 未安装，将跳过相关测试${NC}"
        echo -e "安装: sudo apt install $2"
        return 1
    else
        echo -e "  ✓ $1 可用"
        return 0
    fi
}

check_tool "perf" "linux-tools-common linux-tools-generic"

if check_tool_optional "iostat" "sysstat"; then
    IOSTAT_AVAILABLE=true
else
    IOSTAT_AVAILABLE=false
fi

# flamegraph.pl 检查（在FlameGraph目录中）
if [[ -f "${PROJECT_ROOT}/FlameGraph/flamegraph.pl" ]]; then
    echo -e "  ✓ flamegraph.pl 可用"
else
    echo -e "${RED}错误: FlameGraph 未安装${NC}"
    exit 1
fi

# 检查可执行文件
echo -e "\n${CYAN}>>> 检查可执行文件...${NC}"

RAFT_SERVER="${PROJECT_ROOT}/bin/raftCoreRun"
KV_PERF_TEST="${PROJECT_ROOT}/bin/kv_raft_performance_test"

if [[ ! -f "${RAFT_SERVER}" ]]; then
    echo -e "${RED}错误: ${RAFT_SERVER} 不存在${NC}"
    echo "请先编译项目: cd build && make"
    exit 1
fi
echo -e "  ✓ raftCoreRun 可用"

if [[ ! -f "${KV_PERF_TEST}" ]]; then
    echo -e "${YELLOW}警告: ${KV_PERF_TEST} 不存在，将跳过KV性能测试${NC}"
    KV_TEST_AVAILABLE=false
else
    echo -e "  ✓ kv_raft_performance_test 可用"
    KV_TEST_AVAILABLE=true
fi

################################################################################
# 测试1: 网络层 Perf 分析
################################################################################

echo -e "\n${BLUE}===============================================================================${NC}"
echo -e "${PURPLE}测试1: 网络层 Perf 分析 (RPC send/recv, ConnectionPool)${NC}"
echo -e "${BLUE}===============================================================================${NC}"

TEST1_DIR="${SESSION_DIR}/01_network_layer"
mkdir -p "${TEST1_DIR}"

echo -e "${CYAN}>>> 启动RAFT集群...${NC}"

# 启动3个节点
cd "${PROJECT_ROOT}"
CONFIG_FILE="test.conf"

# 清理旧数据
rm -rf persisterData*

# 启动节点1
echo -e "  启动节点 0..."
${RAFT_SERVER} 0 ${CONFIG_FILE} > "${TEST1_DIR}/node0.log" 2>&1 &
NODE0_PID=$!
sleep 2

# 启动节点2
echo -e "  启动节点 1..."
${RAFT_SERVER} 1 ${CONFIG_FILE} > "${TEST1_DIR}/node1.log" 2>&1 &
NODE1_PID=$!
sleep 2

echo -e "${GREEN}  ✓ RAFT集群已启动 (PIDs: ${NODE0_PID}, ${NODE1_PID})${NC}"
echo -e "  等待Leader选举..."
sleep 10

# 对Leader进行网络层Perf分析
echo -e "${CYAN}>>> 执行网络层Perf分析 (采样60秒)...${NC}"
echo -e "  目标: send/recv, ConnectionPool, RPC调用"

# 选择节点0进行采样（通常会成为Leader）
perf record -F 999 -g -o "${TEST1_DIR}/network_perf.data" -p ${NODE0_PID} sleep 60 &
PERF1_PID=$!

# 在后台产生网络流量（如果有客户端）
if [[ "${KV_TEST_AVAILABLE}" == "true" ]]; then
    echo -e "  发送测试请求以产生网络流量..."
    ${KV_PERF_TEST} ${CONFIG_FILE} 4 500 put > "${TEST1_DIR}/client_output.txt" 2>&1 &
    CLIENT_PID=$!
fi

# 等待perf完成
wait ${PERF1_PID} 2>/dev/null || true

echo -e "${CYAN}>>> 生成网络层Perf报告...${NC}"

# 生成文本报告
perf report -i "${TEST1_DIR}/network_perf.data" --stdio > "${TEST1_DIR}/network_perf_report.txt" 2>&1

# 生成火焰图
perf script -i "${TEST1_DIR}/network_perf.data" | \
    ${PROJECT_ROOT}/FlameGraph/stackcollapse-perf.pl > "${TEST1_DIR}/network_perf.folded"
${PROJECT_ROOT}/FlameGraph/flamegraph.pl \
    --title "网络层Perf火焰图 - RPC & ConnectionPool" \
    "${TEST1_DIR}/network_perf.folded" > "${TEST1_DIR}/network_flamegraph.svg"

echo -e "${GREEN}  ✓ 网络层分析完成${NC}"
echo -e "    报告: ${TEST1_DIR}/network_perf_report.txt"
echo -e "    火焰图: ${TEST1_DIR}/network_flamegraph.svg"

# 提取关键网络函数
echo -e "\n${CYAN}>>> 提取网络层热点函数...${NC}"
grep -E "(send|recv|MprpcChannel|ConnectionPool|getConnection|CallMethod)" \
    "${TEST1_DIR}/network_perf_report.txt" | head -30 | tee "${TEST1_DIR}/network_hotspots.txt"

################################################################################
# 测试2: 日志层 & I/O 层 Perf 分析
################################################################################

echo -e "\n${BLUE}===============================================================================${NC}"
echo -e "${PURPLE}测试2: 日志层 & I/O 层 Perf 分析 (日志提交/提取, Persister 刷盘)${NC}"
echo -e "${BLUE}===============================================================================${NC}"

TEST2_DIR="${SESSION_DIR}/02_log_io_layer"
mkdir -p "${TEST2_DIR}"

echo -e "${CYAN}>>> 执行日志层 & I/O 层 Perf 分析 (采样60秒)...${NC}"
echo -e "  目标: Persister, saveRaftState, readRaftState, 刷盘操作"

# 对Leader进行采样
perf record -F 999 -g -o "${TEST2_DIR}/log_io_perf.data" -p ${NODE0_PID} sleep 60 &
PERF2_PID=$!

# 同时监控I/O统计（如果可用）
if [[ "${IOSTAT_AVAILABLE}" == "true" ]]; then
    echo -e "${CYAN}>>> 监控I/O统计 (iostat)...${NC}"
    iostat -x 5 12 > "${TEST2_DIR}/iostat_during_test.txt" 2>&1 &
    IOSTAT_PID=$!
else
    echo -e "${YELLOW}>>> 跳过 iostat 监控（未安装）${NC}"
fi

# 产生大量日志写入
if [[ "${KV_TEST_AVAILABLE}" == "true" ]]; then
    echo -e "  发送大量PUT请求以产生日志写入..."
    ${KV_PERF_TEST} ${CONFIG_FILE} 8 1000 put > "${TEST2_DIR}/client_put_output.txt" 2>&1 &
    CLIENT2_PID=$!
fi

# 等待perf完成
wait ${PERF2_PID} 2>/dev/null || true
if [[ "${IOSTAT_AVAILABLE}" == "true" ]]; then
    wait ${IOSTAT_PID} 2>/dev/null || true
fi

echo -e "${CYAN}>>> 生成日志层 & I/O 层 Perf报告...${NC}"

# 生成文本报告
perf report -i "${TEST2_DIR}/log_io_perf.data" --stdio > "${TEST2_DIR}/log_io_perf_report.txt" 2>&1

# 生成火焰图
perf script -i "${TEST2_DIR}/log_io_perf.data" | \
    ${PROJECT_ROOT}/FlameGraph/stackcollapse-perf.pl > "${TEST2_DIR}/log_io_perf.folded"
${PROJECT_ROOT}/FlameGraph/flamegraph.pl \
    --title "日志层&I/O层Perf火焰图 - Persister & 刷盘" \
    "${TEST2_DIR}/log_io_perf.folded" > "${TEST2_DIR}/log_io_flamegraph.svg"

echo -e "${GREEN}  ✓ 日志层 & I/O 层分析完成${NC}"
echo -e "    报告: ${TEST2_DIR}/log_io_perf_report.txt"
echo -e "    火焰图: ${TEST2_DIR}/log_io_flamegraph.svg"
echo -e "    iostat: ${TEST2_DIR}/iostat_during_test.txt"

# 提取关键I/O函数
echo -e "\n${CYAN}>>> 提取日志层 & I/O 层热点函数...${NC}"
grep -E "(Persister|saveRaftState|readRaftState|readSnapshot|write|fsync|flush)" \
    "${TEST2_DIR}/log_io_perf_report.txt" | head -30 | tee "${TEST2_DIR}/log_io_hotspots.txt"

# 分析实际刷盘数据
echo -e "\n${CYAN}>>> 分析实际刷盘数据...${NC}"
echo "=== Persister数据目录 ===" | tee "${TEST2_DIR}/persister_analysis.txt"
ls -lh persisterData* 2>/dev/null | tee -a "${TEST2_DIR}/persister_analysis.txt"
du -sh persisterData* 2>/dev/null | tee -a "${TEST2_DIR}/persister_analysis.txt"

################################################################################
# 测试3: 共识层 Perf 分析
################################################################################

echo -e "\n${BLUE}===============================================================================${NC}"
echo -e "${PURPLE}测试3: 共识层 Perf 分析 (AppendEntries, RequestVote, Leader选举)${NC}"
echo -e "${BLUE}===============================================================================${NC}"

TEST3_DIR="${SESSION_DIR}/03_consensus_layer"
mkdir -p "${TEST3_DIR}"

echo -e "${CYAN}>>> 执行共识层 Perf 分析 (采样60秒)...${NC}"
echo -e "  目标: AppendEntries, RequestVote, leaderElection, applier"

# 对所有节点进行采样
echo -e "  采样节点0 (Leader候选)..."
perf record -F 999 -g -o "${TEST3_DIR}/consensus_node0_perf.data" -p ${NODE0_PID} sleep 60 &
PERF3_0_PID=$!

echo -e "  采样节点1..."
perf record -F 999 -g -o "${TEST3_DIR}/consensus_node1_perf.data" -p ${NODE1_PID} sleep 60 &
PERF3_1_PID=$!

# 产生共识层活动
if [[ "${KV_TEST_AVAILABLE}" == "true" ]]; then
    echo -e "  发送混合请求以触发共识协议..."
    ${KV_PERF_TEST} ${CONFIG_FILE} 4 500 mixed > "${TEST3_DIR}/client_mixed_output.txt" 2>&1 &
    CLIENT3_PID=$!
fi

# 等待perf完成
wait ${PERF3_0_PID} 2>/dev/null || true
wait ${PERF3_1_PID} 2>/dev/null || true

echo -e "${CYAN}>>> 生成共识层 Perf报告...${NC}"

# 分析节点0
perf report -i "${TEST3_DIR}/consensus_node0_perf.data" --stdio > "${TEST3_DIR}/consensus_node0_report.txt" 2>&1
perf script -i "${TEST3_DIR}/consensus_node0_perf.data" | \
    ${PROJECT_ROOT}/FlameGraph/stackcollapse-perf.pl > "${TEST3_DIR}/consensus_node0.folded"
${PROJECT_ROOT}/FlameGraph/flamegraph.pl \
    --title "共识层Perf火焰图 - 节点0" \
    "${TEST3_DIR}/consensus_node0.folded" > "${TEST3_DIR}/consensus_node0_flamegraph.svg"

# 分析节点1
perf report -i "${TEST3_DIR}/consensus_node1_perf.data" --stdio > "${TEST3_DIR}/consensus_node1_report.txt" 2>&1
perf script -i "${TEST3_DIR}/consensus_node1_perf.data" | \
    ${PROJECT_ROOT}/FlameGraph/stackcollapse-perf.pl > "${TEST3_DIR}/consensus_node1.folded"
${PROJECT_ROOT}/FlameGraph/flamegraph.pl \
    --title "共识层Perf火焰图 - 节点1" \
    "${TEST3_DIR}/consensus_node1.folded" > "${TEST3_DIR}/consensus_node1_flamegraph.svg"

echo -e "${GREEN}  ✓ 共识层分析完成${NC}"
echo -e "    节点0报告: ${TEST3_DIR}/consensus_node0_report.txt"
echo -e "    节点0火焰图: ${TEST3_DIR}/consensus_node0_flamegraph.svg"
echo -e "    节点1报告: ${TEST3_DIR}/consensus_node1_report.txt"
echo -e "    节点1火焰图: ${TEST3_DIR}/consensus_node1_flamegraph.svg"

# 提取共识层热点函数
echo -e "\n${CYAN}>>> 提取共识层热点函数...${NC}"
echo "=== 节点0 热点 ===" | tee "${TEST3_DIR}/consensus_hotspots.txt"
grep -E "(AppendEntries|RequestVote|leaderElection|sendAppendEntries|applier|commitIndex)" \
    "${TEST3_DIR}/consensus_node0_report.txt" | head -30 | tee -a "${TEST3_DIR}/consensus_hotspots.txt"

################################################################################
# 测试4: 专项 I/O 刷盘性能测试
################################################################################

echo -e "\n${BLUE}===============================================================================${NC}"
echo -e "${PURPLE}测试4: 专项 I/O 刷盘性能测试 (Persister write/fsync)${NC}"
echo -e "${BLUE}===============================================================================${NC}"

TEST4_DIR="${SESSION_DIR}/04_disk_io_dedicated"
mkdir -p "${TEST4_DIR}"

echo -e "${CYAN}>>> 监控 Persister 刷盘性能...${NC}"

# 使用 strace 监控系统调用
echo -e "  使用 strace 监控节点0的系统调用 (60秒)..."
timeout 60 strace -c -p ${NODE0_PID} -o "${TEST4_DIR}/strace_node0.txt" 2>&1 &
STRACE_PID=$!

# 使用 iotop 监控I/O (如果安装了)
if command -v iotop &> /dev/null; then
    echo -e "  使用 iotop 监控I/O活动 (60秒)..."
    timeout 60 sudo iotop -b -n 12 -d 5 -P -p ${NODE0_PID} > "${TEST4_DIR}/iotop_node0.txt" 2>&1 &
    IOTOP_PID=$!
fi

# 持续监控磁盘写入
echo -e "  监控磁盘写入量..."
df -h . | tee "${TEST4_DIR}/disk_before.txt"

# 产生大量写入
if [[ "${KV_TEST_AVAILABLE}" == "true" ]]; then
    echo -e "  发送大量PUT请求触发刷盘..."
    ${KV_PERF_TEST} ${CONFIG_FILE} 16 2000 put > "${TEST4_DIR}/client_heavy_put.txt" 2>&1 &
    CLIENT4_PID=$!
    sleep 60
fi

df -h . | tee "${TEST4_DIR}/disk_after.txt"

# 等待监控完成
wait ${STRACE_PID} 2>/dev/null || true

echo -e "${GREEN}  ✓ I/O 刷盘性能测试完成${NC}"
echo -e "    strace: ${TEST4_DIR}/strace_node0.txt"
if command -v iotop &> /dev/null; then
    echo -e "    iotop: ${TEST4_DIR}/iotop_node0.txt"
fi

# 分析 strace 结果
echo -e "\n${CYAN}>>> 分析系统调用统计...${NC}"
if [[ -f "${TEST4_DIR}/strace_node0.txt" ]]; then
    echo "=== 系统调用统计 ===" | tee "${TEST4_DIR}/syscall_analysis.txt"
    cat "${TEST4_DIR}/strace_node0.txt" | tee -a "${TEST4_DIR}/syscall_analysis.txt"
    
    echo -e "\n=== 关键I/O系统调用 ===" | tee -a "${TEST4_DIR}/syscall_analysis.txt"
    grep -E "(write|fsync|fdatasync|sync)" "${TEST4_DIR}/strace_node0.txt" | tee -a "${TEST4_DIR}/syscall_analysis.txt"
fi

################################################################################
# 清理
################################################################################

echo -e "\n${CYAN}>>> 停止RAFT集群...${NC}"
kill ${NODE0_PID} ${NODE1_PID} 2>/dev/null || true
sleep 2

# 强制杀掉可能残留的进程
pkill -f raftCoreRun 2>/dev/null || true
pkill -f kv_raft_performance_test 2>/dev/null || true

echo -e "${GREEN}  ✓ 集群已停止${NC}"

################################################################################
# 生成综合报告
################################################################################

echo -e "\n${BLUE}===============================================================================${NC}"
echo -e "${PURPLE}生成综合测试报告...${NC}"
echo -e "${BLUE}===============================================================================${NC}"

FINAL_REPORT="${SESSION_DIR}/TARGETED_PERF_ANALYSIS_REPORT.md"

cat > "${FINAL_REPORT}" <<EOF
# KV存储RAFT项目 - 针对性分层Perf分析报告

## 测试信息

- **测试时间**: $(date)
- **测试会话**: ${TIMESTAMP}
- **项目路径**: ${PROJECT_ROOT}
- **输出目录**: ${SESSION_DIR}

---

## 测试概述

本次测试针对RAFT项目的关键层进行了深度Perf分析：

1. ✅ **网络层**: RPC send/recv, ConnectionPool
2. ✅ **日志层**: 日志提交/提取, Persister
3. ✅ **I/O层**: 刷盘操作, fsync性能
4. ✅ **共识层**: AppendEntries, RequestVote, Leader选举

---

## 一、网络层 Perf 分析

### 火焰图
\`\`\`
file://${TEST1_DIR}/network_flamegraph.svg
\`\`\`

### 热点函数（Top 30）
\`\`\`
$(cat "${TEST1_DIR}/network_hotspots.txt" 2>/dev/null || echo "未找到热点数据")
\`\`\`

### 详细报告
详见: ${TEST1_DIR}/network_perf_report.txt

---

## 二、日志层 & I/O 层 Perf 分析

### 火焰图
\`\`\`
file://${TEST2_DIR}/log_io_flamegraph.svg
\`\`\`

### 热点函数（Top 30）
\`\`\`
$(cat "${TEST2_DIR}/log_io_hotspots.txt" 2>/dev/null || echo "未找到热点数据")
\`\`\`

### Persister 数据分析
\`\`\`
$(cat "${TEST2_DIR}/persister_analysis.txt" 2>/dev/null || echo "未找到Persister数据")
\`\`\`

### I/O 统计
\`\`\`
$(tail -50 "${TEST2_DIR}/iostat_during_test.txt" 2>/dev/null || echo "未找到iostat数据")
\`\`\`

### 详细报告
详见: ${TEST2_DIR}/log_io_perf_report.txt

---

## 三、共识层 Perf 分析

### 节点0火焰图（Leader）
\`\`\`
file://${TEST3_DIR}/consensus_node0_flamegraph.svg
\`\`\`

### 节点1火焰图（Follower）
\`\`\`
file://${TEST3_DIR}/consensus_node1_flamegraph.svg
\`\`\`

### 热点函数（Top 30）
\`\`\`
$(cat "${TEST3_DIR}/consensus_hotspots.txt" 2>/dev/null || echo "未找到热点数据")
\`\`\`

### 详细报告
- 节点0: ${TEST3_DIR}/consensus_node0_report.txt
- 节点1: ${TEST3_DIR}/consensus_node1_report.txt

---

## 四、专项 I/O 刷盘性能分析

### 系统调用统计（strace）
\`\`\`
$(cat "${TEST4_DIR}/syscall_analysis.txt" 2>/dev/null || echo "未找到系统调用数据")
\`\`\`

### 磁盘使用前后对比
\`\`\`
=== 测试前 ===
$(cat "${TEST4_DIR}/disk_before.txt" 2>/dev/null || echo "N/A")

=== 测试后 ===
$(cat "${TEST4_DIR}/disk_after.txt" 2>/dev/null || echo "N/A")
\`\`\`

### 详细日志
- strace: ${TEST4_DIR}/strace_node0.txt
- iotop: ${TEST4_DIR}/iotop_node0.txt (如果可用)

---

## 五、性能瓶颈分析

### 网络层瓶颈
$(grep -E "^[[:space:]]*[0-9]+\.[0-9]+%" "${TEST1_DIR}/network_perf_report.txt" | head -10 2>/dev/null || echo "请查看详细报告")

### I/O层瓶颈
$(grep -E "^[[:space:]]*[0-9]+\.[0-9]+%" "${TEST2_DIR}/log_io_perf_report.txt" | head -10 2>/dev/null || echo "请查看详细报告")

### 共识层瓶颈
$(grep -E "^[[:space:]]*[0-9]+\.[0-9]+%" "${TEST3_DIR}/consensus_node0_report.txt" | head -10 2>/dev/null || echo "请查看详细报告")

---

## 六、优化建议

基于本次Perf分析，建议：

### 网络层优化
1. 检查 send/recv 是否被正确 Hook
2. 优化 ConnectionPool 的锁竞争
3. 考虑使用 sendmsg/recvmsg 批量发送

### 日志层优化
1. 优化 Persister 的刷盘策略（批量写入）
2. 考虑使用 fdatasync 代替 fsync
3. 添加写缓存机制

### 共识层优化
1. 批量发送 AppendEntries
2. 优化心跳频率
3. 减少不必要的锁持有时间

---

## 七、测试文件清单

\`\`\`
$(find "${SESSION_DIR}" -type f -name "*.txt" -o -name "*.svg" -o -name "*.data" | sort)
\`\`\`

---

## 报告结束

**完整结果目录**: ${SESSION_DIR}

EOF

echo -e "${GREEN}  ✓ 综合报告已生成: ${FINAL_REPORT}${NC}"

################################################################################
# 总结
################################################################################

echo -e "\n${BLUE}===============================================================================${NC}"
echo -e "${GREEN}                    测试完成！${NC}"
echo -e "${BLUE}===============================================================================${NC}"
echo -e ""
echo -e "📊 测试结果目录: ${GREEN}${SESSION_DIR}${NC}"
echo -e ""
echo -e "📄 主要报告:"
echo -e "  ${CYAN}综合报告:${NC}    ${FINAL_REPORT}"
echo -e "  ${CYAN}网络层:${NC}      ${TEST1_DIR}/network_flamegraph.svg"
echo -e "  ${CYAN}日志&I/O层:${NC}  ${TEST2_DIR}/log_io_flamegraph.svg"
echo -e "  ${CYAN}共识层:${NC}      ${TEST3_DIR}/consensus_node0_flamegraph.svg"
echo -e "  ${CYAN}I/O刷盘:${NC}     ${TEST4_DIR}/syscall_analysis.txt"
echo -e ""
echo -e "🔍 查看报告:"
echo -e "  ${YELLOW}less ${FINAL_REPORT}${NC}"
echo -e "  ${YELLOW}firefox ${TEST1_DIR}/network_flamegraph.svg${NC}"
echo -e ""
echo -e "${BLUE}===============================================================================${NC}"

