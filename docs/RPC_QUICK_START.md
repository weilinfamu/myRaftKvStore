# RPC 协程集成 - 快速开始

## 一分钟快速上手

### 1. 基本用法

```cpp
#include "iomanager.hpp"
#include "mprpcchannel.h"

void my_rpc_call() {
    MprpcChannel channel("192.168.1.100", 8080, false);
    YourService_Stub stub(&channel);
    MprpcController controller;
    YourRequest request;
    YourResponse response;
    
    stub.YourMethod(&controller, &request, &response, nullptr);
    
    if (controller.Failed()) {
        std::cout << "失败: " << controller.ErrorText() << std::endl;
    }
}

int main() {
    monsoon::IOManager iom(4);  // 4个线程
    iom.scheduler(my_rpc_call);  // 在协程中执行
    return 0;
}
```

### 2. 关键点

✅ **在 IOManager 中调度**：确保 RPC 调用在协程环境中执行
```cpp
monsoon::IOManager iom(4);
iom.scheduler(my_rpc_function);  // ← 必须这样调用
```

✅ **自动超时**：已设置 5 秒超时，防止无限等待

✅ **自动异步**：send/recv 会被自动转换为协程友好的异步操作

### 3. 性能对比

#### 传统方式（阻塞）
```
1000 个 RPC 调用 → 需要 1000 个线程（或大线程池）
内存: ~8GB (1000 × 8MB 栈空间)
性能: 大量线程上下文切换
```

#### 协程方式（当前实现）
```
1000 个 RPC 调用 → 只需 4 个线程 + 1000 个协程
内存: ~几十MB (协程栈很小)
性能: 极低的切换开销
```

### 4. 完整文档

- **详细说明**: [docs/RPC协程集成说明.md](./RPC协程集成说明.md)
- **示例代码**: [example/rpc_coroutine_example.cpp](../example/rpc_coroutine_example.cpp)
- **修改总结**: [CHANGES_SUMMARY.md](../CHANGES_SUMMARY.md)

### 5. 常见问题

**Q: 必须在协程环境中使用吗？**
A: 不是必须的。在普通线程中也能用，但会回退到阻塞模式（性能较差）。

**Q: Hook 是什么？**
A: Hook 是 monsoon 库的机制，能自动将 send/recv 等系统调用转换为协程友好的异步操作。

**Q: 超时时间能改吗？**
A: 可以，修改 `newConnect()` 中的 `timeout.tv_sec = 5;` 即可。

**Q: 线程安全吗？**
A: MprpcChannel 不是线程安全的，每个协程应使用独立的实例。

### 6. 注意事项

⚠️ **不要**在没有 IOManager 的普通线程中大量使用（性能差）
⚠️ **不要**在多个协程间共享同一个 MprpcChannel 实例
✅ **应该**为每个协程创建独立的 MprpcChannel
✅ **应该**使用 IOManager 进行任务调度

---

开始使用协程化的 RPC，享受高性能并发！🚀


