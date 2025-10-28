# RPC 协程集成说明

## 概述

`MprpcChannel` 已经完成了与 monsoon 协程库的集成，实现了以下关键特性：

1. **协程友好的 I/O 操作**：利用 monsoon 的 Hook 机制，将阻塞的 `send()` 和 `recv()` 系统调用透明地转换为协程友好的异步操作
2. **超时控制**：为所有 RPC 连接设置了 5 秒的接收和发送超时，配合 Hook 机制实现协程级别的超时控制

## 工作原理

### Hook 机制

当在启用了 Hook 的协程环境中调用 `send()` 或 `recv()` 时：

1. monsoon 的 Hook 层会拦截这些系统调用
2. 如果操作会阻塞（如网络数据尚未到达），Hook 会自动：
   - 调用 `Fiber::yield()` 挂起当前协程
   - 将 fd 的读/写事件注册到 `IOManager` 的 epoll 中
   - 释放物理线程去执行其他就绪的协程
3. 当数据到达或超时发生时，`IOManager` 会唤醒对应的协程
4. 协程从之前的 yield 点继续执行

### 超时控制

在 `newConnect()` 方法中，成功连接后会自动设置：
- `SO_RCVTIMEO`：接收超时 5 秒
- `SO_SNDTIMEO`：发送超时 5 秒

这些超时值会被 Hook 层的 `do_io` 函数检测到，并自动：
1. 在等待 I/O 时添加定时器任务
2. 超时后取消等待并唤醒协程
3. 防止协程被无限期挂起

## 使用方法

### 方式一：在 IOManager 调度的协程中使用（推荐）

```cpp
#include "iomanager.hpp"
#include "mprpcchannel.h"

void rpc_call_task() {
    // 在这个协程中，Hook 已经自动启用
    MprpcChannel channel("127.0.0.1", 8080, false);
    
    // 创建 stub 和 controller
    YourService_Stub stub(&channel);
    MprpcController controller;
    YourRequest request;
    YourResponse response;
    
    // 调用 RPC 方法 - send/recv 会被自动转换为协程友好的异步操作
    stub.YourMethod(&controller, &request, &response, nullptr);
    
    if (controller.Failed()) {
        std::cout << "RPC failed: " << controller.ErrorText() << std::endl;
    }
}

int main() {
    // 创建 IOManager，它会自动启用 Hook
    monsoon::IOManager iom(4);  // 4 个工作线程
    
    // 将 RPC 调用任务调度到协程中执行
    iom.scheduler(rpc_call_task);
    
    return 0;
}
```

### 方式二：手动启用 Hook

```cpp
#include "hook.hpp"
#include "mprpcchannel.h"

void some_function() {
    // 手动启用 Hook
    monsoon::set_hook_enable(true);
    
    MprpcChannel channel("127.0.0.1", 8080, false);
    
    // 进行 RPC 调用...
    
    // 可选：禁用 Hook
    monsoon::set_hook_enable(false);
}
```

## 注意事项

1. **推荐在 IOManager 环境中使用**：IOManager 在启动时会自动为其调度线程启用 Hook

2. **警告信息**：如果在未启用 Hook 的环境中调用 RPC，会打印警告信息：
   ```
   [WARNING-MprpcChannel::CallMethod] Hook is not enabled! 
   RPC calls should run in a coroutine with hook enabled for better performance.
   ```
   这不会导致程序错误，但会降低性能（回退到阻塞模式）

3. **超时调整**：如果需要修改默认的 5 秒超时，可以在 `newConnect()` 方法中修改：
   ```cpp
   timeout.tv_sec = 10;  // 修改为 10 秒
   ```

4. **线程安全**：`monsoon::set_hook_enable()` 是线程局部的，只影响调用它的线程

## 性能优势

- **高并发**：单个物理线程可以并发处理大量 RPC 调用
- **低延迟**：协程切换开销远小于线程切换
- **资源高效**：避免了为每个 RPC 连接创建独立线程
- **超时保护**：自动防止协程被网络延迟无限期挂起

## 代码修改总结

### 头文件 (mprpcchannel.h)
- 添加了 `#include "hook.hpp"` 引入 monsoon Hook 支持

### 源文件 (mprpcchannel.cpp)
1. 添加了 `#include "hook.hpp"`
2. 在 `CallMethod()` 开始处添加了 Hook 启用检查和警告
3. 在 `newConnect()` 中添加了 `SO_RCVTIMEO` 和 `SO_SNDTIMEO` 超时设置

## 原理深入

如果你想深入了解 Hook 机制的实现，可以查看：
- `src/fiber/hook.cpp` - Hook 实现和 `do_io` 函数
- `src/fiber/include/iomanager.hpp` - IOManager 接口定义
- `src/fiber/iomanager.cpp` - IOManager 实现，包括 epoll 事件循环

这些文件展示了如何将同步的系统调用透明地转换为协程友好的异步操作。


