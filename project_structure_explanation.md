# KV存储RAFT项目目录结构详细说明

## 项目整体架构

这是一个基于RAFT共识算法的分布式KV存储系统，采用C++20开发，包含完整的分布式系统组件。

## 根目录文件说明

### 配置文件
- **`.clang-format`** - Clang代码格式化配置文件，定义代码风格规范
- **`.clang-tidy`** - Clang静态分析工具配置，用于代码质量检查
- **`.gitignore`** - Git版本控制忽略文件列表
- **`CMakeLists.txt`** - 项目主构建配置文件，定义编译选项、依赖库等
- **`format.sh`** - 代码格式化脚本，统一项目代码风格
- **`test.conf`** - 测试配置文件，包含RAFT节点连接信息

### 数据文件
- **`raftstatePersist0.txt`**, **`raftstatePersist1.txt`**, **`raftstatePersist2.txt`** - RAFT节点状态持久化文件
- **`snapshotPersist0.txt`**, **`snapshotPersist1.txt`**, **`snapshotPersist2.txt`** - 快照持久化文件

### 文档
- **`README.md`** - 项目说明文档

## src/ 源代码目录

### src/common/ - 公共工具模块
- **`util.cpp`** - 通用工具函数实现
- **`include/config.h`** - 配置参数定义
- **`include/util.h`** - 工具函数头文件

### src/fiber/ - 协程框架模块
- **`fiber.cpp`** - 协程核心实现
- **`iomanager.cpp`** - I/O管理器，处理异步I/O操作
- **`scheduler.cpp`** - 协程调度器
- **`thread.cpp`** - 线程管理
- **`timer.cpp`** - 定时器管理
- **`hook.cpp`** - 系统调用hook，用于协程化
- **`fd_manager.cpp`** - 文件描述符管理
- **`utils.cpp`** - 协程相关工具函数

### src/rpc/ - RPC通信框架
- **`mprpcchannel.cpp`** - RPC通道实现
- **`mprpcconfig.cpp`** - RPC配置管理
- **`mprpccontroller.cpp`** - RPC控制器
- **`rpcprovider.cpp`** - RPC服务提供者
- **`rpcheader.pb.cpp`** - Protobuf生成的RPC头文件
- **`rpcheader.proto`** - RPC协议定义文件

### src/raftCore/ - RAFT核心算法
- **`raft.cpp`** - RAFT共识算法核心实现
- **`kvServer.cpp`** - KV存储服务器
- **`Persister.cpp`** - 状态持久化
- **`raftRpcUtil.cpp`** - RAFT RPC工具函数

### src/raftRpcPro/ - RAFT RPC协议
- **`kvServerRPC.pb.cc`** - KV服务器RPC协议实现
- **`kvServerRPC.proto`** - KV服务器RPC协议定义
- **`raftRPC.pb.cc`** - RAFT RPC协议实现
- **`raftRPC.proto`** - RAFT RPC协议定义

### src/raftClerk/ - 客户端模块
- **`clerk.cpp`** - 客户端实现，与RAFT集群交互
- **`raftServerRpcUtil.cpp`** - 服务器RPC工具

### src/skipList/ - 跳表存储引擎
- **包含头文件** - 跳表数据结构的实现

## example/ 示例程序目录

### example/raftCoreExample/ - RAFT核心示例
- **`raftKvDB.cpp`** - KV数据库服务器启动程序
  - 功能：启动多个RAFT节点进程
  - 参数：`-n <节点数量> -f <配置文件>`
  - 使用fork创建多个进程模拟分布式环境

- **`caller.cpp`** - 客户端调用示例
  - 功能：演示如何与RAFT集群交互
  - 包含Put/Get操作示例

### example/rpcExample/ - RPC通信示例
- **`friend.proto`** - 示例RPC协议定义
- **`friend.pb.cc/h`** - Protobuf生成的代码
- **`callee/friendService.cpp`** - RPC服务端实现
- **`caller/callFriendService.cpp`** - RPC客户端调用

### example/fiberExample/ - 协程使用示例
- **`test_scheduler.cpp`** - 协程调度器测试
- **`test_iomanager.cpp`** - I/O管理器测试
- **`test_hook.cpp`** - 系统调用hook测试
- **`test_thread.cc`** - 线程测试
- **`server.cpp`** - 协程服务器示例

## test/ 测试目录

### 测试程序
- **`run.cpp`** - 序列化/反序列化测试
  - 测试Boost序列化库的使用
  - 验证数据持久化功能

- **`defer_run.cpp`** - defer机制测试
  - 测试资源自动释放机制
  - 使用C++ RAII模式

- **`format.cpp`** - 代码格式化测试
- **`include/defer.h`** - defer机制头文件

## build/ 构建目录

- CMake生成的构建文件
- 编译中间文件
- 最终生成的可执行文件在 `bin/` 目录

## bin/ 可执行文件目录

### 主要可执行程序
- **`raftCoreRun`** - RAFT核心运行程序
  - 启动RAFT KV存储集群
  - 支持多节点部署

- **`callerMain`** - 客户端主程序
  - 与RAFT集群交互的客户端
  - 执行Put/Get操作

### RPC相关程序
- **`provider`** - RPC服务提供者
- **`consumer`** - RPC服务消费者

### 协程测试程序
- **`test_hook`** - 系统调用hook测试
- **`test_iomanager`** - I/O管理器测试
- **`test_scheduler`** - 调度器测试
- **`test_server`** - 服务器测试

## docs/ 文档目录

- **`目录导览.md`** - 项目目录说明
- **`rpc编码方式的改进.html`** - RPC编码优化文档
- **`images/`** - 项目相关图片

## 项目工作流程

### 1. 启动RAFT集群
```bash
./bin/raftCoreRun -n 3 -f cluster.conf
```

### 2. 客户端操作
```bash
./bin/callerMain
```

### 3. 性能测试流程
1. 启动RAFT节点集群
2. 运行压力测试程序
3. 使用perf采集性能数据
4. 生成火焰图分析性能瓶颈

## 技术栈总结

- **共识算法**: RAFT
- **网络通信**: RPC + Protobuf
- **并发模型**: 协程 + 线程池
- **存储引擎**: 跳表
- **构建系统**: CMake
- **代码质量**: Clang-format + Clang-tidy

这个项目实现了完整的分布式KV存储系统，包含了分布式系统的核心组件：共识算法、网络通信、数据存储、并发控制等。
