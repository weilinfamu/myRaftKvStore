# Epoll 与协程深度分析：KVStorageBaseRaft项目实战

## 目录
1. [面试核心问题快速回答](#面试核心问题快速回答)
2. [协程在项目中的使用](#协程在项目中的使用)
3. [Epoll在项目中的使用](#epoll在项目中的使用)
4. [Epoll与协程的协同工作](#epoll与协程的协同工作)
5. [技术对比分析](#技术对比分析)
6. [完整工作流程示例](#完整工作流程示例)
7. [性能优势分析](#性能优势分析)
8. [常见面试问题深入解答](#常见面试问题深入解答)

---

## 面试核心问题快速回答

### Q1: 协程主要用在哪里？

**在本项目中的三个核心应用场景：**

1. **Raft定时任务调度** ([raft.cpp:1033-1034](src/raftCore/raft.cpp#L1033-L1034))
   ```cpp
   m_ioManager->scheduler([this]() -> void { this->leaderHearBeatTicker(); });
   m_ioManager->scheduler([this]() -> void { this->electionTimeOutTicker(); });
   ```
   - Leader心跳定时器（50ms周期）
   - 选举超时定时器（150-300ms随机）

2. **RPC网络通信** ([mprpcchannel.cpp:1-50](src/rpc/mprpcchannel.cpp#L1-L50))
   - 所有的socket操作（connect、send、recv）都被hook
   - 阻塞IO自动转换为协程yield/resume
   - 心跳检测也使用协程定时器

3. **并发请求处理**
   - 每个网络IO请求在协程中执行
   - 一个线程可以处理成千上万个并发连接
   - 避免了线程池的线程数量限制

---

### Q2: 为什么要用协程？

**四大核心优势：**

#### 1. **轻量级并发**
- **传统线程**：每个线程栈空间1-8MB，创建开销大（用户态+内核态切换）
- **协程**：栈空间仅128KB，完全在用户态切换
- **实际效果**：可以轻松创建10万+协程，而线程池通常只有几十个线程

#### 2. **避免回调地狱**
```cpp
// 传统异步回调方式（回调地狱）
asyncConnect(ip, port, [](Result r1) {
    if (r1.success) {
        asyncSend(data, [](Result r2) {
            if (r2.success) {
                asyncRecv([](Result r3) {
                    // 层层嵌套...
                });
            }
        });
    }
});

// 协程方式（同步写法，异步执行）
connect(ip, port);      // 内部自动yield
send(data);             // 内部自动yield
recv();                 // 内部自动yield
```

#### 3. **提高CPU利用率**
- IO等待时自动让出CPU给其他协程
- 没有线程阻塞，所有线程始终在工作
- 本项目：3个工作线程可以处理数千个并发Raft节点通信

#### 4. **简化并发编程**
- 代码逻辑清晰，按顺序编写
- 自动处理异步IO等待
- 无需手动管理回调函数和状态机

---

### Q3: 项目用了epoll吗？在哪里？

**是的，深度使用！** epoll是整个异步IO的核心底层。

**使用位置：** [iomanager.cpp:37-264](src/fiber/iomanager.cpp#L37-L264)

```cpp
// 创建epoll实例
IOManager::IOManager(...) {
    epfd_ = epoll_create(5000);  // 第37行

    // 注册tickle管道（用于唤醒idle协程）
    epoll_event event{};
    event.events = EPOLLIN | EPOLLET;  // 边缘触发
    event.data.fd = tickleFds_[0];
    epoll_ctl(epfd_, EPOLL_CTL_ADD, tickleFds_[0], &event);
}

// idle协程中等待IO事件
void IOManager::idle() {
    epoll_event events[256];
    while (true) {
        // 阻塞等待事件就绪或定时器超时
        int ret = epoll_wait(epfd_, events, 256, next_timeout);

        // 处理就绪事件
        for (int i = 0; i < ret; i++) {
            FdContext *fd_ctx = (FdContext*)events[i].data.ptr;
            // 触发对应的READ/WRITE事件，调度协程
            fd_ctx->triggerEvent(event);
        }

        // 让出CPU给就绪的协程执行
        Fiber::GetThis()->yield();
    }
}
```

**epoll三大操作在项目中的完整应用：**
| 操作 | 代码位置 | 触发时机 | 说明 |
|------|---------|---------|------|
| `epoll_ctl ADD` | [iomanager.cpp:96](src/fiber/iomanager.cpp#L96) | Hook函数检测到IO未就绪(EAGAIN) | 注册新事件，边缘触发(EPOLLET) |
| `epoll_ctl MOD` | [iomanager.cpp:91](src/fiber/iomanager.cpp#L91) | fd已注册，添加新事件类型 | 修改已注册的事件 |
| `epoll_ctl DEL` | [iomanager.cpp:137](src/fiber/iomanager.cpp#L137) | 删除/取消事件 | 从epoll中移除fd |
| `epoll_wait` | [iomanager.cpp:264](src/fiber/iomanager.cpp#L264) | idle协程中循环调用 | 等待事件就绪，支持超时 |

---

### Q4: Epoll和协程的区别是什么？

**这是一个层次问题，两者不是替代关系，而是配合关系！**

| 维度 | Epoll | 协程 | 关系 |
|------|-------|------|------|
| **定位** | Linux内核提供的IO多路复用机制 | 用户态轻量级线程 | epoll负责底层事件通知，协程负责上层任务调度 |
| **作用** | 监控多个fd的IO事件（可读/可写） | 执行具体的业务逻辑 | epoll检测到事件→触发协程恢复 |
| **所在层次** | 内核态系统调用 | 用户态上下文切换 | epoll是底层，协程是上层 |
| **使用方式** | epoll_create/ctl/wait | yield/resume | epoll驱动协程调度 |
| **并发模型** | 一个线程监听多个fd | 一个线程运行多个协程 | 结合使用实现高并发 |

**形象比喻：**
- **epoll** = 门卫（监控哪些门有人敲门）
- **协程** = 服务员（处理具体客人的请求）
- **IOManager** = 调度中心（门卫通知调度中心→调度中心唤醒服务员）

---

## 协程在项目中的使用

### 1. 协程实现原理

本项目使用**自定义协程库Monsoon**，基于POSIX的`ucontext_t`实现。

#### 1.1 协程核心数据结构

[fiber.hpp:13-73](src/fiber/include/fiber.hpp#L13-L73)

```cpp
class Fiber : public std::enable_shared_from_this<Fiber> {
public:
    typedef std::shared_ptr<Fiber> ptr;

    enum State {
        READY,      // 就绪态，刚创建或yield后的状态
        RUNNING,    // 运行态，resume之后的状态
        TERM,       // 结束态，回调函数执行完之后的状态
    };

private:
    uint64_t id_;                    // 协程ID
    uint32_t stackSize_;             // 协程栈大小（默认128KB）
    State state_;                    // 协程状态
    ucontext_t ctx_;                 // 协程上下文（寄存器、栈指针等）
    void* stack_ptr;                 // 协程栈地址
    std::function<void()> cb_;       // 协程执行的回调函数
    bool isRunInScheduler_;          // 是否参与调度器调度
};
```

**关键点：**
- **ucontext_t**：保存协程的CPU寄存器状态、栈指针、程序计数器等
- **独立栈空间**：每个协程拥有独立的128KB栈（可配置）
- **状态机**：READY → RUNNING → TERM（或READY）

#### 1.2 协程切换机制

```cpp
// 恢复协程执行
void Fiber::resume() {
    state_ = RUNNING;
    SetThis(this);  // 设置当前协程
    // 从调度协程切换到本协程（保存调度协程上下文，恢复本协程上下文）
    swapcontext(&(t_scheduler_fiber->ctx_), &ctx_);
}

// 让出CPU
void Fiber::yield() {
    SetThis(t_scheduler_fiber.get());
    state_ = READY;
    // 从本协程切换回调度协程
    swapcontext(&ctx_, &(t_scheduler_fiber->ctx_));
}
```

**上下文切换成本：**
- **线程切换**：需要进入内核态，切换页表、刷新TLB，成本约1-10微秒
- **协程切换**：纯用户态，仅保存/恢复寄存器，成本约10-100纳秒
- **性能差距**：协程切换比线程切换快**10-1000倍**

---

### 2. 协程调度器（Scheduler）

#### 2.1 N→M调度模型

[scheduler.hpp:46-144](src/fiber/include/scheduler.hpp#L46-L144)

```cpp
class Scheduler {
private:
    std::vector<Thread::ptr> threadPool_;      // N个工作线程
    std::list<SchedulerTask> tasks_;           // M个协程任务队列
    Fiber::ptr rootFiber_;                     // 调度协程
    std::atomic<size_t> activeThreadCnt_;      // 活跃线程数
    std::atomic<size_t> idleThreadCnt_;        // 空闲线程数

public:
    // 添加调度任务（可以是协程或函数）
    template<class TaskType>
    void scheduler(TaskType task, int thread = -1) {
        bool needTickle = false;
        {
            Mutex::Lock lock(mutex_);
            needTickle = tasks_.empty();
            tasks_.push_back(SchedulerTask(task, thread));
        }
        if (needTickle) {
            tickle();  // 唤醒idle线程
        }
    }
};
```

**调度流程：**
```
         ┌─────────────────────────────────────┐
         │       Scheduler (调度器)             │
         │  ┌─────────────────────────────┐    │
         │  │  Task Queue (任务队列)       │    │
         │  │  - Fiber1 (协程1)            │    │
         │  │  - Fiber2 (协程2)            │    │
         │  │  - Callback3 (回调函数)      │    │
         │  └─────────────────────────────┘    │
         │           ↓ 调度分配                 │
         │  ┌──────┬──────┬──────┬──────┐      │
         │  │Thread│Thread│Thread│Thread│      │
         │  │  1   │  2   │  3   │  4   │      │
         │  └──────┴──────┴──────┴──────┘      │
         └─────────────────────────────────────┘
```

---

### 3. IOManager：协程+Epoll的完美结合

#### 3.1 IOManager架构

[iomanager.hpp:42-78](src/fiber/include/iomanager.hpp#L42-L78)

```cpp
class IOManager : public Scheduler, public TimerManager {
private:
    int epfd_ = 0;                              // epoll文件描述符
    int tickleFds_[2];                          // pipe管道，用于tickle唤醒
    std::atomic<size_t> pendingEventCnt_;       // 待处理IO事件数量
    std::vector<FdContext*> fdContexts_;        // 文件描述符上下文数组

public:
    // 添加IO事件（自动注册到epoll）
    int addEvent(int fd, Event event, std::function<void()> cb = nullptr);

    // 删除/取消事件
    bool delEvent(int fd, Event event);
    bool cancelEvent(int fd, Event event);
};
```

#### 3.2 文件描述符上下文

```cpp
struct EventContext {
    Scheduler* scheduler = nullptr;   // 调度器
    Fiber::ptr fiber;                 // 事件就绪时执行的协程
    std::function<void()> cb;         // 或者执行的回调函数
};

class FdContext {
    EventContext read;    // 读事件上下文
    EventContext write;   // 写事件上下文
    int fd = 0;           // 文件描述符
    Event events = NONE;  // 已注册的事件类型
    Mutex mutex;          // 保护并发访问
};
```

---

### 4. 在Raft中的具体应用

#### 4.1 初始化协程调度器

[raft.cpp:1027-1034](src/raftCore/raft.cpp#L1027-L1034)

```cpp
void Raft::init(...) {
    // 创建IOManager：3个工作线程，使用调用线程
    m_ioManager = std::make_unique<monsoon::IOManager>(
        FIBER_THREAD_NUM,           // 3个工作线程
        FIBER_USE_CALLER_THREAD     // 使用主线程参与调度
    );

    // 调度Leader心跳协程（50ms周期）
    m_ioManager->scheduler([this]() -> void {
        this->leaderHearBeatTicker();
    });

    // 调度选举超时协程（150-300ms随机）
    m_ioManager->scheduler([this]() -> void {
        this->electionTimeOutTicker();
    });

    // applierTicker使用独立线程（防止数据库IO阻塞）
    std::thread t3(&Raft::applierTicker, this);
    t3.detach();
}
```

#### 4.2 心跳定时器协程

```cpp
void Raft::leaderHearBeatTicker() {
    while (true) {
        // 使用协程友好的sleep（内部会yield）
        usleep(HeartBeatTimeout * 1000);  // 50ms

        if (m_status == Leader) {
            DPrintf("[LeaderHearBeatTicker] Leader %d send heartbeat", m_me);

            // 向所有Follower发送心跳（并行）
            for (int i = 0; i < peers.size(); i++) {
                if (i == m_me) continue;

                // 发送AppendEntries RPC（内部socket操作被hook）
                doHeartBeat(i);
            }
        }
    }
}
```

**协程优势体现：**
1. `usleep`被hook，自动yield让出CPU
2. 50ms到期后，定时器触发，协程自动resume
3. 同一线程可以运行多个定时器协程
4. 不需要为每个定时器创建线程

---

## Epoll在项目中的使用

### 1. Epoll基础概念

#### 1.1 什么是Epoll？

**Epoll是Linux提供的高性能IO多路复用机制**，用于监控多个文件描述符的IO事件。

**三大核心函数：**
```cpp
// 1. 创建epoll实例
int epfd = epoll_create(5000);

// 2. 注册/修改/删除fd的监听事件
struct epoll_event ev;
ev.events = EPOLLIN | EPOLLET;  // 读事件 + 边缘触发
ev.data.ptr = context;
epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);

// 3. 等待事件就绪
struct epoll_event events[256];
int n = epoll_wait(epfd, events, 256, timeout);
for (int i = 0; i < n; i++) {
    // 处理events[i]
}
```

#### 1.2 边缘触发 vs 水平触发

本项目使用**边缘触发（EPOLLET）**模式：

| 模式 | 触发时机 | 优缺点 |
|------|---------|--------|
| **水平触发(LT)** | 只要fd有数据就触发 | 简单但性能差，可能重复通知 |
| **边缘触发(ET)** | 仅在fd状态变化时触发 | 高性能，但需要一次读完所有数据 |

**项目代码：** [iomanager.cpp:44](src/fiber/iomanager.cpp#L44)
```cpp
event.events = EPOLLIN | EPOLLET;  // 边缘触发
```

---

### 2. Epoll在IOManager中的完整实现

#### 2.1 创建Epoll实例

[iomanager.cpp:36-56](src/fiber/iomanager.cpp#L36-L56)

```cpp
IOManager::IOManager(size_t threads, bool use_caller, const std::string& name)
    : Scheduler(threads, use_caller, name) {

    // 创建epoll实例，容量5000
    epfd_ = epoll_create(5000);

    // 创建pipe管道用于tickle通知
    int ret = pipe(tickleFds_);

    // 注册pipe读端到epoll（用于唤醒idle协程）
    epoll_event event{};
    event.events = EPOLLIN | EPOLLET;  // 读事件 + 边缘触发
    event.data.fd = tickleFds_[0];

    // 设置非阻塞（边缘触发要求）
    fcntl(tickleFds_[0], F_SETFL, O_NONBLOCK);

    // 注册到epoll
    epoll_ctl(epfd_, EPOLL_CTL_ADD, tickleFds_[0], &event);

    // 预分配32个FdContext
    contextResize(32);

    // 启动调度器
    start();
}
```

---

#### 2.2 添加事件到Epoll

[iomanager.cpp:72-120](src/fiber/iomanager.cpp#L72-L120)

```cpp
int IOManager::addEvent(int fd, Event event, std::function<void()> cb) {
    // 1. 获取或创建FdContext
    FdContext* fd_ctx = nullptr;
    RWMutex::ReadLock lock(mutex_);
    if ((int)fdContexts_.size() > fd) {
        fd_ctx = fdContexts_[fd];
    } else {
        lock.unlock();
        RWMutex::WriteLock lock2(mutex_);
        contextResize(fd * 1.5);  // 动态扩容
        fd_ctx = fdContexts_[fd];
    }

    // 2. 判断是ADD还是MOD操作
    Mutex::Lock ctxLock(fd_ctx->mutex);
    int op = fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;

    // 3. 构造epoll_event
    epoll_event epevent;
    epevent.events = EPOLLET | fd_ctx->events | event;  // 边缘触发 + 已有事件 + 新事件
    epevent.data.ptr = fd_ctx;  // 保存上下文指针

    // 4. 注册到epoll
    int ret = epoll_ctl(epfd_, op, fd, &epevent);
    if (ret) {
        return -1;
    }

    // 5. 更新FdContext
    ++pendingEventCnt_;  // 待处理事件+1
    fd_ctx->events = (Event)(fd_ctx->events | event);

    // 6. 设置事件回调
    EventContext& event_ctx = fd_ctx->getEveContext(event);
    event_ctx.scheduler = Scheduler::GetThis();
    if (cb) {
        event_ctx.cb.swap(cb);  // 设置回调函数
    } else {
        event_ctx.fiber = Fiber::GetThis();  // 设置当前协程
    }

    return 0;
}
```

---

#### 2.3 Epoll事件循环（核心）

[iomanager.cpp:238-350](src/fiber/iomanager.cpp#L238-L350)

```cpp
void IOManager::idle() {
    const uint64_t MAX_EVENTS = 256;
    epoll_event* events = new epoll_event[MAX_EVENTS]();

    while (true) {
        // ========== 1. 检查是否可以停止 ==========
        uint64_t next_timeout = 0;
        if (stopping(next_timeout)) {
            break;
        }

        // ========== 2. epoll_wait等待事件 ==========
        int ret = 0;
        do {
            // 超时时间：取定时器超时和最大超时的最小值
            if (next_timeout != ~0ull) {
                next_timeout = std::min((int)next_timeout, 5000);
            } else {
                next_timeout = 5000;
            }

            // 阻塞等待事件就绪
            ret = epoll_wait(epfd_, events, MAX_EVENTS, (int)next_timeout);

            if (ret < 0 && errno == EINTR) {
                continue;  // 被信号中断，重试
            }
            break;
        } while (true);

        // ========== 3. 处理超时的定时器 ==========
        std::vector<std::function<void()>> cbs;
        listExpiredCb(cbs);
        for (const auto& cb : cbs) {
            scheduler(cb);  // 调度定时器回调
        }

        // ========== 4. 处理就绪的IO事件 ==========
        for (int i = 0; i < ret; i++) {
            epoll_event& event = events[i];

            // 4.1 处理tickle管道（唤醒通知）
            if (event.data.fd == tickleFds_[0]) {
                uint8_t dummy[256];
                while (read(tickleFds_[0], dummy, sizeof(dummy)) > 0);
                continue;
            }

            // 4.2 获取FdContext
            FdContext* fd_ctx = (FdContext*)event.data.ptr;
            Mutex::Lock lock(fd_ctx->mutex);

            // 4.3 处理错误事件
            if (event.events & (EPOLLERR | EPOLLHUP)) {
                event.events |= (EPOLLIN | EPOLLOUT) & fd_ctx->events;
            }

            // 4.4 判断实际发生的事件类型
            int real_events = NONE;
            if (event.events & EPOLLIN)  real_events |= READ;
            if (event.events & EPOLLOUT) real_events |= WRITE;

            if ((fd_ctx->events & real_events) == NONE) {
                continue;  // 无匹配事件
            }

            // 4.5 从epoll中移除已触发的事件
            int left_events = (fd_ctx->events & ~real_events);
            int op = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            event.events = EPOLLET | left_events;
            epoll_ctl(epfd_, op, fd_ctx->fd, &event);

            // 4.6 触发事件（调度对应的协程或回调）
            if (real_events & READ) {
                fd_ctx->triggerEvent(READ);
                --pendingEventCnt_;
            }
            if (real_events & WRITE) {
                fd_ctx->triggerEvent(WRITE);
                --pendingEventCnt_;
            }
        }

        // ========== 5. idle协程让出CPU ==========
        Fiber::ptr cur = Fiber::GetThis();
        auto raw_ptr = cur.get();
        cur.reset();
        raw_ptr->yield();  // 让调度协程接管，执行就绪的任务
    }
}
```

---

#### 2.4 触发事件并调度协程

[iomanager.cpp:23-34](src/fiber/iomanager.cpp#L23-L34)

```cpp
void FdContext::triggerEvent(Event event) {
    // 1. 清除事件标记
    events = (Event)(events & ~event);

    // 2. 获取事件上下文
    EventContext& ctx = getEveContext(event);

    // 3. 调度协程或回调函数
    if (ctx.cb) {
        ctx.scheduler->scheduler(ctx.cb);  // 调度回调函数
    } else {
        ctx.scheduler->scheduler(ctx.fiber);  // 调度协程
    }

    // 4. 重置事件上下文
    resetEveContext(ctx);
}
```

---

### 3. Tickle机制：唤醒Idle协程

#### 3.1 为什么需要Tickle？

**场景：**
1. 所有工作线程都阻塞在`epoll_wait`（idle状态）
2. 此时有新的任务被添加到任务队列
3. **问题**：如何唤醒idle线程去执行新任务？

**解决方案：Tickle机制**

#### 3.2 实现原理

[iomanager.cpp:225-233](src/fiber/iomanager.cpp#L225-L233)

```cpp
void IOManager::tickle() {
    if (!isHasIdleThreads()) {
        return;  // 没有空闲线程，无需唤醒
    }

    // 向pipe写端写入数据
    int rt = write(tickleFds_[1], "T", 1);
    // pipe读端注册了EPOLLIN事件，epoll_wait会立即返回
}
```

**工作流程：**
```
新任务添加
    ↓
scheduler(task)
    ↓
判断需要tickle (tasks_.empty()变为false)
    ↓
write(tickleFds_[1], "T", 1)
    ↓
tickleFds_[0]可读
    ↓
epoll_wait返回
    ↓
idle协程yield
    ↓
调度协程接管，执行新任务
```

---

## Epoll与协程的协同工作

### 1. Hook机制：连接Epoll和协程的桥梁

#### 1.1 什么是Hook？

**Hook**就是拦截系统调用，替换为自定义实现。

**本项目Hook的函数：** [hook.cpp:13-34](src/fiber/hook.cpp#L13-L34)
```cpp
#define HOOK_FUN(XX)
  XX(sleep)       // 睡眠类
  XX(usleep)
  XX(nanosleep)

  XX(socket)      // socket创建
  XX(connect)     // 连接
  XX(accept)      // 接受连接

  XX(read)        // 读操作
  XX(readv)
  XX(recv)
  XX(recvfrom)
  XX(recvmsg)

  XX(write)       // 写操作
  XX(writev)
  XX(send)
  XX(sendto)
  XX(sendmsg)

  XX(close)       // 关闭
  XX(fcntl)       // 文件控制
  XX(ioctl)
  XX(getsockopt)  // socket选项
  XX(setsockopt)
```

#### 1.2 Hook实现原理

[hook.cpp:36-61](src/fiber/hook.cpp#L36-L61)

```cpp
// 1. 保存原始系统调用的函数指针
extern "C" {
    typedef unsigned int (*sleep_fun)(unsigned int);
    sleep_fun sleep_f = nullptr;

    typedef ssize_t (*read_fun)(int, void*, size_t);
    read_fun read_f = nullptr;
    // ... 其他函数
}

// 2. 在main之前初始化，获取原始函数地址
void hook_init() {
    static bool is_inited = false;
    if (is_inited) return;

    // dlsym：从动态链接库中获取符号地址
    #define XX(name) name##_f = (name##_fun)dlsym(RTLD_NEXT, #name);
    HOOK_FUN(XX);
    #undef XX
}

// 3. 静态初始化，main之前执行
struct _HOOKIniter {
    _HOOKIniter() { hook_init(); }
};
static _HOOKIniter s_hook_initer;

// 4. 线程局部变量，控制是否启用hook
static thread_local bool t_hook_enable = false;
```

---

#### 1.3 IO函数Hook模板

[hook.cpp:67-137](src/fiber/hook.cpp#L67-L137)

```cpp
template<typename OriginFun, typename... Args>
static ssize_t do_io(int fd, OriginFun fun, const char* hook_fun_name,
                     uint32_t event, int timeout_so, Args&&... args) {

    // ========== 1. 检查是否启用hook ==========
    if (!t_hook_enable) {
        return fun(fd, std::forward<Args>(args)...);  // 直接调用原始函数
    }

    // ========== 2. 获取fd上下文 ==========
    FdCtx::ptr ctx = FdMgr::GetInstance()->get(fd);
    if (!ctx || ctx->isClose() || !ctx->isSocket() || ctx->getUserNonblock()) {
        return fun(fd, std::forward<Args>(args)...);  // 不需要hook
    }

    // ========== 3. 获取超时时间 ==========
    uint64_t to = ctx->getTimeout(timeout_so);
    std::shared_ptr<timer_info> tinfo(new timer_info);

retry:
    // ========== 4. 尝试执行IO操作 ==========
    ssize_t n = fun(fd, std::forward<Args>(args)...);

    // 4.1 被信号中断，重试
    while (n == -1 && errno == EINTR) {
        n = fun(fd, std::forward<Args>(args)...);
    }

    // 4.2 数据未就绪（非阻塞socket返回EAGAIN）
    if (n == -1 && errno == EAGAIN) {
        IOManager* iom = IOManager::GetThis();
        Timer::ptr timer;

        // ========== 5. 添加超时定时器 ==========
        if (to != (uint64_t)-1) {
            timer = iom->addConditionTimer(to, [=]() {
                auto t = winfo.lock();
                if (!t || t->cnacelled) return;
                t->cnacelled = ETIMEDOUT;  // 标记超时
                iom->cancelEvent(fd, (Event)(event));  // 取消IO事件
            }, winfo);
        }

        // ========== 6. 注册IO事件到epoll ==========
        int rt = iom->addEvent(fd, (Event)(event));
        if (rt) {
            if (timer) timer->cancel();
            return -1;
        }

        // ========== 7. 协程让出CPU ==========
        Fiber::GetThis()->yield();
        // 线程会去执行其他协程或idle（epoll_wait）

        // ========== 8. 被唤醒后检查结果 ==========
        if (timer) timer->cancel();

        if (tinfo->cnacelled) {
            errno = tinfo->cnacelled;  // 超时错误
            return -1;
        }

        // 事件就绪，重新尝试IO
        goto retry;
    }

    // ========== 9. IO成功，返回结果 ==========
    return n;
}
```

---

#### 1.4 具体Hook实现示例

**Sleep Hook：** [hook.cpp:149-162](src/fiber/hook.cpp#L149-L162)
```cpp
unsigned int sleep(unsigned int seconds) {
    if (!t_hook_enable) {
        return sleep_f(seconds);  // 调用原始sleep
    }

    // Hook版本：添加定时器，协程yield
    Fiber::ptr fiber = Fiber::GetThis();
    IOManager* iom = IOManager::GetThis();

    // 添加定时器，seconds秒后调度本协程
    iom->addTimer(seconds * 1000, [=]() {
        iom->scheduler(fiber);
    });

    // 让出CPU
    Fiber::GetThis()->yield();
    return 0;
}
```

**Read Hook：**
```cpp
ssize_t read(int fd, void* buf, size_t count) {
    return do_io(fd, read_f, "read", READ, SO_RCVTIMEO, buf, count);
}
```

**Write Hook：**
```cpp
ssize_t write(int fd, const void* buf, size_t count) {
    return do_io(fd, write_f, "write", WRITE, SO_SNDTIMEO, buf, count);
}
```

---

### 2. 完整工作流程图

```
用户代码: recv(fd, buf, len)
         ↓
Hook拦截: hook::recv()
         ↓
尝试读取: recv_f(fd, buf, len)
         ↓
返回EAGAIN (数据未就绪)
         ↓
添加事件: iom->addEvent(fd, READ)
         ↓
注册epoll: epoll_ctl(epfd, EPOLL_CTL_ADD, fd, event)
         ↓
协程yield: Fiber::GetThis()->yield()
         |
         |  [协程让出CPU，线程执行其他协程或idle]
         |
         ↓
idle协程: epoll_wait(epfd, events, 256, timeout)
         |
         |  [等待IO事件或定时器超时...]
         |
         ↓
事件就绪: fd可读
         ↓
epoll_wait返回
         ↓
触发事件: fd_ctx->triggerEvent(READ)
         ↓
调度协程: scheduler->scheduler(fiber)
         ↓
idle协程yield
         ↓
原协程resume: swapcontext恢复执行
         ↓
重试读取: goto retry; recv_f(fd, buf, len)
         ↓
读取成功: 返回读取的字节数
         ↓
用户代码继续执行
```

---

### 3. RPC通信中的应用实例

#### 3.1 MprpcChannel连接过程

[mprpcchannel.cpp:26-50](src/rpc/mprpcchannel.cpp#L26-L50)

```cpp
MprpcChannel::MprpcChannel(string ip, short port, bool connectNow)
    : m_ip(ip), m_port(port), m_clientFd(-1),
      m_state(ConnectionState::HEALTHY) {

    if (connectNow) {
        std::string errMsg;
        auto rt = newConnect(ip.c_str(), port, &errMsg);

        // 重试3次
        int tryCount = 3;
        while (!rt && tryCount--) {
            rt = newConnect(ip.c_str(), port, &errMsg);
        }

        if (rt) {
            ScheduleHeartbeat();  // 启动心跳定时器
        } else {
            m_state.store(ConnectionState::DISCONNECTED);
        }
    }
}
```

#### 3.2 newConnect内部流程

```cpp
bool MprpcChannel::newConnect(const char* ip, uint16_t port, string* errMsg) {
    // 1. 创建socket（会被hook）
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientfd == -1) {
        *errMsg = "create socket error";
        return false;
    }

    // 2. 设置超时（5秒）
    struct timeval timeout = {5, 0};
    setsockopt(clientfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(clientfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    // 3. 连接服务器（会被hook）
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &server_addr.sin_addr);

    // connect被hook：如果连接未建立，协程会yield，等待EPOLLOUT事件
    if (connect(clientfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        close(clientfd);
        *errMsg = "connect error";
        return false;
    }

    m_clientFd = clientfd;
    m_state.store(ConnectionState::HEALTHY);
    return true;
}
```

**Hook工作过程：**
1. `connect()`被hook拦截
2. 底层调用`connect_f()`返回`EINPROGRESS`
3. Hook添加`EPOLLOUT`事件到epoll
4. 协程yield，等待连接建立
5. 连接建立后，epoll触发`EPOLLOUT`
6. 协程resume，`connect()`返回成功

---

## 技术对比分析

### 1. 传统多线程 vs 协程模型

| 对比维度 | 传统多线程模型 | 协程模型（本项目） |
|---------|--------------|-----------------|
| **并发单元** | 线程（1-8MB栈） | 协程（128KB栈） |
| **并发数量** | 受限于线程数（通常几十到几百） | 可达10万+ |
| **上下文切换** | 1-10微秒（内核态） | 10-100纳秒（用户态） |
| **切换开销** | 高（需要进入内核、切换页表、刷新TLB） | 低（仅保存/恢复寄存器） |
| **调度器** | 内核调度器 | 用户态调度器 |
| **编程模型** | 回调函数或阻塞线程 | 同步写法，异步执行 |
| **内存占用** | 100线程 ≈ 100MB+ | 10000协程 ≈ 1.2GB |
| **创建开销** | 毫秒级 | 微秒级 |

**场景对比：**

#### 场景1：处理10000个并发连接

**传统线程池（100线程）：**
```cpp
ThreadPool pool(100);
for (int i = 0; i < 10000; i++) {
    pool.submit([=]() {
        // 阻塞等待IO
        recv(fd, buf, len);  // 线程阻塞，无法处理其他连接
    });
}
// 问题：100个线程无法同时处理10000个连接，大量请求需要排队
```

**协程模型（3线程 + 10000协程）：**
```cpp
IOManager iom(3);  // 仅3个工作线程
for (int i = 0; i < 10000; i++) {
    iom.scheduler([=]() {
        recv(fd, buf, len);  // 协程yield，线程可以执行其他协程
    });
}
// 优势：3个线程通过协程切换，可以同时处理10000个连接
```

---

### 2. Select/Poll/Epoll对比

| 特性 | Select | Poll | Epoll |
|------|--------|------|-------|
| **数据结构** | bitmap | 数组 | 红黑树 + 就绪队列 |
| **最大连接数** | 1024（FD_SETSIZE） | 无限制（受内存限制） | 无限制（受内存限制） |
| **fd拷贝** | 每次调用都要拷贝整个fd_set | 每次调用都要拷贝整个pollfd数组 | 无需拷贝，内核维护 |
| **检测就绪方式** | 遍历所有fd | 遍历所有fd | 仅返回就绪的fd |
| **时间复杂度** | O(n) | O(n) | O(1) - 就绪fd数量 |
| **工作模式** | 水平触发 | 水平触发 | 水平触发 + 边缘触发 |
| **性能** | 差 | 较差 | 优秀 |

**为什么本项目选择Epoll？**
1. **高并发**：Raft集群可能有成百上千个连接
2. **高性能**：O(1)复杂度获取就绪事件
3. **边缘触发**：减少系统调用次数
4. **Linux专属**：项目运行在Linux环境

---

### 3. 回调模型 vs 协程模型

#### 回调模型（Callback Hell）

```cpp
void handleRequest() {
    asyncConnect(ip, port, [](Result r1) {
        if (!r1.success) {
            handleError();
            return;
        }

        asyncSend(request, [](Result r2) {
            if (!r2.success) {
                handleError();
                return;
            }

            asyncRecv([](Result r3) {
                if (!r3.success) {
                    handleError();
                    return;
                }

                asyncProcess(r3.data, [](Result r4) {
                    // 层层嵌套...
                });
            });
        });
    });
}
```

**问题：**
- 代码嵌套层次深，难以阅读
- 错误处理重复
- 调试困难（异步调用栈）
- 状态管理复杂

---

#### 协程模型（本项目）

```cpp
void handleRequest() {
    // 同步写法，异步执行
    if (!connect(ip, port)) {
        handleError();
        return;
    }

    if (send(request) < 0) {
        handleError();
        return;
    }

    if (recv(response) < 0) {
        handleError();
        return;
    }

    process(response);
}
```

**优势：**
- 代码扁平化，逻辑清晰
- 错误处理统一
- 调试友好（同步调用栈）
- 无需手动管理状态机

---

## 完整工作流程示例

### 场景：Raft Leader发送心跳

#### 1. 初始化阶段

```cpp
// raft.cpp:1027
m_ioManager = std::make_unique<monsoon::IOManager>(3, true);

// 调度心跳协程
m_ioManager->scheduler([this]() {
    this->leaderHearBeatTicker();
});
```

**此时状态：**
- 创建了IOManager，包含3个工作线程
- 启动了epoll事件循环
- 心跳协程被添加到任务队列

---

#### 2. 心跳协程执行

```cpp
void Raft::leaderHearBeatTicker() {
    while (true) {
        // Step 1: 睡眠50ms（被hook）
        usleep(50 * 1000);

        if (m_status == Leader) {
            // Step 2: 向所有Follower发送心跳
            for (int i = 0; i < peers.size(); i++) {
                if (i == m_me) continue;
                doHeartBeat(i);
            }
        }
    }
}
```

---

#### 3. usleep被Hook

```cpp
// hook.cpp:164
int usleep(useconds_t usec) {
    if (!t_hook_enable) {
        return usleep_f(usec);
    }

    // ===== Hook处理 =====
    Fiber::ptr fiber = Fiber::GetThis();  // 获取当前协程
    IOManager* iom = IOManager::GetThis();

    // 添加50ms定时器
    iom->addTimer(50, [=]() {
        iom->scheduler(fiber);  // 50ms后重新调度本协程
    });

    // 协程让出CPU
    Fiber::GetThis()->yield();
    // 线程去执行其他任务...

    return 0;
}
```

**定时器管理：**
- 定时器插入到红黑树（按超时时间排序）
- `idle()`中会计算最近的超时时间
- `epoll_wait(timeout)`的timeout参数使用最近超时时间

---

#### 4. Idle协程等待

```cpp
// iomanager.cpp:264
void IOManager::idle() {
    while (true) {
        uint64_t next_timeout = getNextTimer();  // 获取最近定时器：50ms

        // 等待IO事件或定时器超时
        int ret = epoll_wait(epfd_, events, 256, 50);  // 50ms

        // 50ms后超时返回
        if (ret == 0) {
            // 处理超时定时器
            std::vector<std::function<void()>> cbs;
            listExpiredCb(cbs);  // 获取所有超时的定时器回调

            for (const auto& cb : cbs) {
                scheduler(cb);  // 重新调度心跳协程
            }
        }

        // idle协程yield，让调度协程执行任务
        Fiber::GetThis()->yield();
    }
}
```

---

#### 5. 心跳协程恢复，发送RPC

```cpp
void Raft::doHeartBeat(int i) {
    // 构造AppendEntries请求
    AppendEntriesArgs args;
    args.set_term(m_currentTerm);
    args.set_leaderid(m_me);
    // ...

    // 发送RPC（异步）
    AppendEntriesReply reply;
    bool ok = peers[i]->AppendEntries(&args, &reply);

    if (ok) {
        // 处理响应
    }
}
```

---

#### 6. RPC调用：CallMethod

```cpp
// mprpcchannel.cpp
void MprpcChannel::CallMethod(...) {
    // 1. 序列化请求
    std::string args_str;
    if (!request->SerializeToString(&args_str)) {
        controller->SetFailed("serialize request error");
        return;
    }

    // 2. 发送数据（被hook）
    int send_size = send(m_clientFd, send_buf, send_buf_len, 0);
    if (send_size == -1) {
        controller->SetFailed("send error");
        close(m_clientFd);
        m_clientFd = -1;
        return;
    }

    // 3. 接收响应（被hook）
    char recv_buf[1024] = {0};
    int recv_size = recv(m_clientFd, recv_buf, 1024, 0);
    if (recv_size == -1) {
        controller->SetFailed("recv error");
        close(m_clientFd);
        m_clientFd = -1;
        return;
    }

    // 4. 反序列化响应
    if (!response->ParseFromArray(recv_buf, recv_size)) {
        controller->SetFailed("parse response error");
        return;
    }
}
```

---

#### 7. send被Hook

```cpp
// hook.cpp
ssize_t send(int sockfd, const void* buf, size_t len, int flags) {
    return do_io(sockfd, send_f, "send", WRITE, SO_SNDTIMEO, buf, len, flags);
}

template<typename OriginFun, typename... Args>
static ssize_t do_io(int fd, OriginFun fun, ...) {
    // 尝试发送
    ssize_t n = send_f(fd, buf, len, flags);

    if (n == -1 && errno == EAGAIN) {
        // 发送缓冲区满，添加WRITE事件
        IOManager* iom = IOManager::GetThis();

        // 注册到epoll
        iom->addEvent(fd, WRITE);
        // -> epoll_ctl(epfd, EPOLL_CTL_ADD, fd, EPOLLOUT | EPOLLET)

        // 协程yield
        Fiber::GetThis()->yield();
        // 等待socket可写...

        // 被唤醒后重试
        goto retry;
    }

    return n;
}
```

---

#### 8. Epoll检测到socket可写

```cpp
// iomanager.cpp:idle()
int ret = epoll_wait(epfd_, events, 256, timeout);

for (int i = 0; i < ret; i++) {
    if (events[i].events & EPOLLOUT) {
        FdContext* fd_ctx = (FdContext*)events[i].data.ptr;

        // 触发WRITE事件
        fd_ctx->triggerEvent(WRITE);
        // -> scheduler->scheduler(fiber)  调度send协程
    }
}

// idle协程yield
Fiber::GetThis()->yield();
```

---

#### 9. send协程恢复，重试发送

```cpp
// hook.cpp:do_io() resume到这里
retry:
    ssize_t n = send_f(fd, buf, len, flags);  // 重新发送
    // 此时socket可写，发送成功
    return n;  // 返回发送字节数
```

---

#### 10. 时序图

```
Time  Thread-1          Fiber-Heartbeat         IOManager          Epoll
  |      |                    |                     |                |
  0      |--- schedule ------>|                     |                |
  |      |                    |--- usleep(50ms) --->|                |
  |      |                    |    (hook)           |                |
  |      |                    |<-- yield ------     |                |
  |      |                    |                     |                |
  |      |<-- idle协程执行 ---|                     |                |
  |      |-------------------- epoll_wait(50ms) --->|                |
  |      |                    |                     |  等待50ms      |
 50ms    |                    |                     |<- timeout --   |
  |      |<-- 定时器超时 -----|                     |                |
  |      |--- schedule ------>|                     |                |
  |      |                    |<-- resume -----     |                |
  |      |                    |                     |                |
  |      |                    |--- doHeartBeat() -->|                |
  |      |                    |    send(data)       |                |
  |      |                    |    (hook)           |                |
  |      |                    |                     |-- epoll_ctl -->|
  |      |                    |<-- yield ------     |   ADD WRITE    |
  |      |                    |                     |                |
  |      |<-- idle协程执行 ---|                     |                |
  |      |-------------------- epoll_wait() ------->|                |
  |      |                    |                     |  等待可写      |
  |      |                    |                     |<- EPOLLOUT -   |
  |      |<-- socket可写 -----|                     |                |
  |      |--- schedule ------>|                     |                |
  |      |                    |<-- resume -----     |                |
  |      |                    |--- send成功 --->    |                |
  |      |                    |--- recv(resp) ----->|                |
  |      |                    |    (hook)           |                |
  |      |                    |                     |-- epoll_ctl -->|
  |      |                    |<-- yield ------     |   ADD READ     |
  |      |                    |                     |                |
  |      |<-- idle协程执行 ---|                     |                |
  |      |-------------------- epoll_wait() ------->|                |
  |      |                    |                     |  等待可读      |
  |      |                    |                     |<- EPOLLIN --   |
  |      |<-- socket可读 -----|                     |                |
  |      |--- schedule ------>|                     |                |
  |      |                    |<-- resume -----     |                |
  |      |                    |--- recv成功 --->    |                |
  |      |                    |--- 处理响应 --->    |                |
  |      |                    |                     |                |
  |      |                    |--- usleep(50ms) --->|  循环...       |
  |      |                    |<-- yield ------     |                |
```

---

## 性能优势分析

### 1. 量化对比

#### 场景：10000个并发Raft节点心跳

**传统多线程模型：**
```
线程数量：10000（每个连接一个线程）
内存占用：10000 * 2MB = 20GB
上下文切换：10000次/秒 * 5微秒 = 50ms/秒（CPU损耗）
```

**协程模型（本项目）：**
```
线程数量：3（固定工作线程）
协程数量：10000
内存占用：3 * 2MB + 10000 * 128KB ≈ 1.3GB
上下文切换：10000次/秒 * 50纳秒 = 0.5ms/秒（CPU损耗）
```

**性能提升：**
- **内存占用**：减少93.5%（20GB → 1.3GB）
- **上下文切换开销**：减少99%（50ms → 0.5ms）
- **并发能力**：提升100倍+

---

### 2. 实测数据（理论估算）

#### 吞吐量测试

**传统阻塞IO + 线程池（100线程）：**
```
并发连接：100（线程池限制）
QPS：约5000（每个请求20ms，100线程并发）
CPU利用率：40%（大量线程阻塞）
```

**协程 + Epoll + Hook：**
```
并发连接：10000+
QPS：约50000（每个请求20ms，但有10000个协程并发）
CPU利用率：85%+（几乎无阻塞）
```

---

### 3. 资源利用率

#### CPU利用率

```
传统模型：
- 线程阻塞在recv/send
- CPU空闲等待IO
- 利用率：30-50%

协程模型：
- IO等待时协程yield
- 线程执行其他协程
- 利用率：80-95%
```

#### 内存利用率

```
传统模型：
- 每线程固定2MB栈
- 大量栈空间未使用
- 利用率：10-20%

协程模型：
- 按需分配128KB栈
- 动态扩容
- 利用率：60-80%
```

---

## 常见面试问题深入解答

### Q0: ⚠️ 常见误区：协程是用于阻塞函数的吗？

**❌ 错误理解：**
> "协程主要用于阻塞函数这种阻塞的地方来进行线程层面协程的切换"

**这个回答有三个问题：**

#### 问题1："协程用于阻塞函数" ❌

**准确理解：**
- 协程**不是用于阻塞函数**
- 而是通过**Hook机制将阻塞IO转换为非阻塞异步IO**

**工作原理：**
```cpp
// 用户代码（看起来是阻塞的）
int n = recv(fd, buf, len);  // 同步写法

// Hook内部实现
ssize_t recv(int fd, void* buf, size_t len) {
    // 1. 尝试读取
    ssize_t n = recv_f(fd, buf, len);  // 调用真正的系统调用

    // 2. 如果数据未就绪（非阻塞socket返回-1，errno=EAGAIN）
    if (n == -1 && errno == EAGAIN) {
        IOManager* iom = IOManager::GetThis();

        // 3. 注册fd到epoll，监听EPOLLIN事件
        iom->addEvent(fd, READ);

        // 4. 协程yield，让出CPU给其他协程
        Fiber::GetThis()->yield();
        // 【关键】此时线程不阻塞，而是去执行其他协程

        // 5. 当数据到达，epoll触发，协程resume到这里
        // 6. 重新尝试读取
        goto retry;
    }

    // 7. 读取成功，返回
    return n;
}
```

**关键点：**
- recv本身还是会"阻塞"的（如果数据未就绪）
- 但Hook拦截后，**线程不会阻塞**
- 线程会去执行其他协程，直到数据到达

---

#### 问题2："线程层面的协程切换" ❌

**这个说法容易混淆！正确理解：**

| 对比项 | 线程切换 | 协程切换 |
|--------|---------|---------|
| **发生层次** | 内核态 | **用户态** |
| **切换者** | 内核调度器 | 用户态Scheduler |
| **需要系统调用** | 是 | **否** |
| **是否阻塞线程** | 是 | **否**（关键！） |
| **成本** | 1-10微秒 | 10-100纳秒 |

**准确表述：**
```
❌ "线程层面协程的切换"
✅ "在用户态进行协程切换，避免线程阻塞"
✅ "协程yield时，线程继续运行其他协程"
✅ "多个协程复用同一个线程，避免线程上下文切换"
```

**形象比喻：**
```
传统多线程模型：
Thread1 → recv()阻塞 → 线程睡眠 → CPU空闲
Thread2 → recv()阻塞 → 线程睡眠 → CPU空闲
Thread3 → recv()阻塞 → 线程睡眠 → CPU空闲
【问题】线程阻塞 → CPU利用率低

协程模型：
Thread1运行多个协程：
  Fiber1 → recv()未就绪 → yield → Thread1继续运行
  Fiber2 → send()未就绪 → yield → Thread1继续运行
  Fiber3 → 正在计算   → 占用CPU
  Fiber4 → sleep等待  → yield → Thread1继续运行
  ...
【优势】线程永不阻塞 → CPU利用率高
```

---

#### 问题3：缺少"为什么要用协程"的核心价值

**✅ 完整准确的回答应该是：**

> **协程在本项目中的作用是：**
>
> **1. 将阻塞IO转换为非阻塞异步IO**
> - 当IO未就绪时，协程yield让出CPU
> - 线程继续执行其他协程，而不是阻塞等待
> - IO就绪后，epoll触发事件，协程resume
> - **关键价值**：一个线程可以同时处理数千个并发连接
>
> **2. 简化异步编程**
> - 代码按同步方式编写（`recv() → send() → process()`）
> - 底层自动转换为异步执行
> - 避免回调地狱
>
> **3. 高并发低成本**
> - 每个协程仅128KB栈（线程2MB）
> - 协程切换仅50纳秒（线程切换5微秒）
> - 可以轻松创建10万+协程
>
> **4. 定时任务调度**
> - Raft心跳、选举超时都用协程定时器
> - 一个线程可以管理成千上万个定时器

---

### 🎯 面试标准答案模板

**问：协程主要用在哪里？**

```
在我的Raft项目中，协程有三个核心应用场景：

【场景1：网络IO】
当调用recv/send等IO函数时，如果数据未就绪，Hook机制会：
1. 将fd注册到epoll监听
2. 当前协程yield，让出CPU
3. 线程去执行其他协程（关键：线程不阻塞）
4. 当IO就绪，epoll触发事件，协程resume继续执行

这样一个线程可以同时处理数千个并发连接，而传统线程池只能处理几十个。

【场景2：定时任务】
Raft的心跳定时器(50ms)和选举超时(150-300ms)都用协程实现。
usleep被hook后，会添加定时器并yield，到期后自动resume。
一个线程可以管理成千上万个定时器协程。

【场景3：RPC调用】
每个RPC请求都在独立协程中执行，connect/send/recv被hook后自动异步化。
代码按同步方式写，底层自动转换为异步执行，避免了回调地狱。

【核心价值】
- 内存：10000协程约1.3GB，10000线程需20GB
- 性能：协程切换50ns，线程切换5us，快100倍
- 并发：轻松支持10万+并发，线程模型只能几百
- 编程：同步写法+异步执行，代码清晰易维护
```

---

### Q1: 协程和线程的区别是什么？

| 维度 | 线程 | 协程 |
|------|------|------|
| **调度者** | 内核调度 | 用户态调度器 |
| **切换成本** | 1-10微秒（需要进入内核） | 10-100纳秒（用户态） |
| **栈空间** | 1-8MB（固定） | 128KB（可配置） |
| **并发数** | 受限（几百到几千） | 几万到几十万 |
| **同步机制** | 互斥锁、信号量 | 协程本身就是同步的 |
| **是否抢占** | 抢占式（内核强制切换） | 协作式（主动yield） |

---

### Q2: 协程的栈空间是如何管理的？

**本项目实现：**
```cpp
// fiber.cpp
Fiber::Fiber(std::function<void()> cb, size_t stackSize, bool run_in_scheduler)
    : cb_(cb), isRunInScheduler_(run_in_scheduler) {

    // 默认栈大小128KB
    stackSize_ = stackSize ? stackSize : 128 * 1024;

    // 分配栈空间
    stack_ptr = malloc(stackSize_);

    // 初始化ucontext
    getcontext(&ctx_);
    ctx_.uc_stack.ss_sp = stack_ptr;
    ctx_.uc_stack.ss_size = stackSize_;
    ctx_.uc_link = nullptr;

    // 设置协程入口函数
    makecontext(&ctx_, &Fiber::MainFunc, 0);
}

Fiber::~Fiber() {
    if (stack_ptr) {
        free(stack_ptr);  // 释放栈空间
    }
}
```

**要点：**
1. 每个协程独立栈空间（128KB）
2. 使用`malloc`分配，协程销毁时释放
3. `ucontext`保存栈指针、栈大小
4. 切换时自动切换栈空间

---

### Q3: epoll的边缘触发和水平触发有什么区别？

**水平触发（Level Triggered, LT）：**
```cpp
// 只要socket缓冲区有数据，epoll_wait就会返回
epoll_event ev;
ev.events = EPOLLIN;  // 水平触发

// 场景：
// 1. socket接收100字节
// 2. epoll_wait返回
// 3. 读取50字节
// 4. epoll_wait再次返回（还有50字节未读）
// 5. 读取剩余50字节
```

**边缘触发（Edge Triggered, ET）：**
```cpp
// 仅在socket状态变化时触发
epoll_event ev;
ev.events = EPOLLIN | EPOLLET;  // 边缘触发

// 场景：
// 1. socket接收100字节
// 2. epoll_wait返回
// 3. 读取50字节
// 4. epoll_wait不会返回（状态未变化）
// 5. 必须在第3步读完所有数据！

// 正确做法：
while (true) {
    int n = recv(fd, buf, sizeof(buf), 0);
    if (n == -1 && errno == EAGAIN) {
        break;  // 读完了
    }
    // 处理数据
}
```

**本项目选择边缘触发的原因：**
1. **性能更高**：减少epoll_wait触发次数
2. **避免惊群**：多线程场景下更高效
3. **配合协程**：读取时yield，其他协程执行，提高并发

---

### Q4: Hook机制的原理是什么？有什么局限性？

**原理：**
1. **动态链接**：程序启动时，通过`dlsym(RTLD_NEXT, "read")`获取libc中`read`的地址
2. **符号覆盖**：提供自定义的`read`函数，链接时优先使用自定义版本
3. **条件执行**：通过`thread_local bool t_hook_enable`控制是否hook

**局限性：**
1. **仅hook标准库函数**：自定义系统调用无法hook
2. **需要动态链接**：静态链接无效
3. **线程局部控制**：某些库可能禁用hook
4. **调试困难**：调用栈可能混乱

---

### Q5: 如果协程数量非常多（10万+），调度器如何高效调度？

**本项目的优化策略：**

1. **任务队列**：使用`std::list`，O(1)插入/删除
```cpp
std::list<SchedulerTask> tasks_;
```

2. **无锁优化**：读多写少场景使用`RWMutex`
```cpp
RWMutex::ReadLock lock(mutex_);  // 读锁
RWMutex::WriteLock lock(mutex_); // 写锁
```

3. **Tickle优化**：仅在有空闲线程时tickle
```cpp
if (!isHasIdleThreads()) {
    return;  // 无需唤醒
}
```

4. **Epoll批处理**：一次处理256个事件
```cpp
epoll_wait(epfd_, events, 256, timeout);
```

5. **定时器红黑树**：O(logN)插入/删除
```cpp
std::set<Timer::ptr, Timer::Comparator> timers_;
```

---

### Q6: 项目中如何保证协程安全？

**1. 协程本身是协作式的**
```cpp
// 同一时刻，一个线程只运行一个协程
// 协程主动yield才会切换
Fiber::GetThis()->yield();
```

**2. 共享数据使用互斥锁**
```cpp
class Scheduler {
    Mutex mutex_;  // 保护任务队列
    std::list<SchedulerTask> tasks_;
};
```

**3. FdContext使用锁保护**
```cpp
class FdContext {
    Mutex mutex;  // 保护事件上下文

    void triggerEvent(Event event) {
        Mutex::Lock lock(mutex);
        // ...
    }
};
```

**4. 原子变量**
```cpp
std::atomic<size_t> pendingEventCnt_;
std::atomic<ConnectionState> m_state;
```

---

### Q7: 如果某个协程阻塞了（比如死循环），会影响其他协程吗？

**会！协程是协作式调度。**

**场景：**
```cpp
iom->scheduler([]() {
    while (true) {
        // 死循环，不yield
        calculate();
    }
});
```

**后果：**
- 该协程占用线程，不yield
- 同线程的其他协程无法执行
- 其他线程的协程不受影响

**解决方案：**
1. **定期yield**
```cpp
while (true) {
    calculate();
    if (should_yield()) {
        Fiber::GetThis()->yield();
    }
}
```

2. **使用定时器**
```cpp
void longTask() {
    while (!done) {
        doWork();
        usleep(1000);  // 内部会yield
    }
}
```

3. **独立线程**
```cpp
// applierTicker使用独立线程，避免阻塞协程
std::thread t3(&Raft::applierTicker, this);
t3.detach();
```

---

### Q8: 本项目的协程模型有什么缺点？

**1. 仅支持Linux**
- 依赖epoll（Linux专属）
- ucontext在某些平台已废弃

**2. 不支持抢占**
- 协程必须主动yield
- 死循环会阻塞线程

**3. 调试困难**
- 协程切换导致调用栈复杂
- GDB调试体验差

**4. Hook有局限**
- 仅hook标准库函数
- 第三方库可能绕过hook

**5. 学习曲线陡峭**
- 需要理解ucontext、epoll、hook
- 异步编程思维需要转变

---

## 总结

### 核心要点

1. **协程用在哪里？**
   - Raft定时任务（心跳、选举）
   - RPC网络通信（所有socket操作）
   - 高并发请求处理

2. **为什么用协程？**
   - 轻量级（128KB vs 2MB）
   - 高性能（100ns vs 5us切换）
   - 编程友好（同步写法，异步执行）
   - 高并发（10万+ vs 几百）

3. **Epoll用了吗？**
   - 是的！Epoll是整个异步IO的底层基础
   - 在IOManager::idle()中循环调用epoll_wait
   - 边缘触发模式(EPOLLET)提升性能

4. **Epoll和协程的区别？**
   - Epoll：内核态IO多路复用，监控fd事件
   - 协程：用户态轻量级线程，执行业务逻辑
   - 关系：Epoll驱动协程调度（事件就绪→触发协程）

### 技术架构总览

```
┌─────────────────────────────────────────────────────────┐
│                     Raft Application                    │
│  (leaderHearBeatTicker, electionTimeOutTicker, RPC...)  │
└────────────────────┬────────────────────────────────────┘
                     │ 业务逻辑层
                     ↓
┌─────────────────────────────────────────────────────────┐
│                   Monsoon Coroutine Library             │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  │
│  │   Scheduler  │  │  IOManager   │  │ TimerManager │  │
│  │ (协程调度器) │  │(IO事件管理器)│  │ (定时器管理) │  │
│  └──────────────┘  └──────────────┘  └──────────────┘  │
│         ↑                  ↑                  ↑          │
│         │                  │                  │          │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  │
│  │    Fiber     │  │  FdContext   │  │    Timer     │  │
│  │ (协程上下文) │  │ (文件描述符) │  │  (定时器)    │  │
│  └──────────────┘  └──────────────┘  └──────────────┘  │
└────────────────────┬────────────────────────────────────┘
                     │ 协程层
                     ↓
┌─────────────────────────────────────────────────────────┐
│                      Hook Layer                         │
│  (sleep, usleep, socket, connect, read, write, ...)    │
│  阻塞IO → 协程yield + Epoll事件注册 → 协程resume       │
└────────────────────┬────────────────────────────────────┘
                     │ Hook层
                     ↓
┌─────────────────────────────────────────────────────────┐
│                    Linux Kernel                         │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  │
│  │    Epoll     │  │   Socket     │  │    Timer     │  │
│  │ (事件通知)   │  │  (网络IO)    │  │  (timerfd)   │  │
│  └──────────────┘  └──────────────┘  └──────────────┘  │
└─────────────────────────────────────────────────────────┘
                     内核层
```

### 最佳实践建议

1. **何时使用协程？**
   - 高并发IO密集型应用
   - 需要大量定时器
   - 希望简化异步编程

2. **何时不用协程？**
   - CPU密集型计算（线程池更合适）
   - 跨平台需求（协程库平台相关）
   - 简单应用（增加复杂度）

3. **性能优化建议：**
   - 使用边缘触发减少系统调用
   - 定期yield避免长时间占用线程
   - 合理设置协程栈大小
   - 使用连接池复用连接

---

**本文档涵盖了epoll和协程的所有核心知识点，足以应对大部分面试场景！**

如有疑问，欢迎继续深入探讨！
