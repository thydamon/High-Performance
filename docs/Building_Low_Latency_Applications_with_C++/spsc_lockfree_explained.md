# SPSC 为什么无锁？—— 深度解析

> 目标：理解 SPSC 队列为什么连 CAS 都不需要，仅用 atomic load/store 就能实现线程安全的无锁通信。

---

## 一、"无锁"到底是什么意思？

首先澄清三个容易混淆的概念：

| 术语 | 含义 | 例子 |
|---|---|---|
| **无锁（lock-free）** | 不使用互斥锁（mutex），不会因为某个线程被挂起而阻塞其他所有线程 | SPSC 队列、CAS 循环 |
| **无等待（wait-free）** | 更强：每个操作在有限步数内**一定完成**，不存在重试循环 | 原子计数器自增 |
| **无锁 ≠ 无原子操作** | 无锁编程大量使用 `std::atomic`，关键是不用 mutex 导致的内核态切换 | — |

SPSC 队列不仅是无锁的，它连 **CAS（Compare-And-Swap）都不需要**——只用普通的 atomic load/store 就完成了同步。这意味着没有重试循环，延迟完全确定。

---

## 二、核心秘密：一人一变量，永不争抢

SPSC 队列的"无锁"根基是一个简单的事实：

```
       生产者只写 tail_          消费者只写 head_
       生产者只读 head_          消费者只读 tail_

  ┌─────────────────┐     ┌─────────────────┐
  │    生产者        │     │     消费者       │
  │                 │     │                 │
  │ 写: tail_  ←─── │     │ 写: head_  ←─── │
  │ 读: head_  ────→│     │ 读: tail_  ────→│
  │                 │     │                 │
  │ tail_ 是我独占的！│     │ head_ 是我独占的！│
  │ 没人跟我抢 tail_  │     │ 没人跟我抢 head_  │
  └─────────────────┘     └─────────────────┘
```

**每个变量只有一个写者。** 这才是 SPSC 无锁的根本原因——没有两个线程会同时写同一个变量，所以不需要锁来保护，也不需要 CAS 来竞争。

---

## 三、对比：为什么其他方案会冲突？

### 有锁队列（最慢）

```
生产者                              消费者
  │                                  │
  ├─ lock(mutex)                     │
  │   ↓ 获得锁                       ├─ lock(mutex) → 阻塞！
  ├─ buffer[tail] = data             │   等待生产者释放锁...
  ├─ tail++                          │   可能被内核挂起 (1~5µs)
  ├─ unlock(mutex)                   │
  │                                  ├─ 获得锁
  │                                  ├─ data = buffer[head]
  │                                  ├─ head++
  │                                  └─ unlock(mutex)
```

**问题**：同一时刻只有一个线程能操作队列。争不到锁的线程被操作系统挂起，上下文切换开销 1–5 µs。

### MPSC 队列（需要 CAS）

```
生产者 A              生产者 B              消费者
  │                     │                    │
  ├─ 读 tail=5          │                    │
  │                     ├─ 读 tail=5         │
  │                     │                    │
  ├─ CAS(tail,5,6)     ├─ CAS(tail,5,6)    │
  │   ✓ 成功！位置5归我  │   ✗ 失败！          │
  │                     │   另一线程抢先      │
  │                     │   重试...           │
  │                     ├─ 读 tail=6         │
  │                     ├─ CAS(tail,6,7)    │
  │                     │   ✓ 成功！          │
```

**问题**：多个生产者竞争同一个 `tail_`，必须用 CAS 原子地"抢位置"。CAS 失败的线程要重试，形成忙等待，延迟不确定。

### SPSC 队列（纯 load/store，不需要 CAS）

```
生产者                              消费者
  │                                  │
  ├─ tail = load(tail_)  relaxed     │
  ├─ next = tail + 1                 │
  ├─ head = load(head_)  acquire     │
  ├─ if next == head → 满            │
  ├─ buffer[tail] = data             │
  ├─ store(tail_, next) release      │
  │                                  ├─ head = load(head_)  relaxed
  │                                  ├─ tail = load(tail_)  acquire
  │                                  ├─ if head == tail → 空
  │                                  ├─ data = buffer[head]
  │                                  └─ store(head_, head+1) release
```

**没有锁，没有 CAS，没有重试。** 因为 `tail_` 只有生产者在写，`head_` 只有消费者在写。它们各写各的，永远不会产生"两个线程同时改同一个变量"的冲突。

### 三种方案对比总结

```
╔═════════════════════════════════════════════════════════════╗
║                      有锁队列                               ║
╠═════════════════════════════════════════════════════════════╣
║  push: lock → 写数据 → tail++ → unlock                     ║
║  pop:  lock → 读数据 → head++ → unlock                     ║
║  问题: 锁 = 互斥 = 线程挂起 → 内核态切换 ~µs 级             ║
╠═════════════════════════════════════════════════════════════╣
║                      MPSC(M) 队列                           ║
╠═════════════════════════════════════════════════════════════╣
║  push: CAS(tail, old, old+1) → 抢位置 → 写数据              ║
║  问题: 多个生产者抢 tail_，CAS 失败必须重试 → 延迟不确定    ║
╠═════════════════════════════════════════════════════════════╣
║                       SPSC 队列                             ║
╠═════════════════════════════════════════════════════════════╣
║  push: tail_ 只有我写 → 读 → 写 → store 即可                ║
║  pop:  head_ 只有我写 → 读 → 写 → store 即可                ║
║  ✅  各写各的 → 零冲突 → 零重试 → 延迟完全确定              ║
╚═════════════════════════════════════════════════════════════╝
```

---

## 四、环形缓冲区：让读写各走各的道

SPSC 队列是一个**环形缓冲区**（Ring Buffer），head_ 和 tail_ 在环上追逐：

### 基本状态

```
初始化状态 (空):
  head_ = 0, tail_ = 0

  buffer: [ _ ][ _ ][ _ ][ _ ][ _ ][ _ ][ _ ][ _ ]
           ↑
        head_/tail_

生产者 push 3 个元素后:
  tail_ = 3

  buffer: [ A ][ B ][ C ][ _ ][ _ ][ _ ][ _ ][ _ ]
           ↑              ↑
         head_           tail_

消费者 pop 1 个元素后:
  head_ = 1

  buffer: [ _ ][ B ][ C ][ _ ][ _ ][ _ ][ _ ][ _ ]
               ↑         ↑
             head_     tail_

绕回 (wrap around):
  push 5 个 → tail_ 超过 Capacity-1=7，绕回
  push → tail_=0 → tail_=1

  buffer: [ X ][ Y ][ Z ][ _ ][ _ ][ W ][ U ][ V ]
               ↑              ↑
             tail_          head_
```

### 空和满的判断

```
空: head_ == tail_
满: (tail_ + 1) % Capacity == head_

为什么容量利用率是 Capacity - 1？
  如果放满 8 个: tail_ 绕回 = 0, head_ = 0
  tail_ == head_  →  空？满？无法区分！

  必须牺牲一个槽位来区分空和满。
```

### 为什么容量必须是 2 的幂？

```
Capacity = 8 (0b1000)

取模优化:
  index % 8  →  index & 7 (0b0111)

  9 % 8 = 1     →     9 & 7 = 0b1001 & 0b0111 = 0b0001 = 1  ✓
  8 % 8 = 0     →     8 & 7 = 0b1000 & 0b0111 = 0b0000 = 0  ✓

位与运算比取模运算快 10–20 倍，而且无分支，CPU 流水线友好。
```

代码中 `(current_tail + 1) & (Capacity - 1)` 就是这个优化。

---

## 五、操作时间线：并发场景下的完整分析

以生产者 push 和消费者 pop **同时发生**为例：

```
时间轴:
═══════════════════════════════════════════════════════════════

生产者线程 (push)                           消费者线程 (pop)

T1: tail = load(tail_) relaxed
    读: tail_ = 3
    生产者看到 3

T2:                                     head = load(head_) relaxed
                                        读: head_ = 1
                                        ↑ head_ 没有生产者写，安全

T3: next = 4
    head = load(head_) acquire
    读: head_ = 1
    4 != 1, 没满 → 可以写

T4: buffer_[3] = value  (写数据)         tail = load(tail_) acquire
                                        读: tail_ = 3
                                        1 != 3, 不空 → 可以读

T5: store(tail_, 4) release             buffer_[1] 读取数据 ← 安全！
    写: tail_ = 4                       和生产者写的不在同一个位置！
    ↑ 仅生产者改 tail_                  位置 3 和位置 1 完全不同

T6:                                     store(head_, 2) release
                                        写: head_ = 2
                                        ↑ 仅消费者改 head_
```

**关键观察**：

1. 生产者写 `buffer_[3]`，消费者读 `buffer_[1]`——buffer 的**不同槽位**，没有数据竞争；
2. 生产者写 `tail_`，消费者写 `head_`——**不同变量**，没有数据竞争；
3. 生产者读 `head_`，消费者读 `head_`——**读操作可以并发**，不会冲突；
4. 唯一"交叉"的是：生产者读 `head_` 判断是否满了，消费者读 `tail_` 判断是否空了。但**读对方写的变量是安全的**（配合 acquire/release 保证数据可见性）。

---

## 六、内存序：保证"先写数据，再更新指针"的可见性

"各写各的变量"解决了数据竞争，但还有一个问题：**CPU 和编译器会乱序执行**。

### 乱序的风险

```
生产者代码（逻辑顺序）:
  buffer_[tail] = value;    // 1. 先写数据
  tail_.store(next, ...);   // 2. 再更新 tail_ 指针

如果 CPU 乱序成:
  tail_.store(next, ...);   // 2. 先更新 → 消费者以为有数据了！
  buffer_[tail] = value;    // 1. 后写  → 但数据还没写进去！

结果: 消费者读到垃圾数据！
```

### acquire/release 配对如何解决

```
生产者 (push):                          消费者 (pop):
  buffer_[tail] = value;    ← 普通写       │
  ───── release 屏障 ─────                │    release: 之前的写操作
  tail_.store(next, release)               │    不能跨越这条线重排到后面
         │                                 │
         │  tail_ 从 3 变成 4              │
         │                                 │
         └────────────────────────────────→│   消费者通过 acquire 看到
                                           │   tail_ 的新值 4
                                           │
                              ───── acquire 屏障 ─────
                              tail_.load(acquire) 读到 4    acquire: 之后的读操作
                              data = buffer_[tail];  ← 普通读  不能跨越这条线重排到前面

合在一起的效果:
  消费者 acquire 读到生产者 release 写入的 tail_ 新值
  → 生产者在 release 之前对 buffer 的写入一定对消费者可见
  → 读到的 buffer_[tail] 一定是正确的数据
```

### 为什么读 head_ 用 acquire，读 tail_ 用 relaxed？

```cpp
// push 中:
const size_t current_tail = tail_.load(std::memory_order_relaxed);  // ① relaxed 读自己的 tail_
const size_t next_tail = (current_tail + 1) & (Capacity - 1);

if (next_tail == head_.load(std::memory_order_acquire)) {           // ② acquire 读对方的 head_
    return false;  // 满
}

buffer_[current_tail] = value;                                       // ③ 写数据
tail_.store(next_tail, std::memory_order_release);                   // ④ release 更新 tail_
```

| 操作 | 内存序 | 原因 |
|---|---|---|
| ① 读 tail_(自己的) | relaxed | 只有自己写，值不会"凭空变化"，不需要同步 |
| ② 读 head_(对方的) | acquire | 需要看到消费者最新写入的 head_，确保"满"判断正确 |
| ③ 写 buffer | 普通写 | 在 release 之前，被 release 保护 |
| ④ 写 tail_(通知对方) | release | 保证 buffer 写入先于 tail_ 更新，消费者看到新 tail_ 时一定也看到数据 |

```cpp
// pop 中:
const size_t current_head = head_.load(std::memory_order_relaxed);   // ⑤ relaxed 读自己的 head_

if (current_head == tail_.load(std::memory_order_acquire)) {         // ⑥ acquire 读对方的 tail_
    return false;  // 空
}

value = buffer_[current_head];                                       // ⑦ 读数据
head_.store((current_head + 1) & (Capacity - 1),                      // ⑧ release 更新 head_
            std::memory_order_release);
```

| 操作 | 内存序 | 原因 |
|---|---|---|
| ⑤ 读 head_(自己的) | relaxed | 只有自己写，不需要同步 |
| ⑥ 读 tail_(对方的) | acquire | 需要看到生产者最新写入的 tail_，确保能看到 buffer 中的数据 |
| ⑦ 读 buffer | 普通读 | 在 acquire 之后，被 acquire 保护 |
| ⑧ 写 head_(通知对方) | release | 保证 buffer 读取完成后再更新 head_，生产者看到新 head_ 时该槽已安全可写 |

---

## 七、`alignas(64)`：消除最后一种"隐藏冲突"

即使 `head_` 和 `tail_` 是**不同的变量**，如果它们在**同一个缓存行**（64 字节）里，CPU 的缓存一致性协议仍然会让它们互相拖慢：

### 伪共享：两个互不竞争的变量在缓存层面打架

```
无对齐 (发生伪共享):
  ┌────────────── 64 bytes 缓存行 ──────────────┐
  │  head_ (8B)  │  tail_ (8B)  │  padding ...  │
  └─────────────────────────────────────────────┘
        ↑               ↑
   消费者每次写      生产者每次写
   head_ 都会让     tail_ 也会让
   这条缓存行在     这条缓存行在
   生产者核心上     消费者核心上
   失效！            失效！

   每次写操作 → 对方的缓存行失效 → 对方下次读必须重载
   两个线程交替写 → 缓存行在两个核心之间来回弹跳
   → 延迟增加数倍

对齐后 (消除伪共享):
  ┌────── 缓存行 0 ──────┐  ┌────── 缓存行 1 ──────┐
  │  head_ (8B)  │ pad.. │  │  tail_ (8B)  │ pad.. │
  └──────────────────────┘  └──────────────────────┘
       ↑ 消费者独占               ↑ 生产者独占
       各自独立缓存行             各自独立缓存行
       互不干扰                   互不干扰
```

### 为什么是 64？

主流 x86-64 CPU 的缓存行大小是 **64 字节**。`alignas(64)` 把变量对齐到 64 字节边界，确保它独占一个完整的缓存行：

```cpp
alignas(64) std::atomic<size_t> head_;
alignas(64) std::atomic<size_t> tail_;
```

---

## 八、三段式总结

```
SPSC 无锁的三个支柱:

  ┌─────────────────────────────────────────────────────────────┐
  │  1. 一人一变量                                              │
  │     tail_ 只有生产者写，head_ 只有消费者写                    │
  │     → 零冲突，不需要锁，不需要 CAS                           │
  ├─────────────────────────────────────────────────────────────┤
  │  2. release/acquire 内存序                                  │
  │     生产者: 写数据 → release → 更新 tail_                    │
  │     消费者: acquire → 读 tail_ → 读数据                     │
  │     → 消费者看到新 tail_ 时一定看到完整数据                   │
  ├─────────────────────────────────────────────────────────────┤
  │  3. alignas(64) 消除伪共享                                  │
  │     head_ 和 tail_ 各占独立缓存行                            │
  │     → 生产者和消费者的写操作在缓存层面完全独立               │
  └─────────────────────────────────────────────────────────────┘

  结果: 零锁、零 CAS、零重试、零内核切换 —— 延迟完全确定
```

---

## 九、常见追问

**1. 如果消费者读 tail_ 时，生产者正好在更新它怎么办？**

`load(acquire)` 是原子操作——要么读到旧值，要么读到新值，不会读到"写了一半"的中间状态。如果读到旧值，消费者以为队列空，下次再读即可。如果读到新值，acquire 保证 buffer 数据也可见。

**2. 把 acquire/release 换成 relaxed 会怎样？**

`relaxed` 不提供顺序保证。CPU 可能把"写 buffer"重排到"更新 tail_"之后，导致消费者看到新 tail_ 时 buffer 数据还没写完——读到垃圾。在实际多核机器上，这种 bug 可能运行很久才出现一次，极难排查。

**3. SPSC 是 wait-free 吗？**

从生产者和消费者各自的角度看：push 永远在固定步数内完成（读两个原子变量 + 写 buffer + 写一个原子变量），pop 同理。它们从不重试。"满则返回 false"是业务层面的失败，不是算法层面的重试。所以 SPSC 可以被认为是 wait-free 的（前提是 buffer 容量足够，不会被"返回 false"阻塞业务逻辑）。

**4. 为什么 SPSC 不能扩展成 MPMC？**

因为 MPMC 时，多个生产者竞争同一个 tail_，多个消费者竞争同一个 head_。两个写者抢一个变量 → 必须 CAS → 必须重试 → 不再是 wait-free。这就是为什么 SPSC 是最简单也最快的原因——它利用了"一写一读"这个最强约束。

---

## 十、队列满时该忙等还是 yield？

SPSC 队列 `push` 失败（队列满）时，调用方需要决定：等，还是让？

```cpp
std::thread producer([&]() {
    for (int i = 0; i < N; ++i) {
        while (!queue.push(i)) {
            // ← 这里 ！队列满，三种选择
        }
    }
});
```

### 10.1 三种等待策略

| 策略 | 代码 | 行为 |
|---|---|---|
| **忙等 (spin)** | 什么都不做，空循环 | CPU 100% 占用，最快响应队列出现空位 |
| **忙等 + pause** | `_mm_pause()` | CPU 仍然忙等，但功耗略降且不霸占超线程执行单元 |
| **yield** | `std::this_thread::yield()` | 主动让出 CPU，操作系统调度其他线程 |

### 10.2 忙等 (spin) 详解

```
生产者 push 失败 (队列满):
  T=0ns:   load head_ → 满
  T=10ns:  load head_ → 还是满
  T=20ns:  load head_ → 还是满
  T=30ns:  load head_ → 消费者腾出位置了 → push 成功

总等待: ~30ns
```

优点：**响应延迟极低**。消费者一有空位，生产者在几个纳秒内就能感知。

缺点：CPU 核心 100% 占用，功耗高。如果队列长时间满（消费者持续慢于生产者），纯忙等浪费 CPU。

**`_mm_pause()` (PAUSE 指令) 的作用：**

```cpp
#include <emmintrin.h>

while (!queue.push(i)) {
    _mm_pause();  // x86 PAUSE 指令
}
```

| 效果 | 说明 |
|---|---|
| 降低功耗 | 告诉 CPU "我在忙等"，降低执行单元的功耗开销 |
| 不霸占执行单元 | 超线程场景下，同物理核的另一个逻辑核不会被饿死 |
| 避免内存序误判 | 退出 spin 循环时，避免 CPU 推测执行导致的 memory order mis-speculation 惩罚 |
| 延迟影响 | 每次 pause 增加约 10–40 个时钟周期 (~3–10 ns) |

> 低延迟最佳实践：**`_mm_pause()` 几乎零代价消除忙等的主要副作用，在 spin 循环中应该默认加上。**

### 10.3 yield 详解

```
生产者 push 失败 (队列满):
  T=0ns:        load head_ → 满
  T=0ns:        yield() → 系统调用 → 陷入内核
  T=500ns:      内核: 当前线程让出 CPU，调度其他线程
  T=1000ns:     内核: 该线程重新被调度
  T=1200ns:     用户态，load head_ → 空位了 → push 成功

总等待: ~1200ns (1.2µs)
```

对比忙等的 ~30ns，yield 慢了约 **40 倍**。

**yield 的风险：** `yield()` 是一个系统调用（`sched_yield`），涉及用户态→内核态切换。如果线程绑定到隔离核心：

- 操作系统可能调度一个不相关的线程到该核心；
- 该线程污染 L1/L2 缓存（原来关键线程的热数据被挤出）；
- 当前线程被重新调度时，缓存全是冷的；
- 延迟不确定性不降反增。

### 10.4 决策因素

| 因素 | 倾向于 spin | 倾向于 yield |
|---|---|---|
| 线程是否绑定隔离核心 | 是（核心闲着也是闲着） | 否（其他线程需要 CPU） |
| 队列满的频率 | 偶尔满（短 burst） | 经常满（消费者跟不上） |
| 延迟敏感度 | 超低延迟（< 100ns 预算） | 中等延迟（几百 µs 可接受） |
| 是否开启超线程 | 关闭超线程（无兄弟核被抢占） | 开启超线程（pause 也不够安全） |

### 10.5 决策矩阵

| 场景 | 推荐策略 | 理由 |
|---|---|---|
| 绑定隔离核心 + 超低延迟 + 偶尔满 | **spin + `_mm_pause()`** | 延迟最低，核心上无其他任务 |
| 绑定隔离核心 + 经常满 | **扩大 buffer / 降速生产者** | 等待策略只是掩盖问题 |
| 未绑定核心 + 低延迟 | **spin + `_mm_pause()`** | 几纳秒不值得让出 CPU |
| 未绑定核心 + 中等延迟 | **spin N 次 → yield** | 先尝试等，不行再让 |
| 非关键路径（日志/统计） | **yield 或条件变量** | CPU 效率优先 |

### 10.6 混合策略：自适应退避

生产环境中最实用的方式——先忙等一小段时间，如果一直满再逐级退让：

```cpp
bool try_push_with_backoff(SPSCQueue& queue, const Order& data) {
    // 第一层：快速 spin + pause（~10µs）
    for (int spin = 0; spin < 1000; ++spin) {
        if (queue.push(data)) return true;
        _mm_pause();
    }

    // 第二层：让出 CPU，给消费者执行机会
    for (int yield_round = 0; yield_round < 10; ++yield_round) {
        if (queue.push(data)) return true;
        std::this_thread::yield();
    }

    // 第三层：队列持续满——记录、丢弃或阻塞
    dropped_count_++;
    return false;
}
```

| 层 | 策略 | 持续时间 | 含义 |
|---|---|---|---|
| 第一层 | spin + pause ~1000 次 | ~10 µs | "消费者马上就会腾出位置" |
| 第二层 | yield ~10 次 | ~100 µs | "我先让一下，消费者你赶紧跑" |
| 第三层 | 丢弃/记录 | — | "消费者真的跟不上，不能无限等" |

### 10.7 为什么低延迟系统默认选忙等？

回到本书上下文，低延迟交易引擎中：

```
生产者 (行情线程):                    消费者 (撮合引擎):
  行情到达 → 解析 → push               pop → 更新订单簿 → 撮合

  绑定核心 2 (isolcpus=2)              绑定核心 3 (isolcpus=3)
  关闭超线程                            关闭超线程
  nohz_full=2                           nohz_full=3
```

在这样的环境下：
- 核心 2 被 `isolcpus` 隔离，没有其他任务；
- 忙等时"废掉的 CPU 周期"反正也没有其他线程能利用；
- 线程不做有用功时就是在空转——那不如尽快感知队列空位，延迟最小化；
- yield 反而引入内核态切换、缓存污染和调度不确定性，违背低延迟原则。

### 10.8 一句话总结

> **队列满时该忙等还是 yield，取决于线程有没有绑定独占核心。低延迟系统中，关键线程通常绑定到隔离核心，核心上空闲周期没有其他任务可以执行，所以忙等（spin + `_mm_pause()`）是最优选择——延迟最低且不浪费任何其他人的资源。yield 引入内核态切换和调度不确定性，反而与低延迟目标背道而驰。**

---

## 十一、全文总结

> **SPSC 之所以能无锁（连 CAS 都不需要），是因为 `tail_` 只有生产者写、`head_` 只有消费者写——两个变量永远不会被两个线程同时修改。配合环形缓冲区的槽位隔离保证读写不撞车，release/acquire 内存序保证数据可见性，`alignas(64)` 消除伪共享。这三者合在一起，让 SPSC 成为最简单、最快、延迟最确定的无锁数据结构。而当队列满时，低延迟系统应默认使用 spin + `_mm_pause()` 策略，因为绑定隔离核心意味着"空转的 CPU 周期没有被浪费"，而 yield 的内核态切换反而引入不可控的延迟抖动。**
