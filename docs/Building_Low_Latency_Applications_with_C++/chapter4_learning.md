# 第四章：构建低延迟基础组件

> 教材：《Building Low Latency Applications with C++》  
> 阶段：第二阶段（第 4 章）⭐ 核心  
> 目标：实现低延迟系统常用的基础设施组件，为后续交易所和客户端策略打下实现基础。

---

## 一、本章学习目标

学完本章后，你应该能够回答：

1. 为什么线程亲和性（affinity）能降低延迟抖动？如何用 C++ 绑定线程到指定 CPU？
2. 动态内存分配（`new/delete`）为什么不适合热路径？内存池如何解决？
3. 什么是 lock-free？SPSC 和 MPSC 无锁队列分别适用于什么场景？
4. C++ 内存序（memory order）有哪几种？什么情况下必须用 `acquire/release` 或 `seq_cst`？
5. 异步日志框架为什么要避免锁和动态分配？常见设计是什么？
6. 网络 socket 封装中，非阻塞 IO 和 zero-copy 的基本思想是什么？
7. 这些基础组件如何组合成一个可运行的低延迟程序骨架？

---

## 二、为什么要学这一章？

第三章从 C++ 语言和编译器层面建立了优化意识。  
第四章进入**系统组件的实现**，这是全书的**核心基石**：

- 后续交易所的撮合引擎、行情发布、订单网关都要依赖这些组件；
- 客户端策略的行情处理、订单发送、风险管理也要依赖这些组件；
- 能否正确实现无锁队列、内存池、异步日志，直接决定系统的延迟上限。

> **核心思想**：低延迟系统不是用现成库简单拼装，而是对每一个关键组件都精确控制其内存、同步和调度行为。

---

## 三、学习路线（分 5 个小节）

### 第 1 小节：线程库与线程亲和性

#### 4.1.1 线程是低延迟系统的基本执行单元

现代 CPU 是多核的，低延迟程序需要：

- 把不同工作分配给不同线程；
- 让线程**长期运行在指定核心上**，减少调度抖动；
- 让线程尽量少进入内核态，少被中断和抢占。

#### 4.1.2 线程亲和性（Thread Affinity）

线程亲和性是指把线程绑定到特定的 CPU 核心上运行。

**为什么需要绑定？**

| 原因 | 说明 |
|---|---|
| 减少调度迁移 | 线程不会被操作系统随意迁移到其他核心 |
| 缓存热数据 | 线程长期运行在同一核心，L1/L2 缓存更有效 |
| 避免核心间干扰 | 不同线程不会争抢同一核心的执行资源 |
| 降低延迟抖动 | 调度不确定性减少，延迟更稳定 |

#### 4.1.3 Linux 下设置线程亲和性

Linux 使用 `pthread_setaffinity_np` 设置线程亲和性，它允许显式指定一个线程可以在哪些 CPU 核心上运行。

**代码示例**

```cpp
#include <iostream>
#include <thread>
#include <pthread.h>

void pin_thread_to_core(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    pthread_t current_thread = pthread_self();
    int rc = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
        std::cerr << "Failed to set affinity: " << rc << "\n";
    }
}

int main() {
    std::thread worker([]() {
        pin_thread_to_core(2);  // 绑定到 CPU 核心 2
        std::cout << "Worker running on core 2\n";
        // 执行关键任务...
    });

    worker.join();
    return 0;
}
```

编译：

```bash
g++ -std=c++17 -pthread -o affinity_demo affinity_demo.cpp
```

**代码逐段解释**

| 代码 | 作用 |
|---|---|
| `#include <pthread.h>` | 引入 POSIX 线程 API，提供 `pthread_setaffinity_np` |
| `cpu_set_t cpuset` | 定义一个 CPU 集合位图，每一位代表一个 CPU 核心 |
| `CPU_ZERO(&cpuset)` | 清空 CPU 集合，表示暂不允许任何核心 |
| `CPU_SET(core_id, &cpuset)` | 把 `core_id` 对应的位设为 1，允许该核心 |
| `pthread_self()` | 获取当前线程的 pthread 标识符 |
| `pthread_setaffinity_np(...)` | 为当前线程设置亲和性，`_np` 表示 GNU 扩展 |
| `rc != 0` 错误处理 | 核心号越界等情况会返回非零错误码 |

**关键 API 说明**

| API | 作用 |
|---|---|
| `CPU_ZERO(cpu_set_t*)` | 清空 CPU 集合 |
| `CPU_SET(int, cpu_set_t*)` | 把指定核心加入集合 |
| `CPU_CLR(int, cpu_set_t*)` | 把指定核心从集合移除 |
| `CPU_ISSET(int, const cpu_set_t*)` | 判断指定核心是否在集合中 |
| `pthread_setaffinity_np(pthread_t, size_t, const cpu_set_t*)` | 设置线程亲和性 |
| `pthread_getaffinity_np(pthread_t, size_t, cpu_set_t*)` | 查询线程当前亲和性 |

**绑定多个核心**

`cpu_set_t` 可以同时设置多个允许的核心：

```cpp
cpu_set_t cpuset;
CPU_ZERO(&cpuset);
CPU_SET(2, &cpuset);
CPU_SET(3, &cpuset);  // 允许线程在核心 2 或 3 上运行
pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
```

**为什么这段代码能降低延迟抖动？**

没有亲和性时，操作系统的调度器可能根据系统负载把线程在不同核心之间迁移：

- 每次迁移都要保存/恢复寄存器上下文；
- 原来核心上的 L1/L2 缓存全部失效；
- 新核心需要重新加载热数据，引入不可预测的延迟。

设置亲和性后：

- 线程固定在指定核心运行；
- L1/L2 缓存保持热状态；
- 调度不确定性减少，尾延迟（p99/p999）更稳定。

**使用注意事项**

| 注意点 | 说明 |
|---|---|
| 核心号从 0 开始 | `core_id = 0` 表示第一个核心 |
| 不要越界 | 传入不存在核心号时 `pthread_setaffinity_np` 返回 `EINVAL` |
| 配合 CPU 隔离 | 用 `isolcpus=2` 把核心 2 从普通调度中隔离，避免被其他任务抢占 |
| 关闭超线程 | 避免同物理核心上的另一个逻辑核抢占执行资源和污染缓存 |
| 平台差异 | 该 API 是 Linux/GNU 扩展；Windows 用 `SetThreadAffinityMask`；macOS 支持较弱 |
| 绑定不等于无中断 | 亲和性只限制调度到哪个核心，不能禁止中断和系统调用 |

**一句话总结**

> `pthread_setaffinity_np` 通过 `cpu_set_t` 位图告诉操作系统：这个线程只能在指定的 CPU 核心上运行。配合 CPU 隔离和关闭超线程，可以把关键线程“钉”在固定核心上，减少调度抖动和缓存失效，从而获得更稳定的延迟。

#### 4.1.4 隔离核心（CPU Isolation）

CPU 隔离（CPU Isolation）是线程亲和性的进一步升级：把某些 CPU 核心从 Linux 内核的普通调度中“拿出去”，专门留给低延迟关键线程，让它们几乎独占运行。

**启动参数示例**

```bash
# 启动参数示例：隔离核心 2,3,4,5
isolcpus=2,3,4,5 nohz_full=2,3,4,5 rcu_nocbs=2,3,4,5
```

**三个参数的作用**

| 参数 | 作用 |
|---|---|
| `isolcpus=2,3,4,5` | 内核调度器不会在这些核心上调度普通任务 |
| `nohz_full=2,3,4,5` | 关闭这些核心上的周期时钟中断（tickless） |
| `rcu_nocbs=2,3,4,5` | 把 RCU 回调移到其他核心执行 |

**参数详细解释**

1. **`isolcpus`**

   告诉内核：核心 2、3、4、5 不参与普通任务调度。系统启动后，默认进程不会跑到这些核心上，只有显式通过 `pthread_setaffinity_np` 或 `taskset` 绑定的线程才能使用。

   效果：关键线程可以独占这些核心，不会被其他用户态任务抢占。

2. **`nohz_full`**

   Linux 默认每秒会给每个核心发送很多次时钟中断（tick），用于进程统计、调度、定时器等。即使线程正在忙等处理关键任务，也会被这些中断打断，导致延迟抖动。

   `nohz_full` 关闭被隔离核心上的周期时钟中断，让关键线程可以更长时间不被打断，延迟更稳定。

   > 注意：`nohz_full` 通常需要和 `isolcpus` 一起使用，且至少保留一个非隔离核心（如核心 0）处理全局时钟和系统任务。

3. **`rcu_nocbs`**

   RCU（Read-Copy-Update）是 Linux 内核的同步机制，会触发 `rcuos`/`rcuob` 等回调线程。即使核心被 `isolcpus` 隔离，这些回调线程仍可能在上面运行。

   `rcu_nocbs` 把指定核心上的 RCU 回调迁移到其他核心，进一步减少被隔离核心上的内核活动。

**如何设置**

通常通过 bootloader（如 GRUB）在系统启动时传入：

```bash
# 编辑 /etc/default/grub
GRUB_CMDLINE_LINUX_DEFAULT="quiet isolcpus=2,3,4,5 nohz_full=2,3,4,5 rcu_nocbs=2,3,4,5"

# 更新 GRUB 配置
sudo update-grub

# 重启生效
sudo reboot
```

重启后验证：

```bash
# 查看当前内核启动参数
cat /proc/cmdline

# 查看被隔离的 CPU
cat /sys/devices/system/cpu/isolated
```

**完整使用流程**

1. 启动时通过 GRUB 隔离指定核心；
2. 程序中用 `pthread_setaffinity_np` 把关键线程绑定到隔离核心；
3. 关键线程独占运行：无普通任务竞争、无周期时钟中断、无 RCU 回调干扰。

**注意事项**

| 注意点 | 说明 |
|---|---|
| 内核版本要求 | `nohz_full` 和 `rcu_nocbs` 需要较新的内核版本和配置支持 |
| 保留非隔离核心 | 至少留一个核心（通常是核心 0）给系统和内核任务 |
| 不要隔离所有核心 | 否则后台任务、内核线程无法正常调度 |
| 配合关闭超线程 | 否则隔离的是一个逻辑核，同物理核另一个逻辑核仍会抢占资源和污染缓存 |
| 调试用具可能异常 | 隔离后某些系统工具或监控行为可能变化，需提前做好方案 |

**一句话总结**

> CPU 隔离是把特定核心从内核普通调度中“切出来”，配合 `nohz_full` 关闭时钟中断、`rcu_nocbs` 移走 RCU 回调，让低延迟关键线程在这些核心上几乎独占运行，最大程度减少调度抖动和中断干扰。

#### 4.1.5 超线程的取舍

超线程（Simultaneous Multithreading，SMT）是 CPU 的一种技术：一个物理核心向操作系统暴露为两个逻辑核心，两套指令流共享同一个物理核心的执行单元和缓存。

**超线程为什么能提高吞吐？**

单个线程往往无法 100% 占满物理核心内部的所有执行单元（ALU、FPU、加载/存储端口等）。超线程在同一个物理核上维护两套寄存器状态：

- 当线程 A 因 cache miss、分支预测失败、数据依赖等原因停顿时；
- 核心可以切换去执行线程 B，利用原本空闲的执行单元；
- 从而提高整体吞吐量。

**适合场景**：高并发 Web 服务、批处理、计算任务有较多等待时间的场景。

**超线程为什么对低延迟不利？**

低延迟系统关心的是单个关键任务的**最坏情况延迟**，而不是总吞吐。超线程会引入以下问题：

| 问题 | 说明 |
|---|---|
| 执行单元竞争 | 两个逻辑核共享 ALU、FPU、重排序缓冲区等，关键线程可能被另一个逻辑核抢占执行资源 |
| 缓存污染 | 共享 L1/L2 缓存，另一个逻辑核不断带入新数据，会把关键线程的热数据挤出 |
| 中断和内核活动叠加 | 另一个逻辑核上的系统调用、中断、内核调度会干扰同物理核上的关键线程 |
| 延迟不可预测 | 另一个逻辑核在空闲还是重计算，直接决定关键线程的延迟表现 |

**低延迟场景下的三种做法**

| 方案 | 做法 | 适用场景 |
|---|---|---|
| BIOS 关闭超线程 | 在 BIOS/UEFI 中禁用 Hyper-Threading/SMT | 超低延迟交易系统，追求最稳定延迟 |
| 隔离时只用一个逻辑核 | 隔离部分逻辑核，并确保其兄弟核跑非关键任务 | 无法关闭超线程时的折中 |
| 关键线程独占物理核心 | 把关键线程绑定到一个逻辑核，普通任务绑定到另一个兄弟核 | 混合型系统，兼顾吞吐和延迟 |

**如何查看逻辑核与物理核的对应关系**

```bash
lscpu -e
```

输出示例：

```
CPU NODE SOCKET CORE L1d:L1i:L2:L3 ONLINE    MAXMHZ   MINMHZ      MHZ
  0    0      0    0 0:0:0:0          yes    4900.0000 800.0000 1200.0000
  1    0      0    0 0:0:0:0          yes    4900.0000 800.0000 1200.0000
  2    0      0    1 1:1:1:0          yes    4900.0000 800.0000 1200.0000
  3    0      0    1 1:1:1:0          yes    4900.0000 800.0000 1200.0000
```

这里 CPU 0 和 1 属于同一个物理核心（CORE 0），CPU 2 和 3 属于 CORE 1。如果关键线程绑定到 CPU 2，就应确保 CPU 3 不跑关键任务。

**超线程与 CPU 隔离的配合**

| 组合 | 效果 |
|---|---|
| 关闭超线程 + `isolcpus=2,3,4,5` | 核心 2–5 完全独占，延迟最稳定 |
| 开启超线程 + `isolcpus=2,4,6,8` | 只隔离部分逻辑核，需确保兄弟核不跑关键任务 |
| 开启超线程 + `isolcpus=2,3,4,5` | 若 2/3、4/5 是兄弟核，会互相干扰，效果打折扣 |

**是否应该绝对关闭超线程？**

不一定。超线程对吞吐型负载有益，关键是根据场景权衡：

| 场景 | 建议 |
|---|---|
| 超低延迟交易系统 | 关闭超线程，关键线程独占物理核心 |
| 高吞吐 Web 服务器 | 开启超线程，提高并发处理能力 |
| 混合型系统 | 关键线程独占部分物理核心，其余核心开启超线程跑普通任务 |
| 开发/测试环境 | 可以开启，便于复现多线程问题 |

**一句话总结**

> 超线程用共享执行单元和缓存换取总吞吐，但会让同物理核上的两个逻辑核心互相干扰。低延迟系统追求确定性延迟，因此关键线程应独占物理核心，通常选择关闭超线程或精心规划绑定策略，避免同核兄弟逻辑核跑关键任务。

```cpp
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <pthread.h>
#include <sched.h>

void pin_to_core(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}

int get_current_core() {
    return sched_getcpu();
}

int main() {
    const int iterations = 1'000'000;

    // 不绑定核心
    std::cout << "=== 不绑定核心 ===\n";
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        volatile int core = get_current_core();
        (void)core;
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    std::cout << "耗时: " << us << " us\n";

    // 绑定核心
    std::cout << "\n=== 绑定到核心 2 ===\n";
    pin_to_core(2);
    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        volatile int core = get_current_core();
        (void)core;
    }
    end = std::chrono::high_resolution_clock::now();
    us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    std::cout << "耗时: " << us << " us, current core: " << get_current_core() << "\n";

    return 0;
}
```

> **思考点**：
> 1. 为什么线程亲和性能降低延迟抖动？
> 2. `isolcpus` 和 `nohz_full` 分别解决什么问题？
> 3. 为什么低延迟场景常建议关闭超线程？

<details>
<summary><b>思考点答案</b></summary>

1. **为什么线程亲和性降低抖动？**
   - 操作系统调度器可能根据负载把线程迁移到不同核心，每次迁移都要保存/恢复状态，并导致缓存失效。
   - 绑定核心后，线程基本固定在一个核心运行，调度不确定性减少，L1/L2 缓存保持热状态。
   - 结果是延迟更稳定，尾延迟（p99/p999）显著改善。

2. **isolcpus 和 nohz_full 的作用**
   - `isolcpus`：让内核调度器不在这几个核心上调度普通任务，保证关键线程独占核心。
   - `nohz_full`：关闭这些核心上的周期时钟滴答中断，减少内核定时器中断对关键线程的干扰。
   - 两者配合，可以把关键线程的“软中断/调度”干扰降到最低。

3. **为什么关闭超线程？**
   - 超线程两个逻辑核共享物理执行单元和 L1/L2 缓存。
   - 如果同物理核上的另一个逻辑核在跑其他任务，会抢占执行资源、污染缓存，导致关键线程延迟不确定。
   - 超低延迟系统常关闭 SMT，让每个物理核心只跑一个关键线程。

</details>

---

### 第 2 小节：内存池与对象复用

#### 4.2.1 动态内存分配的问题

在热路径上调用 `new`/`delete` 有以下问题：

| 问题 | 说明 |
|---|---|
| 非确定性延迟 | 内存分配器需要查找空闲块，复杂时可能触发系统调用 |
| 锁竞争 | 默认分配器（如 ptmalloc）使用全局锁，多线程竞争激烈 |
| 内存碎片 | 长期运行后堆碎片化，分配效率下降 |
| 缓存不友好 | 新分配内存可能不在缓存中，访问延迟高 |
| TLB 压力 | 频繁分配释放导致页表抖动 |

#### 4.2.2 内存池的基本思想

内存池预先分配一大块连续内存，然后自己管理这块内存的分配和回收。

```
预先分配: [block][block][block][block][block][block]...
          ↑free_list

分配时: 从 free_list 取一个 block
释放时: 把 block 放回 free_list
```

**核心优势**：

- 分配/释放都是 O(1)；
- 不需要进入内核；
- 无锁设计：每个线程一个内存池，或整个池用一个锁（但尽量不在热路径使用）；
- 内存连续，缓存友好。

#### 4.2.3 固定大小内存池设计

```cpp
#include <iostream>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <new>

class FixedMemoryPool {
public:
    explicit FixedMemoryPool(size_t block_size, size_t num_blocks)
        : block_size_(block_size), num_blocks_(num_blocks) {
        // 分配大块内存
        pool_ = new char[block_size * num_blocks];

        // 初始化 free_list
        free_list_.reserve(num_blocks);
        for (size_t i = 0; i < num_blocks; ++i) {
            free_list_.push_back(pool_ + i * block_size);
        }
    }

    ~FixedMemoryPool() {
        delete[] pool_;
    }

    // 禁止拷贝
    FixedMemoryPool(const FixedMemoryPool&) = delete;
    FixedMemoryPool& operator=(const FixedMemoryPool&) = delete;

    void* allocate() {
        if (free_list_.empty()) {
            return nullptr;  // 池耗尽
        }
        void* block = free_list_.back();
        free_list_.pop_back();
        return block;
    }

    void deallocate(void* block) {
        if (!block) return;
        free_list_.push_back(static_cast<char*>(block));
    }

    size_t available() const { return free_list_.size(); }

private:
    size_t block_size_;
    size_t num_blocks_;
    char* pool_;
    std::vector<char*> free_list_;
};

struct Order {
    uint64_t id;
    double price;
    uint32_t quantity;
    char symbol[8];
};

int main() {
    FixedMemoryPool pool(sizeof(Order), 1024);

    // 分配
    Order* o1 = static_cast<Order*>(pool.allocate());
    o1->id = 1;
    o1->price = 100.5;
    o1->quantity = 100;

    std::cout << "Available after alloc: " << pool.available() << "\n";

    // 释放
    pool.deallocate(o1);
    std::cout << "Available after free: " << pool.available() << "\n";

    return 0;
}
```

#### 4.2.4 线程局部内存池

多线程场景下，全局内存池仍有锁竞争。更好的做法是每个线程维护独立的内存池：

```cpp
#include <thread>
#include <memory>

thread_local std::unique_ptr<FixedMemoryPool> tls_pool;

void* thread_local_alloc() {
    if (!tls_pool) {
        tls_pool = std::make_unique<FixedMemoryPool>(sizeof(Order), 1024);
    }
    return tls_pool->allocate();
}
```

> 注意：线程局部池释放的内存不能由其他线程回收，除非设计跨线程归还机制。

#### 4.2.5 内存对齐

某些硬件或 SIMD 指令要求内存按特定边界对齐。C++11 起可用 `alignas`：

```cpp
struct alignas(64) CacheAlignedCounter {
    int64_t value = 0;
};
```

内存池分配时也应保证对齐：

```cpp
void* aligned_allocate(size_t size, size_t alignment) {
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) != 0) {
        return nullptr;
    }
    return ptr;
}
```

#### 4.2.6 代码示例：对比 new/delete 与内存池

```cpp
#include <iostream>
#include <chrono>
#include <vector>
#include <random>

#include "fixed_memory_pool.h"  // 假设上面内存池定义在此

const int N = 1'000'000;

struct Packet {
    char data[128];
};

int main() {
    using namespace std::chrono;

    // 系统 new/delete
    {
        std::vector<Packet*> ptrs;
        ptrs.reserve(N);

        auto start = high_resolution_clock::now();
        for (int i = 0; i < N; ++i) {
            ptrs.push_back(new Packet());
        }
        for (auto* p : ptrs) {
            delete p;
        }
        auto end = high_resolution_clock::now();
        auto us = duration_cast<microseconds>(end - start).count();
        std::cout << "new/delete: " << us << " us\n";
    }

    // 内存池
    {
        FixedMemoryPool pool(sizeof(Packet), N);
        std::vector<void*> ptrs;
        ptrs.reserve(N);

        auto start = high_resolution_clock::now();
        for (int i = 0; i < N; ++i) {
            ptrs.push_back(pool.allocate());
        }
        for (auto* p : ptrs) {
            pool.deallocate(p);
        }
        auto end = high_resolution_clock::now();
        auto us = duration_cast<microseconds>(end - start).count();
        std::cout << "memory pool: " << us << " us\n";
    }

    return 0;
}
```

> **思考点**：
> 1. 内存池为什么比 `new/delete` 快？
> 2. 固定大小内存池有什么局限性？如何设计变长内存池？
> 3. 线程局部内存池如何解决多线程竞争？又有什么新限制？

<details>
<summary><b>思考点答案</b></summary>

1. **内存池为什么更快？**
   - `new/delete` 需要维护全局堆结构，可能涉及锁、查找、合并、系统调用。
   - 内存池预先分配好连续内存，分配/释放只是链表/栈操作，O(1) 且不进内核。
   - 内存池的内存布局连续，缓存命中率高，TLB 也更稳定。

2. **固定大小内存池的局限**
   - 只能分配固定大小的对象，无法处理变长请求。
   - 池大小固定，可能耗尽。
   - 改进方案：
     - 按大小分级（size-class）的内存池，如 16、32、64、128 字节等级别；
     - 每个级别一个固定大小内存池；
     - 大块请求直接 fallback 到系统分配器。

3. **线程局部内存池**
   - 每个线程有独立池，完全避免线程间锁竞争。
   - 限制：一个线程分配的内存不能由另一个线程释放到该池中，否则可能破坏结构或引发竞争。
   - 解决方案：使用 per-thread cache + 全局 pool 的两级设计；或者让对象生命周期明确归属某线程。

</details>

---

### 第 3 小节：无锁队列

#### 4.3.1 为什么需要无锁队列？

低延迟系统中，不同线程之间需要传递数据，常见方式：

| 方式 | 问题 |
|---|---|
| 共享数据 + 互斥锁 | 锁竞争、上下文切换、内核态开销 |
| 条件变量 | 线程唤醒延迟不确定 |
| 无锁队列 | 无锁、无上下文切换、确定性高 |

无锁队列是低延迟系统的**核心通信机制**。

#### 4.3.2 SPSC vs MPSC

| 类型 | 全称 | 特点 | 适用场景 |
|---|---|---|---|
| **SPSC** | Single Producer Single Consumer | 一个生产者、一个消费者，实现最简单，性能最好 | 网络线程 → 业务线程、策略线程 → 订单线程 |
| **MPSC** | Multi Producer Single Consumer | 多个生产者、一个消费者，需要处理多生产者竞争 | 多个行情源聚合、多个客户端订单入队 |
| **SPMC** | Single Producer Multi Consumer | 较少见 | 广播场景 |
| **MPMC** | Multi Producer Multi Consumer | 最复杂，性能最低 | 通用线程池等 |

本书核心场景使用 **SPSC** 和 **MPSC**。

#### 4.3.3 SPSC 无锁队列实现

基于环形缓冲区的 SPSC 无锁队列：

```cpp
#include <iostream>
#include <vector>
#include <atomic>
#include <cassert>
#include <new>

template<typename T, size_t Capacity>
class SPSCQueue {
public:
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

    SPSCQueue() : head_(0), tail_(0) {
        buffer_ = static_cast<T*>(aligned_allocate(sizeof(T) * Capacity, alignof(T)));
    }

    ~SPSCQueue() {
        // 简单示例：不逐个析构，实际应清空队列
        aligned_free(buffer_);
    }

    // 禁用拷贝
    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    bool push(const T& value) {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (current_tail + 1) & (Capacity - 1);

        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false;  // 队列满
        }

        buffer_[current_tail] = value;
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    bool pop(T& value) {
        const size_t current_head = head_.load(std::memory_order_relaxed);

        if (current_head == tail_.load(std::memory_order_acquire)) {
            return false;  // 队列空
        }

        value = buffer_[current_head];
        head_.store((current_head + 1) & (Capacity - 1), std::memory_order_release);
        return true;
    }

    bool empty() const {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

private:
    T* buffer_;
    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;

    static void* aligned_allocate(size_t size, size_t alignment) {
        void* ptr = nullptr;
        if (posix_memalign(&ptr, alignment, size) != 0) {
            throw std::bad_alloc();
        }
        return ptr;
    }

    static void aligned_free(void* ptr) {
        free(ptr);
    }
};

int main() {
    SPSCQueue<int, 1024> queue;

    queue.push(42);
    queue.push(100);

    int v;
    while (queue.pop(v)) {
        std::cout << "Pop: " << v << "\n";
    }

    return 0;
}
```

#### 4.3.4 内存序（Memory Order）

C++11 提供 6 种内存序：

| 内存序 | 含义 | 使用场景 |
|---|---|---|
| `memory_order_relaxed` | 只保证原子性，无顺序约束 | 计数器、不需要同步的场景 |
| `memory_order_consume` | 依赖同步（实际很少单独使用） | 学术研究为主 |
| `memory_order_acquire` | 之后的读写不能重排到这次读之前 | 消费者读取共享数据时 |
| `memory_order_release` | 之前的读写不能重排到这次写之后 | 生产者写入共享数据后 |
| `memory_order_acq_rel` | 同时包含 acquire 和 release | read-modify-write 操作 |
| `memory_order_seq_cst` | 顺序一致性，最强 | 默认，最简单但最慢 |

**SPSC 队列中的内存序解释**：

- `push` 中，先写 `buffer_[current_tail] = value`，再用 `release` 更新 `tail_`。
  - 保证：消费者通过 `acquire` 读到新 `tail_` 时，一定能看到写入的 `value`。
- `pop` 中，先用 `acquire` 读 `tail_`，再读 `buffer_[current_head]`。
  - 保证：读到新 `tail_` 后，读取 buffer 不会看到旧值。

#### 4.3.5 MPSC 无锁队列思路

MPSC 比 SPSC 复杂，因为多个生产者会竞争写入。

常见实现：

1. **每个生产者一个 SPSC + 消费者轮询**
   - 最简单，无锁，性能好。
   - 适合生产者数量固定且不多的场景。

2. **基于 atomic CAS 的环形队列**
   - 多个生产者用 CAS 竞争 tail 位置。
   - 实现复杂，ABA 问题需要注意。

3. **Michael-Scott 队列**
   - 经典 MPMC 无锁链表队列。
   - 每次分配节点，不适合极高频场景。

本书交易所常用：**每个连接/模块一个 SPSC，最终汇聚到少量消费者**。

#### 4.3.6 代码示例：SPSC 无锁队列 benchmark

```cpp
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>

#include "spsc_queue.h"  // 假设上面 SPSCQueue 定义在此

const int N = 10'000'000;

int main() {
    SPSCQueue<int, 65536> queue;
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};

    using namespace std::chrono;

    auto start = high_resolution_clock::now();

    std::thread producer([&]() {
        for (int i = 0; i < N; ++i) {
            while (!queue.push(i)) {
                // 忙等待，实际可用 pause/yield
            }
        }
        produced.store(N, std::memory_order_release);
    });

    std::thread consumer([&]() {
        int value;
        int count = 0;
        while (count < N) {
            if (queue.pop(value)) {
                ++count;
            }
        }
        consumed.store(N, std::memory_order_release);
    });

    producer.join();
    consumer.join();

    auto end = high_resolution_clock::now();
    auto us = duration_cast<microseconds>(end - start).count();

    std::cout << "Sent " << produced.load() << ", received " << consumed.load() << "\n";
    std::cout << "Total time: " << us << " us\n";
    std::cout << "Ops/sec: " << (2.0 * N / us) << " million\n";

    return 0;
}
```

> **思考点**：
> 1. 为什么 SPSC 队列能做到无锁？`head_` 和 `tail_` 的对齐有什么用？
> 2. `memory_order_acquire` 和 `memory_order_release` 在 SPSC 队列中分别起什么作用？
> 3. 如果生产者调用 `push` 时发现队列满，应该忙等还是 yield？

<details>
<summary><b>思考点答案</b></summary>

1. **SPSC 为什么无锁？**
   - 只有一个生产者写 `tail_`，一个消费者读 `head_`。
   - 两者分别更新自己拥有的变量，不会竞争同一个原子变量。
   - 通过读取对方的变量判断队列空/满。
   - `head_` 和 `tail_` 用 `alignas(64)` 分开，避免伪共享。

2. **acquire/release 的作用**
   - `release` 保证：写入 buffer 的操作不会重排到更新 tail 之后。
   - `acquire` 保证：读取 tail 新值后，读取 buffer 的操作不会重排到读 tail 之前。
   - 两者配合，构成 producer-consumer 同步关系。

3. **队列满时该忙等还是 yield？**
   - 对延迟极其敏感的场景：忙等（spin），配合 `pause` 指令减少功耗。
   - 对 CPU 友好/队列偶尔满：可 `std::this_thread::yield()` 或条件变量。
   - 本书低延迟场景通常选择忙等，因为线程已被绑定到独占核心，yield 反而引入调度不确定性。

</details>

---

### 第 4 小节：异步日志框架

#### 4.4.1 为什么日志不能阻塞热路径？

日志是低延迟系统的重要组成部分，但直接写日志会：

| 问题 | 说明 |
|---|---|
| 磁盘 IO 慢 | 写磁盘是微秒到毫秒级，远慢于业务处理 |
| 同步锁 | 多线程共享日志文件需要互斥锁 |
| 格式化开销 | `std::ostringstream`、`printf` 等格式化耗时 |
| 动态内存 | 日志字符串可能触发堆分配 |

#### 4.4.2 异步日志设计

核心思想：

```
业务线程 → 写入 ring buffer → 日志线程 → 刷盘
```

- 业务线程只把日志内容拷贝到预分配的 ring buffer；
- 单独的日志线程负责格式化和写磁盘；
- 业务线程不直接做 IO、不加锁（或用无锁队列）。

#### 4.4.3 关键设计点

| 设计点 | 说明 |
|---|---|
| 预分配 buffer | 避免运行时分配 |
| 无锁 SPSC/MPSC | 业务线程 → 日志线程通信 |
| 二进制日志 | 只存结构化数据，延迟后再格式化 |
| 批量刷盘 | 日志线程批量写入，减少系统调用 |
| 双缓冲 | 一个 buffer 写，一个 buffer 刷，避免阻塞 |
| 日志降级 | buffer 满时丢弃日志，不阻塞业务 |

#### 4.4.4 简化版异步日志实现

```cpp
#include <iostream>
#include <thread>
#include <atomic>
#include <cstring>
#include <fstream>
#include <chrono>
#include <sstream>

class AsyncLogger {
public:
    static constexpr size_t BUFFER_SIZE = 1024 * 1024;  // 1 MB
    static constexpr size_t MSG_MAX_LEN = 256;

    AsyncLogger(const std::string& filename)
        : write_pos_(0), read_pos_(0), stop_(false) {
        buffer_ = new char[BUFFER_SIZE];
        file_.open(filename, std::ios::out | std::ios::app);
        worker_ = std::thread(&AsyncLogger::flush_loop, this);
    }

    ~AsyncLogger() {
        stop_.store(true, std::memory_order_release);
        worker_.join();
        delete[] buffer_;
    }

    // 业务线程调用：尝试写入日志
    bool log(const char* msg, size_t len) {
        if (len > MSG_MAX_LEN) return false;

        size_t current = write_pos_.load(std::memory_order_relaxed);
        size_t next = current + sizeof(size_t) + len;
        if (next >= read_pos_.load(std::memory_order_acquire) + BUFFER_SIZE) {
            return false;  // buffer 满，丢弃
        }

        // 写入长度
        std::memcpy(buffer_ + (current % BUFFER_SIZE), &len, sizeof(size_t));
        // 写入内容
        std::memcpy(buffer_ + ((current + sizeof(size_t)) % BUFFER_SIZE), msg, len);

        write_pos_.store(next, std::memory_order_release);
        return true;
    }

private:
    char* buffer_;
    std::atomic<size_t> write_pos_;
    std::atomic<size_t> read_pos_;
    std::atomic<bool> stop_;
    std::ofstream file_;
    std::thread worker_;

    void flush_loop() {
        while (!stop_.load(std::memory_order_acquire)) {
            size_t current_read = read_pos_.load(std::memory_order_relaxed);
            size_t current_write = write_pos_.load(std::memory_order_acquire);

            while (current_read < current_write) {
                size_t len;
                std::memcpy(&len, buffer_ + (current_read % BUFFER_SIZE), sizeof(size_t));

                char msg[MSG_MAX_LEN];
                std::memcpy(msg, buffer_ + ((current_read + sizeof(size_t)) % BUFFER_SIZE), len);
                msg[len] = '\0';

                file_ << msg << "\n";

                current_read += sizeof(size_t) + len;
            }

            read_pos_.store(current_read, std::memory_order_release);
            file_.flush();

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
};

int main() {
    AsyncLogger logger("app.log");

    for (int i = 0; i < 1000; ++i) {
        std::ostringstream oss;
        oss << "Log message " << i;
        std::string s = oss.str();
        logger.log(s.c_str(), s.size());
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return 0;
}
```

> 注意：上面是教学简化版，未处理 buffer 环绕时的边界情况。生产环境需要更严谨的边界处理或分块写入。

#### 4.4.5 日志字符串优化

| 优化 | 说明 |
|---|---|
| 避免 `std::string` / `std::ostringstream` | 格式化可能分配内存，延迟高 |
| 使用固定 buffer + `snprintf` | 栈分配，可控 |
| 二进制日志 | 只记录整数/枚举，事后解析 |
| 编译期过滤 | `LOG_LEVEL` 宏，低级别日志直接不编译 |
| 延迟格式化 | 把格式化推到日志线程，业务线程只拷贝 raw data |

#### 4.4.6 代码示例：同步日志 vs 异步日志对比

```cpp
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <sstream>

const int N = 100'000;

void sync_log(const std::string& filename) {
    std::ofstream file(filename);
    using namespace std::chrono;
    auto start = high_resolution_clock::now();
    for (int i = 0; i < N; ++i) {
        file << "Sync log message " << i << "\n";
    }
    auto end = high_resolution_clock::now();
    std::cout << "Sync: " << duration_cast<microseconds>(end - start).count() << " us\n";
}

int main() {
    sync_log("sync.log");
    // async_log 可用上面的 AsyncLogger 测试
    return 0;
}
```

> **思考点**：
> 1. 异步日志为什么能避免阻塞业务线程？
> 2. 二进制日志相比文本日志有什么优势和劣势？
> 3. 如果日志 buffer 满了，应该阻塞业务线程还是丢弃日志？

<details>
<summary><b>思考点答案</b></summary>

1. **为什么异步日志不阻塞业务线程？**
   - 业务线程只把日志内容放入预分配的 ring buffer，这是纯内存操作。
   - 真正的磁盘 IO、格式化由单独日志线程完成。
   - 两者通过无锁队列或原子索引通信，业务线程无需等待磁盘。

2. **二进制日志的优劣势**
   - **优势**：业务线程不需要格式化字符串，只拷贝结构化数据；体积小、解析快。
   - **劣势**：人类不可读，需要事后工具解析；调试时不如文本日志直观。
   - 超低延迟系统通常用二进制日志 + 离线解析工具。

3. **buffer 满时如何处理？**
   - 延迟敏感场景：**丢弃日志**，绝对不能阻塞业务线程。
   - 可选择：记录丢日志事件、扩大 buffer、提高日志线程优先级。
   - 阻塞会导致不可预测延迟，违背低延迟设计原则。

</details>

---

### 第 5 小节：网络 socket 封装

#### 4.5.1 低延迟网络的关键诉求

| 诉求 | 说明 |
|---|---|
| 低延迟 | 从收到数据到应用处理的时间尽量短 |
| 低抖动 | 延迟稳定，尾延迟小 |
| 高吞吐 | 能处理大量行情和订单 |
| 可控性 | 能控制 buffer、超时、CPU 绑定 |

#### 4.5.2 TCP vs UDP

| 特性 | TCP | UDP |
|---|---|---|
| 可靠性 | 可靠、有序 | 不可靠、无序 |
| 连接 | 面向连接 | 无连接 |
| 延迟 | 重传、拥塞控制可能引入抖动 | 更低、更可控 |
| 使用场景 | 订单网关、需要可靠传输 | 行情广播、可容忍丢包 |

本书交易所：

- **订单协议**：用 TCP，必须可靠送达。
- **行情协议**：可用 UDP 组播，追求最低延迟。

#### 4.5.3 非阻塞 IO

阻塞 socket 在 `recv()`/`send()` 时会挂起线程，无法做其他事。非阻塞 socket 在没数据时立即返回 `EAGAIN`/`EWOULDBLOCK`。

```cpp
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>
#include <iostream>

int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// 创建非阻塞 TCP socket
int create_nonblocking_socket() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    set_nonblocking(fd);
    return fd;
}
```

#### 4.5.4 epoll 事件驱动

Linux 下用 `epoll` 高效监听大量 socket：

```cpp
#include <sys/epoll.h>
#include <vector>

class EpollPoller {
public:
    EpollPoller() : epoll_fd_(epoll_create1(0)) {}
    ~EpollPoller() { close(epoll_fd_); }

    bool add_fd(int fd, uint32_t events) {
        epoll_event ev;
        ev.events = events;
        ev.data.fd = fd;
        return epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == 0;
    }

    int wait(std::vector<epoll_event>& events, int timeout_ms) {
        return epoll_wait(epoll_fd_, events.data(), events.size(), timeout_ms);
    }

private:
    int epoll_fd_;
};
```

#### 4.5.5 Zero-Copy 思想

Zero-copy 指尽量减少数据在用户态和内核态之间的拷贝。

| 技术 | 说明 |
|---|---|
| `sendfile` | 内核直接把文件数据发到 socket，不经过用户态 |
| `mmap` | 文件映射到用户态内存，减少拷贝 |
| `splice` | 管道零拷贝传输 |
| Kernel bypass | 网卡直接把数据送到用户态，本书后续可能涉及 |

本书交易所/客户端 socket 封装主要关注：

- 非阻塞 + epoll；
- 固定接收 buffer，避免重复分配；
- 消息解析直接在 buffer 上做，减少拷贝。

#### 4.5.6 代码示例：简单 Echo Server/Client

**Server**：

```cpp
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>

int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main() {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    set_nonblocking(listen_fd);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(12345);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    listen(listen_fd, 5);

    std::cout << "Echo server listening on 12345\n";

    char buffer[1024];
    while (true) {
        int client_fd = accept(listen_fd, nullptr, nullptr);
        if (client_fd < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        set_nonblocking(client_fd);

        int n = recv(client_fd, buffer, sizeof(buffer), 0);
        if (n > 0) {
            send(client_fd, buffer, n, 0);
        }
        close(client_fd);
    }

    close(listen_fd);
    return 0;
}
```

**Client**：

```cpp
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(12345);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    const char* msg = "Hello, low latency!";
    send(fd, msg, strlen(msg), 0);

    char buffer[1024];
    int n = recv(fd, buffer, sizeof(buffer), 0);
    buffer[n] = '\0';
    std::cout << "Received: " << buffer << "\n";

    close(fd);
    return 0;
}
```

编译：

```bash
g++ -std=c++17 -pthread -o echo_server echo_server.cpp
g++ -std=c++17 -o echo_client echo_client.cpp
```

> **思考点**：
> 1. 为什么低延迟系统倾向非阻塞 IO 而不是阻塞 IO？
> 2. TCP 和 UDP 分别在交易所的哪些环节使用？
> 3. Zero-copy 的核心目标是什么？epoll 为什么比 select/poll 更适合高并发？

<details>
<summary><b>思考点答案</b></summary>

1. **为什么用非阻塞 IO？**
   - 阻塞 IO 会让线程挂起，等待数据到达，期间无法处理其他事。
   - 非阻塞 IO 配合 epoll，一个线程可以监听大量 socket，有事件才处理。
   - 关键线程不会被单个慢连接阻塞，延迟更可控。

2. **TCP 和 UDP 的使用场景**
   - **TCP**：订单发送/接收，必须可靠、按顺序，不能丢单。
   - **UDP**：行情广播，追求最低延迟，允许少量丢包（后续行情会刷新）。
   - 有些系统也用 UDP 做订单，但需自己实现可靠性和重传，复杂度高。

3. **Zero-copy 与 epoll**
   - Zero-copy 目标是减少用户态 ↔ 内核态之间的数据拷贝，降低 CPU 和延迟。
   - epoll 使用红黑树和就绪链表，监听大量 socket 时效率仍为 O(1)（有事件时）。
   - select/poll 每次都要遍历所有 fd，fd 数量大时性能线性下降。

</details>

---

## 四、第四章实践任务清单

完成以下任务，才算真正掌握第四章：

### 必做

1. **线程亲和性实验**
   - 写一个程序创建多个线程，分别绑定到不同核心。
   - 用 `taskset` 或启动参数隔离核心后，观察调度稳定性。
   - 对比绑定和不绑定时的延迟分布（可用 `std::chrono` 测量循环耗时）。

2. **内存池实现与 benchmark**
   - 实现固定大小内存池。
   - 对比 `new/delete`、全局内存池、线程局部内存池的分配延迟。
   - 尝试让内存池支持对齐分配。

3. **SPSC 无锁队列实现与测试**
   - 实现基于环形缓冲区的 SPSC 无锁队列。
   - 用两个线程发送 1000 万条消息，测量延迟分布（p50/p99/p999）。
   - 改变 buffer 大小，观察吞吐变化。
   - 尝试把 `head_`/`tail_` 放同一缓存行 vs 分开对齐，观察性能差异。

4. **异步日志器实现**
   - 实现一个基础异步日志器：业务线程写 ring buffer，单独线程刷盘。
   - 测试高并发写入时业务线程的最大延迟。
   - 实现 buffer 满时丢弃策略，确保业务线程不被阻塞。

5. **Echo Server/Client**
   - 用非阻塞 socket + epoll 实现 echo server（可处理多个客户端）。
   - 实现 echo client，测量 RTT（Round-Trip Time）。
   - 对比阻塞版和非阻塞版的吞吐和延迟。

### 选做（加深理解）

6. **MPSC 无锁队列调研与实现**
   - 调研基于 CAS 的 MPSC 队列实现。
   - 或者用“每个生产者一个 SPSC + 消费者轮询”方案实现 MPSC。
   - 对比两种方案在多个生产者下的性能。

7. **无锁队列的内存序实验**
   - 把 SPSC 队列中的 `acquire/release` 改成 `relaxed`。
   - 在多核机器上运行，观察是否出现数据不一致或异常。
   - 理解为什么内存序不能随意削弱。

8. **日志格式化优化**
   - 用 `snprintf` 替代 `std::ostringstream` 生成日志字符串。
   - 对比两者在热路径上的耗时。
   - 尝试二进制日志格式，写一个离线解析工具。

9. **思考题**
   - 线程绑定核心后，如果核心数量少于线程数量，应该怎么设计？
   - 无锁队列的“无锁”是否意味着“无等待”？
   - 异步日志如果日志线程崩溃，已经写入 buffer 的日志会丢失吗？如何设计保证不丢？

---

## 五、常见误区与注意事项

| 误区 | 正确认识 |
|---|---|
| ❌ “无锁一定比有锁快” | ✅ 无锁避免的是锁竞争和内核切换，但复杂实现可能引入更多原子操作和重试 |
| ❌ “内存池可以完全替代 new/delete” | ✅ 内存池适合固定大小、高频分配的对象；复杂对象仍需系统分配器 |
| ❌ “异步日志不会丢日志” | ✅ buffer 满时通常会丢弃；需要专门设计持久化策略才不丢 |
| ❌ “非阻塞 IO 一定更好” | ✅ 简单场景下阻塞 IO 代码更简单；非阻塞 IO 适合高并发、低延迟 |
| ❌ “epoll 本身无延迟” | ✅ epoll_wait 仍有系统调用开销，极致场景会考虑 kernel bypass |
| ❌ “线程越多越好” | ✅ 线程过多增加调度开销；关键线程应少而精，绑定到隔离核心 |
| ❌ “memory_order_relaxed 够用了” | ✅ 错误使用 relaxed 会导致数据竞争和诡异 bug；必须理解 happens-before |

---

## 六、下章预告

第五章会进入**交易所生态系统设计**，包括：

- 交易所整体拓扑与组件划分
- 撮合引擎（Matching Engine）设计
- 限价订单簿（Limit Order Book）数据结构
- 市场数据发布（Market Data Publisher）
- 订单网关（Order Gateway）

第四章实现的基础组件（线程、内存池、无锁队列、日志、socket）将直接用于构建交易所。

---

## 七、推荐学习节奏

| 时间 | 内容 |
|---|---|
| 第 1–2 天 | 第 1 小节：线程库与线程亲和性 |
| 第 3–4 天 | 第 2 小节：内存池与对象复用 |
| 第 5–7 天 | 第 3 小节：无锁队列（重点） |
| 第 8–9 天 | 第 4 小节：异步日志框架 |
| 第 10–11 天 | 第 5 小节：网络 socket 封装 |
| 第 12–14 天 | 综合实践：把组件串成一个小程序骨架 |

> ⚠️ 第四章是全书基石，建议慢下来吃透，不要赶进度。

---

## 八、本章核心金句

> “低延迟系统的竞争力，往往不在业务逻辑，而在基础组件的实现质量。”

> “无锁不是炫技，而是为了避免内核态切换和不可预测的调度延迟。”

> “内存池的价值不是更快，而是让分配延迟变得可预测。”

> “异步日志的第一原则：绝不阻塞业务线程。”

> “线程亲和性不是优化，而是对调度不确定性的主动控制。”
