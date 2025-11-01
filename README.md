# 基于 Raft 共识算法的分布式 KV 存储数据库

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)]() [![Language](https://img.shields.io/badge/language-C%2B%2B17-blue)]() [![License](https://img.shields.io/badge/license-MIT-orange)]()

本项目是基于 Raft 共识算法的分布式 KV 数据库，具备线性一致性和分区容错性，在少于半数节点失效时仍可正常对外提供服务。

## ✨ 核心特性

- 🔐 **Raft 共识算法**：实现强一致性、Leader 选举、日志复制、快照机制
- 🚀 **高性能 RPC**：自研 MprRpc 框架，支持 Protobuf 序列化、连接池、健康检查
- 📦 **SkipList 存储**：跳表数据结构实现高效 KV 存储
- 🌐 **异步协程**：集成 sptsock 协程库，支持高并发异步 I/O
- 📝 **工业级日志**：spdlog 异步日志系统，支持日志轮转和多级别输出
- 🔧 **可扩展架构**：接口抽象 + 依赖注入，松耦合设计

## 📁 项目结构

```
.
├── src/                    # 源代码
│   ├── raftCore/          # Raft 共识层
│   ├── raftClerk/         # 客户端
│   ├── rpc/               # RPC 框架
│   ├── skipList/          # 跳表存储引擎
│   ├── fiber/             # 协程库
│   └── common/            # 公共组件（日志、工具）
├── config/                 # 配置文件
│   ├── test_3nodes.conf   # 3节点测试配置
│   ├── test_5nodes.conf   # 5节点测试配置
│   └── test_7nodes.conf   # 7节点测试配置
├── docs/                   # 文档
│   ├── architecture/      # 架构设计文档
│   ├── git-logs/          # Git 提交记录
│   ├── interview/         # 面试准备材料
│   ├── testing/           # 测试文档
│   ├── logging/           # 日志系统文档
│   └── quick-start/       # 快速开始指南
├── example/                # 示例代码
└── 所有测试/               # 测试用例

详细的项目结构说明见：docs/project_structure_explanation.md
```

## 🚀 快速开始

### 1. 编译项目

```bash
# 清理旧编译
rm -rf build
mkdir build && cd build

# Debug 模式（开发调试）
cmake .. -DCMAKE_BUILD_TYPE=Debug -DDebug=ON
make -j$(nproc)

# Release 模式（生产环境）
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 2. 启动 3 节点集群

```bash
# 终端 1 - 启动节点 0
./bin/raftCoreRun callee config/test_3nodes.conf 0

# 终端 2 - 启动节点 1
./bin/raftCoreRun callee config/test_3nodes.conf 1

# 终端 3 - 启动节点 2
./bin/raftCoreRun callee config/test_3nodes.conf 2

# 终端 4 - 启动客户端测试
./bin/callerMain caller config/test_3nodes.conf
```

### 3. 查看日志

```bash
# 实时查看节点日志
tail -f logs/raft_node_0.log

# 只看错误日志
grep "\[error\]" logs/raft_node_0.log
```

## 📚 详细文档

- **快速开始**: [docs/quick-start/🚀快速开始-面试准备.md](docs/quick-start/🚀快速开始-面试准备.md)
- **架构设计**: [docs/architecture/架构解耦优化方案.md](docs/architecture/架构解耦优化方案.md)
- **配置说明**: [config/README_配置说明.md](config/README_配置说明.md)
- **日志系统**: [docs/logging/日志系统调试详解.md](docs/logging/日志系统调试详解.md)
- **调试指南**: [docs/testing/调试指南.md](docs/testing/调试指南.md)
- **面试准备**: [docs/interview/简历项目描述.md](docs/interview/简历项目描述.md)

完整文档导航：[docs/README.md](docs/README.md)

## 🔧 技术栈

| 组件 | 技术 |
|------|------|
| **编程语言** | C++17 |
| **共识算法** | Raft (Leader 选举、日志复制、快照) |
| **RPC 框架** | 自研 MprRpc (Protobuf + 连接池 + 健康检查) |
| **存储引擎** | SkipList（跳表） |
| **协程库** | sptsock (基于 epoll 的异步 I/O) |
| **日志系统** | spdlog（异步、轮转、多级别） |
| **序列化** | Protobuf、Boost.Serialization |
| **构建工具** | CMake |

## 📊 性能指标

- **吞吐量**: 10000+ ops/sec（3节点集群）
- **延迟**: P50 < 5ms, P99 < 20ms
- **容错能力**: 支持少数节点故障（3节点可容忍1个故障）
- **日志性能**: spdlog 异步日志提升 3-5倍

详细性能测试报告：[docs/performance/](docs/performance/)

## 🧪 测试

### 功能测试

```bash
# 基础功能测试
cd 所有测试
./run_test.sh

# 容错性测试（杀死 Leader）
./test_fault_tolerance.sh
```

### 性能测试

```bash
cd 所有测试/压测
./benchmark.sh
```

测试文档：[docs/testing/](docs/testing/)

## 📖 学习路径

### 新手入门
1. 阅读 [快速开始指南](docs/quick-start/🚀快速开始-面试准备.md)
2. 编译并运行 3 节点集群
3. 使用客户端进行 Put/Get 操作
4. 查看日志理解 Raft 工作流程

### 深入学习
1. 阅读 [架构设计文档](docs/architecture/架构解耦优化方案.md)
2. 研究 Raft 共识算法实现
3. 分析 RPC 框架和连接池机制
4. 学习协程和异步 I/O

### 高级进阶
1. 修改代码实现新功能
2. 进行性能测试和优化
3. 参与代码 Review
4. 阅读 [面试准备材料](docs/interview/)

## 🤝 贡献指南

欢迎提交 Issue 和 Pull Request！

1. Fork 项目
2. 创建功能分支 (`git checkout -b feature/amazing-feature`)
3. 提交更改 (`git commit -m 'Add some amazing feature'`)
4. 推送到分支 (`git push origin feature/amazing-feature`)
5. 打开 Pull Request

## 📝 Git 提交记录

查看完整的 Git 提交记录：[docs/git-logs/Git提交快速查找卡.md](docs/git-logs/Git提交快速查找卡.md)

最近更新：
- **2025-11-01**: 架构解耦重构，引入接口抽象层和适配器模式
- **2025-11-01**: 日志系统升级，替换 DPrintf 为 spdlog
- **2025-11-01**: 项目结构重组，文档归类整理

完整记录：[docs/git-logs/](docs/git-logs/)

## 📞 联系方式

- **项目地址**: https://github.com/weilinfamu/myRaftKvStore
- **面试技术报告**: [docs/interview/面试技术报告完整版.md](docs/interview/面试技术报告完整版.md)
- **简历项目描述**: [docs/interview/简历项目描述.md](docs/interview/简历项目描述.md)

## 📄 License

本项目采用 MIT 许可证 - 详见 [LICENSE](LICENSE) 文件

---

**⭐ 如果这个项目对你有帮助，请给个 Star！**

**最后更新**: 2025-11-01
