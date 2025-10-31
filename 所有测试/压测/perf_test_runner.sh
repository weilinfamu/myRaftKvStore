#!/bin/bash

# KV存储RAFT项目性能测试运行脚本
# 这个脚本用于运行性能测试并生成性能分析报告

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 配置参数
CONFIG_FILE="test.conf"
THREADS=4
OPERATIONS=1000
TEST_TYPE="all"
PERF_DURATION=30
OUTPUT_DIR="perf_results"

# 打印帮助信息
print_help() {
    echo -e "${BLUE}KV存储RAFT项目性能测试运行脚本${NC}"
    echo
    echo "用法: $0 [选项]"
    echo
    echo "选项:"
    echo "  -c, --config FILE    配置文件路径 (默认: test.conf)"
    echo "  -t, --threads NUM    并发线程数 (默认: 4)"
    echo "  -o, --operations NUM 每个线程的操作数 (默认: 1000)"
    echo "  -T, --type TYPE      测试类型 (默认: all)"
    echo "                       [all|put|get|mixed|latency]"
    echo "  -d, --duration SEC   perf采样持续时间 (默认: 30)"
    echo "  -O, --output DIR     输出目录 (默认: perf_results)"
    echo "  -h, --help          显示此帮助信息"
    echo
    echo "示例:"
    echo "  $0 -c test.conf -t 8 -o 500 -T all"
    echo "  $0 --threads 4 --operations 2000 --type put"
}

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case $1 in
        -c|--config)
            CONFIG_FILE="$2"
            shift 2
            ;;
        -t|--threads)
            THREADS="$2"
            shift 2
            ;;
        -o|--operations)
            OPERATIONS="$2"
            shift 2
            ;;
        -T|--type)
            TEST_TYPE="$2"
            shift 2
            ;;
        -d|--duration)
            PERF_DURATION="$2"
            shift 2
            ;;
        -O|--output)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        -h|--help)
            print_help
            exit 0
            ;;
        *)
            echo -e "${RED}错误: 未知选项 $1${NC}"
            print_help
            exit 1
            ;;
    esac
done

# 检查配置文件是否存在
if [[ ! -f "$CONFIG_FILE" ]]; then
    echo -e "${RED}错误: 配置文件 $CONFIG_FILE 不存在${NC}"
    exit 1
fi

# 创建输出目录
mkdir -p "$OUTPUT_DIR"

echo -e "${BLUE}==========================================${NC}"
echo -e "${BLUE}KV存储RAFT项目性能测试${NC}"
echo -e "${BLUE}==========================================${NC}"
echo -e "配置文件: ${GREEN}$CONFIG_FILE${NC}"
echo -e "线程数: ${GREEN}$THREADS${NC}"
echo -e "操作数/线程: ${GREEN}$OPERATIONS${NC}"
echo -e "测试类型: ${GREEN}$TEST_TYPE${NC}"
echo -e "Perf采样: ${GREEN}${PERF_DURATION}秒${NC}"
echo -e "输出目录: ${GREEN}$OUTPUT_DIR${NC}"
echo -e "${BLUE}==========================================${NC}"

# 检查是否安装了perf
if ! command -v perf &> /dev/null; then
    echo -e "${YELLOW}警告: perf工具未安装，跳过性能分析${NC}"
    echo "在Ubuntu上安装: sudo apt install linux-tools-common linux-tools-generic"
    echo "在CentOS上安装: sudo yum install perf"
    PERF_AVAILABLE=false
else
    PERF_AVAILABLE=true
fi

# 构建项目
echo -e "${BLUE}[1/4] 构建项目...${NC}"
if [[ -d "build" ]]; then
    cd build
    make -j$(nproc)
    cd ..
else
    echo -e "${RED}错误: build目录不存在，请先运行cmake${NC}"
    exit 1
fi

# 检查可执行文件是否存在
if [[ ! -f "bin/kv_raft_performance_test" ]]; then
    echo -e "${RED}错误: 可执行文件 bin/kv_raft_performance_test 不存在${NC}"
    echo "请确保项目构建成功"
    exit 1
fi

# 运行性能测试
echo -e "${BLUE}[2/4] 运行性能测试...${NC}"
TEST_OUTPUT="$OUTPUT_DIR/test_output_$(date +%Y%m%d_%H%M%S).txt"

if $PERF_AVAILABLE; then
    # 使用perf记录性能数据
    echo -e "${YELLOW}使用perf记录性能数据...${NC}"
    perf record -F 99 -g -- bin/kv_raft_performance_test "$CONFIG_FILE" "$THREADS" "$OPERATIONS" "$TEST_TYPE" \
        > "$TEST_OUTPUT" 2>&1 &
    
    TEST_PID=$!
    
    # 等待测试完成或超时
    echo -e "测试进程PID: $TEST_PID"
    wait $TEST_PID 2>/dev/null
    TEST_EXIT_CODE=$?
    
    if [[ $TEST_EXIT_CODE -ne 0 ]]; then
        echo -e "${RED}性能测试失败，退出码: $TEST_EXIT_CODE${NC}"
        cat "$TEST_OUTPUT"
        exit $TEST_EXIT_CODE
    fi
    
    # 生成perf报告
    echo -e "${BLUE}[3/4] 生成性能分析报告...${NC}"
    
    # 生成火焰图数据
    perf script | ./FlameGraph/stackcollapse-perf.pl > "$OUTPUT_DIR/perf.folded"
    
    # 生成火焰图
    ./FlameGraph/flamegraph.pl "$OUTPUT_DIR/perf.folded" > "$OUTPUT_DIR/perf.svg"
    
    # 生成文本报告
    perf report --stdio > "$OUTPUT_DIR/perf_report.txt"
    
    echo -e "${GREEN}性能分析报告生成完成:${NC}"
    echo -e "  - 火焰图: ${OUTPUT_DIR}/perf.svg"
    echo -e "  - 性能报告: ${OUTPUT_DIR}/perf_report.txt"
else
    # 不使用perf，直接运行测试
    echo -e "${YELLOW}跳过性能分析，直接运行测试...${NC}"
    bin/kv_raft_performance_test "$CONFIG_FILE" "$THREADS" "$OPERATIONS" "$TEST_TYPE" \
        > "$TEST_OUTPUT" 2>&1
    
    TEST_EXIT_CODE=$?
    
    if [[ $TEST_EXIT_CODE -ne 0 ]]; then
        echo -e "${RED}性能测试失败，退出码: $TEST_EXIT_CODE${NC}"
        cat "$TEST_OUTPUT"
        exit $TEST_EXIT_CODE
    fi
fi

# 生成测试摘要
echo -e "${BLUE}[4/4] 生成测试摘要...${NC}"
SUMMARY_FILE="$OUTPUT_DIR/test_summary_$(date +%Y%m%d_%H%M%S).txt"

{
    echo "KV存储RAFT项目性能测试摘要"
    echo "=========================="
    echo "测试时间: $(date)"
    echo "配置文件: $CONFIG_FILE"
    echo "线程数: $THREADS"
    echo "操作数/线程: $OPERATIONS"
    echo "测试类型: $TEST_TYPE"
    echo "Perf采样: ${PERF_DURATION}秒"
    echo
    echo "测试输出:"
    echo "--------"
    cat "$TEST_OUTPUT"
} > "$SUMMARY_FILE"

echo -e "${GREEN}测试摘要生成完成: $SUMMARY_FILE${NC}"

# 显示关键结果
echo -e "${BLUE}==========================================${NC}"
echo -e "${GREEN}性能测试完成!${NC}"
echo -e "${BLUE}==========================================${NC}"
echo -e "测试输出: ${GREEN}$TEST_OUTPUT${NC}"
if $PERF_AVAILABLE; then
    echo -e "火焰图: ${GREEN}${OUTPUT_DIR}/perf.svg${NC}"
    echo -e "性能报告: ${GREEN}${OUTPUT_DIR}/perf_report.txt${NC}"
fi
echo -e "测试摘要: ${GREEN}$SUMMARY_FILE${NC}"

# 显示测试结果的关键指标
echo
echo -e "${YELLOW}关键性能指标:${NC}"
grep -E "(吞吐量|成功率|平均延迟|P50|P95|P99)" "$TEST_OUTPUT" | head -10

echo -e "${BLUE}==========================================${NC}"
