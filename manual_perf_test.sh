#!/bin/bash

# 最简化的手动Perf测试脚本

cd /home/ric/projects/work/KVstorageBaseRaft-cpp-main

# 清理
pkill -f raftCoreRun 2>/dev/null
pkill -f kv_raft_performance_test 2>/dev/null
sleep 2
rm -rf persisterData*
rm -rf manual_test_results
mkdir -p manual_test_results

echo "==============================================================================="
echo "         手动Perf测试 - 分层分析"
echo "==============================================================================="
echo ""

# 启动RAFT集群
echo "[1] 启动RAFT集群..."
./bin/raftCoreRun 0 test.conf > manual_test_results/node0.log 2>&1 &
NODE0_PID=$!
echo "  节点0 PID: $NODE0_PID"

./bin/raftCoreRun 1 test.conf > manual_test_results/node1.log 2>&1 &
NODE1_PID=$!
echo "  节点1 PID: $NODE1_PID"

echo "  等待Leader选举 (10秒)..."
sleep 10

# 测试1: 网络层
echo ""
echo "[2] 测试网络层 (30秒perf采样)..."
echo "  启动客户端产生网络流量..."
./bin/kv_raft_performance_test test.conf 4 500 put > manual_test_results/client1_output.txt 2>&1 &
CLIENT1_PID=$!

echo "  开始perf record..."
perf record -F 99 -g -o manual_test_results/network_perf.data -p $NODE0_PID sleep 30
echo "  ✓ perf采样完成"

echo "  生成报告..."
perf report -i manual_test_results/network_perf.data --stdio > manual_test_results/network_perf_report.txt 2>&1
echo "  生成火焰图..."
perf script -i manual_test_results/network_perf.data 2>/dev/null | \
    ./FlameGraph/stackcollapse-perf.pl | \
    ./FlameGraph/flamegraph.pl --title "网络层火焰图" > manual_test_results/network_flamegraph.svg
echo "  ✓ 网络层测试完成"

# 测试2: 日志I/O层
echo ""
echo "[3] 测试日志I/O层 (30秒perf采样)..."
echo "  启动客户端产生大量写入..."
./bin/kv_raft_performance_test test.conf 8 1000 put > manual_test_results/client2_output.txt 2>&1 &
CLIENT2_PID=$!

echo "  开始perf record..."
perf record -F 99 -g -o manual_test_results/log_io_perf.data -p $NODE0_PID sleep 30
echo "  ✓ perf采样完成"

echo "  生成报告..."
perf report -i manual_test_results/log_io_perf.data --stdio > manual_test_results/log_io_perf_report.txt 2>&1
echo "  生成火焰图..."
perf script -i manual_test_results/log_io_perf.data 2>/dev/null | \
    ./FlameGraph/stackcollapse-perf.pl | \
    ./FlameGraph/flamegraph.pl --title "日志I/O层火焰图" > manual_test_results/log_io_flamegraph.svg
echo "  ✓ 日志I/O层测试完成"

# 测试3: 共识层 (双节点)
echo ""
echo "[4] 测试共识层 (30秒perf采样 - 双节点)..."
echo "  启动客户端产生混合请求..."
./bin/kv_raft_performance_test test.conf 4 500 mixed > manual_test_results/client3_output.txt 2>&1 &
CLIENT3_PID=$!

echo "  开始perf record节点0..."
perf record -F 99 -g -o manual_test_results/consensus_node0_perf.data -p $NODE0_PID sleep 30 &
PERF0_PID=$!

echo "  开始perf record节点1..."
perf record -F 99 -g -o manual_test_results/consensus_node1_perf.data -p $NODE1_PID sleep 30 &
PERF1_PID=$!

wait $PERF0_PID
wait $PERF1_PID
echo "  ✓ perf采样完成"

echo "  生成节点0报告..."
perf report -i manual_test_results/consensus_node0_perf.data --stdio > manual_test_results/consensus_node0_report.txt 2>&1
perf script -i manual_test_results/consensus_node0_perf.data 2>/dev/null | \
    ./FlameGraph/stackcollapse-perf.pl | \
    ./FlameGraph/flamegraph.pl --title "共识层火焰图-节点0(Leader)" > manual_test_results/consensus_node0_flamegraph.svg

echo "  生成节点1报告..."
perf report -i manual_test_results/consensus_node1_perf.data --stdio > manual_test_results/consensus_node1_report.txt 2>&1
perf script -i manual_test_results/consensus_node1_perf.data 2>/dev/null | \
    ./FlameGraph/stackcollapse-perf.pl | \
    ./FlameGraph/flamegraph.pl --title "共识层火焰图-节点1(Follower)" > manual_test_results/consensus_node1_flamegraph.svg
echo "  ✓ 共识层测试完成"

# 测试4: strace系统调用
echo ""
echo "[5] 测试系统调用 (30秒strace)..."
echo "  启动客户端产生大量写入..."
./bin/kv_raft_performance_test test.conf 16 2000 put > manual_test_results/client4_output.txt 2>&1 &
CLIENT4_PID=$!

echo "  开始strace..."
timeout 30 strace -c -p $NODE0_PID 2> manual_test_results/strace_node0.txt
echo "  ✓ strace完成"

# 分析Persister数据
echo ""
echo "[6] 分析Persister数据..."
ls -lh persisterData* 2>/dev/null > manual_test_results/persister_files.txt
du -sh persisterData* 2>/dev/null >> manual_test_results/persister_files.txt
cat manual_test_results/persister_files.txt

# 清理
echo ""
echo "[7] 停止集群..."
kill $NODE0_PID $NODE1_PID 2>/dev/null
pkill -f raftCoreRun 2>/dev/null
pkill -f kv_raft_performance_test 2>/dev/null

echo ""
echo "==============================================================================="
echo "                    测试完成！"
echo "==============================================================================="
echo ""
echo "📊 结果目录: manual_test_results/"
echo ""
echo "📄 主要文件:"
echo "  网络层:"
echo "    - manual_test_results/network_perf_report.txt"
echo "    - manual_test_results/network_flamegraph.svg"
echo "  日志I/O层:"
echo "    - manual_test_results/log_io_perf_report.txt"
echo "    - manual_test_results/log_io_flamegraph.svg"
echo "  共识层:"
echo "    - manual_test_results/consensus_node0_report.txt"
echo "    - manual_test_results/consensus_node0_flamegraph.svg"
echo "    - manual_test_results/consensus_node1_report.txt"
echo "    - manual_test_results/consensus_node1_flamegraph.svg"
echo "  系统调用:"
echo "    - manual_test_results/strace_node0.txt"
echo ""
echo "==============================================================================="

