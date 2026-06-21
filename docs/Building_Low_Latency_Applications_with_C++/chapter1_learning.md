# 第一章：低延迟应用开发导论

> 教材：《Building Low Latency Applications with C++》  
> 阶段：第一阶段（第 1–3 章）  
> 目标：建立低延迟思维，理解 C++ 语言和编译器层面的优化方向。

---

## 一、本章学习目标

学完本章后，你应该能够回答：

1. 什么是延迟？延迟和吞吐有什么区别？
2. `latency-sensitive` 和 `latency-critical` 有什么本质不同？
3. 为什么 C++ 是低延迟系统的首选语言？
4. 低延迟系统设计中有哪些通用原则？
5. 如何度量延迟？`p50` / `p99` / `p999` / `max` 代表什么？

---

## 二、学习路线（分 4 个小节，循序渐进）

### 第 1 小节：什么是延迟？

#### 2.1.1 核心概念

**延迟（Latency）**：从请求发出到收到响应所经过的时间。  
**吞吐（Throughput）**：单位时间内系统能处理多少请求。

> **重要区别**：
> - 高吞吐 ≠ 低延迟。一个系统每秒能处理 100 万条消息，但单条消息可能要等 10ms，这不算低延迟。
> - 低延迟系统关注的是**单条路径上的时间确定性**，而不是总量。

#### 2.1.2 延迟的层次

在计算机系统中，延迟无处不在：

| 操作 | 大致延迟 | 数量级 |
|---|---|---|
| CPU 寄存器访问 | 0.3 ns | 1 |
| L1 缓存访问 | 1 ns | 3 |
| L2 缓存访问 | 4 ns | 12 |
| L3 缓存访问 | 10–40 ns | 30–100 |
| 内存访问 | 100 ns | 300 |
| SSD 随机读 | 10–100 μs | 30,000–300,000 |
| 网络（同一数据中心） | 100 μs–1 ms | 300,000–3,000,000 |
| 磁盘寻道 | 10 ms | 30,000,000 |

> **为什么要了解这些？**  
> 低延迟编程的核心就是：**让数据尽可能待在离 CPU 近的地方，减少不可预测的访问。**

#### 2.1.3 代码示例：测量一段简单代码的耗时

```cpp
#include <iostream>
#include <chrono>

int main() {
    using namespace std::chrono;
    
    auto start = high_resolution_clock::now();
    
    // 要测量的代码
    volatile int sum = 0;
    for (int i = 0; i < 1'000'000; ++i) {
        sum += i;
    }
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<nanoseconds>(end - start).count();
    
    std::cout << "耗时: " << duration << " ns\n";
    std::cout << "单次平均: " << duration / 1'000'000.0 << " ns\n";
    
    return 0;
}
```

> **思考点**：
> 1. 为什么用 `volatile`？
> 2. `std::chrono::high_resolution_clock` 的精度如何？
> 3. 这个测量方法有什么问题？

<details>
<summary><b>思考点答案</b></summary>

1. **为什么用 `volatile`？**
   - `volatile` 告诉编译器：这个变量可能被程序外部因素改变，不要对其访问做优化。
   - 在这个例子里，`sum` 的每次写入和读取都要真实发生，否则编译器可能发现 `sum` 只用于内部累加，最终结果被丢弃，于是把整个循环优化掉。
   - 注意：`volatile` 不是线程同步原语，它不能保证多线程间的可见性。多线程同步要用 `std::atomic`。

2. **`std::chrono::high_resolution_clock` 的精度如何？**
   - 它提供的是系统中精度最高的时钟，通常对应 `steady_clock` 或 `system_clock` 的别名。
   - 在 Linux x86_64 上，通常基于 `CLOCK_MONOTONIC`，精度约为 1 ns 级别，但实际分辨率取决于硬件和内核实现。
   - 它适合粗粒度测量，但在微秒级以下的精确测量中会有系统调用开销和抖动。后续会学习使用 CPU 的 `RDTSC` 指令获得更高精度的时间戳。

3. **这个测量方法有什么问题？**
   - **首次运行效应**：第一次访问代码和数据时缓存是冷的，可能比稳态慢。
   - **CPU 频率变化**：现代 CPU 有睿频和节能，频率不稳定会导致测量波动。
   - **上下文切换**：进程可能被操作系统调度出去。
   - **测量本身的 overhead**：取时间戳本身需要几十纳秒。
   - **没有预热**：正式 benchmark 通常要先跑几轮 warm-up。
   - **没有多次采样取分布**：单次测量不能反映 tail latency。

</details>

---

### 第 2 小节：latency-sensitive vs latency-critical

#### 2.2.1 概念区分

| 类型 | 含义 | 例子 |
|---|---|---|
| **Latency-sensitive** | 延迟敏感，希望快，但偶尔慢一点可接受 | 视频直播、在线游戏 |
| **Latency-critical** | 延迟关键，必须在严格时限内完成，否则有价值损失 | 高频交易、自动驾驶控制、工业控制 |

> 本书重点是 **latency-critical**，因为电子交易就是这样的场景：慢 1 微秒，可能就被别人抢走了更好的价格。

#### 2.2.2 为什么要做这个区分？

因为这决定了系统设计的**严格程度**：

- **Latency-sensitive**：可以用一些通用组件，如标准库容器、常规日志、线程池。
- **Latency-critical**：必须手工控制一切可能的延迟来源：
  - 避免动态内存分配
  - 避免锁
  - 避免系统调用
  - 避免分支预测失败
  - 绑定 CPU core
  - 禁用 CPU 频率调节

#### 2.2.3 代码示例：感受“抖动”

```cpp
#include <iostream>
#include <vector>
#include <chrono>
#include <algorithm>

int main() {
    using namespace std::chrono;
    
    const int N = 1'000'000;
    std::vector<long long> times;
    times.reserve(N);
    
    volatile int sum = 0;
    
    for (int run = 0; run < N; ++run) {
        auto start = high_resolution_clock::now();
        sum += run;  // 简单操作
        auto end = high_resolution_clock::now();
        times.push_back(duration_cast<nanoseconds>(end - start).count());
    }
    
    std::sort(times.begin(), times.end());
    
    std::cout << "p50: " << times[N * 0.5] << " ns\n";
    std::cout << "p99: " << times[N * 0.99] << " ns\n";
    std::cout << "p999: " << times[N * 0.999] << " ns\n";
    std::cout << "max: " << times.back() << " ns\n";
    
    return 0;
}
```

> **思考点**：
> 1. 即使是非常简单的操作，为什么 `p99` 和 `max` 会比 `p50` 大很多？
> 2. 这种抖动从哪里来？
> 3. 为什么低延迟系统更关注 `p99` / `p999` / `max`，而不是平均值？

<details>
<summary><b>思考点答案</b></summary>

1. **为什么 `p99` 和 `max` 比 `p50` 大很多？**
   - 现代操作系统和硬件并不是完全确定性的。
   - 大部分时候，代码在热缓存、稳定频率、无中断的情况下执行，所以 `p50` 很低。
   - 但偶尔会遇到缓存未命中、TLB miss、中断、系统调用、CPU 频率切换、上下文切换等情况，导致某次执行明显变慢，形成“长尾”。
   - 在高精度测量中，即使 `sum += run` 这样一条指令，也会因为上述因素出现几十到上千倍的波动。

2. **这种抖动从哪里来？**
   - **操作系统调度**：进程/线程被切出，等待下一次调度。
   - **硬件中断**：网卡、键盘、定时器等中断会抢占 CPU。
   - **缓存状态**：数据是否在 L1/L2/L3 缓存中，是否有 cache miss。
   - **TLB miss**：虚拟地址转物理地址的页表未命中。
   - **CPU 频率调节**：节能睿频、Turbo Boost 导致频率变化。
   - **总线竞争**：多核共享内存总线、LLC 竞争。
   - **测量 overhead**：取时间戳本身不是无成本的。
   - **指令流水线波动**：分支预测失败、乱序执行的波动。

3. **为什么低延迟系统更关注 tail latency？**
   - 平均值会被大量正常样本拉低，掩盖极少数的慢请求。
   - 在 latency-critical 场景中，一次慢请求就可能造成实际损失：
     - 高频交易中：慢 1 μs 可能错过最优报价。
     - 自动驾驶中：慢 10 ms 可能导致刹车不及。
   - `p99` / `p999` / `max` 能反映系统的最差表现，是衡量确定性的关键指标。
   - 优化 tail latency 通常比优化平均延迟更难，也更体现工程能力。

</details>

---

### 第 3 小节：为什么 C++ 是低延迟首选？

#### 2.3.1 C++ 的核心优势

| 优势 | 说明 |
|---|---|
| **编译型语言** | 编译成机器码，无运行时解释开销 |
| **零成本抽象** | 模板、内联等特性可以做到“用进废退” |
| **近硬件控制** | 可直接操作内存、指针、对齐、CPU 指令 |
| **无 GC** | 不会有垃圾回收导致的不可预测停顿 |
| **确定性资源管理** | RAII，资源获取即初始化 |
| **丰富的编译时能力** | `constexpr`、模板元编程可在编译期做计算 |
| **成熟的生态** | 高性能库、编译器优化成熟 |

> **重要**：C++ 不是天然就快，而是因为它**允许你控制一切**，也要求你必须控制好一切。

#### 2.3.2 与其他语言对比

| 语言 | 适合低延迟吗？ | 原因 |
|---|---|---|
| Python | ❌ 不适合 | 解释型、GIL、动态类型、GC |
| Java | ⚠️ 受限 | JIT 编译、GC 停顿、对象头开销 |
| C# | ⚠️ 受限 | GC、运行时开销 |
| Rust | ✅ 适合 | 无 GC、内存安全、零成本抽象，但生态成熟度还在追赶 |
| C | ✅ 适合 | 更底层，但缺乏现代抽象能力 |
| C++ | ✅✅ 最适合 | 兼顾性能与抽象能力，金融业事实标准 |

#### 2.3.3 代码示例：C++ 的零成本抽象

```cpp
#include <iostream>
#include <array>

// 编译时确定大小的数组，无运行时开销
template <size_t N>
class FixedArray {
public:
    int& operator[](size_t i) { return data_[i]; }
    size_t size() const { return N; }
private:
    std::array<int, N> data_;
};

int main() {
    FixedArray<10> arr;
    for (size_t i = 0; i < arr.size(); ++i) {
        arr[i] = static_cast<int>(i);
    }
    
    // 编译后，上面的循环很可能被完全展开并优化
    // 等价于直接操作 10 个 int 变量
    
    return 0;
}
```

> **思考点**：
> 1. `std::array` 和 `std::vector` 的区别是什么？为什么低延迟更倾向 `std::array`？
> 2. `size()` 函数为什么没有运行时开销？
> 3. 如何用 `g++ -O3 -S` 验证零成本抽象？

<details>
<summary><b>思考点答案</b></summary>

1. **`std::array` 和 `std::vector` 的区别**
   - `std::array`：大小在编译期确定，栈上或作为对象的一部分连续存储，无动态分配，无额外指针开销。
   - `std::vector`：大小可变，数据在堆上，需要动态分配内存，扩容时有重新分配和拷贝开销。
   - 低延迟系统倾向 `std::array` 的原因：
     - 避免运行时堆分配，堆分配会触发系统调用或内存碎片整理，带来不可预测延迟。
     - 数据布局紧凑，有利于缓存命中。
     - 大小固定，编译器可以做更多优化，如循环展开、边界检查消除。
   - 不过 `std::array` 只适合大小已知且不变的场景。大小动态变化时必须用其他技术（如内存池、环形缓冲区）。

2. **`size()` 为什么没有运行时开销？**
   - `size()` 返回的是模板参数 `N`，`N` 是编译期常量。
   - 函数被标记为 `const`，且非常简单，编译器会将其**内联（inline）**。
   - 内联后，`arr.size()` 直接变成常量 `10`，循环条件变为 `i < 10`，编译器可以进一步做循环展开和常量传播。
   - 这里没有虚函数、没有运行时多态、没有间接调用，所有信息在编译期都已确定。

3. **如何用 `g++ -O3 -S` 验证？**
   ```bash
   g++ -O0 -S template_abstract.cpp -o zero_cost_O0.s
   g++ -O3 -S template_abstract.cpp -o zero_cost_O3.s
   ```
   - 对比两个汇编文件：
     - `-O0` 版本：会保留 `FixedArray` 的构造函数、`operator[]`、`size()` 的调用，有函数调用开销。
     - `-O3` 版本：很可能整个循环被展开成对 10 个整数的直接赋值，`FixedArray` 的抽象完全消失。
   - 你会发现 `-O3` 下，代码几乎等价于：
     ```cpp
     int arr[10];
     arr[0] = 0; arr[1] = 1; ... arr[9] = 9;
     ```
   - 这就是“零成本抽象”的含义：**你写了高级的、可复用的代码，但运行时没有额外代价。**

</details>

#### 2.3.4 动手：看汇编，验证零成本

```bash
# 生成汇编代码
g++ -O0 -S template_abstract.cpp -o zero_cost_O0.s
g++ -O3 -S template_abstract.cpp -o zero_cost_O3.s

# 对比两个文件的大小和内容
```

> **思考点**：
> 1. `-O0` 和 `-O3` 生成的汇编有什么不同？
> 2. 编译器优化了什么？
> 3. 为什么发布低延迟系统时通常用 `-O3` 或 `-Ofast`？

<details>
<summary><b>思考点答案</b></summary>

1. **`-O0` 和 `-O3` 生成的汇编有什么不同？**
   - `-O0`：几乎不做优化，编译速度快，调试信息完整，每条 C++ 语句对应多条汇编，函数调用、临时变量都保留。
   - `-O3`：激进优化，包括内联、循环展开、常量传播、死代码消除、指令调度等，生成的汇编更短、更高效，但调试更困难。
   - 在 `template_abstract.cpp` 中：
     - `-O0`：你能看到 `FixedArray<10>` 的模板实例化、`operator[]`、`size()` 的调用序列。
     - `-O3`：这些调用被内联并优化掉，可能只剩下对栈上 10 个 int 的直接赋值。

2. **编译器优化了什么？**
   - **内联（Inline）**：把小函数体直接插入调用处，消除函数调用开销。
   - **循环展开（Loop Unrolling）**：把循环体复制多份，减少循环控制开销，增加指令级并行。
   - **常量传播（Constant Propagation）**：编译期计算出常量表达式，减少运行时计算。
   - **死代码消除（Dead Code Elimination）**：删除不影响结果的代码。
   - **指令调度（Instruction Scheduling）**：重新排列指令顺序，减少流水线停顿。
   - **向量化（Vectorization）**：用 SIMD 指令一次处理多个数据。

3. **为什么发布低延迟系统时用 `-O3` 或 `-Ofast`？**
   - `-O3` 提供最大优化级别，通常能显著提升运行速度。
   - `-Ofast` 比 `-O3` 更激进，会放宽一些标准合规性（如 `-ffast-math`），可能进一步提升浮点性能，但也可能带来数值精度问题。
   - 在低延迟场景中，我们通常需要：
     - 最小化单条指令的执行时间。
     - 减少分支和函数调用。
     - 最大化 CPU 流水线效率和缓存利用率。
   - 但要注意：`-O3`/`-Ofast` 会增加编译时间，且某些优化可能让代码体积变大（Loop Unrolling），对指令缓存不友好。需要结合实际 benchmark 选择。

</details>

---

### 第 4 小节：低延迟系统设计初探

#### 2.4.1 通用设计原则

| 原则 | 含义 |
|---|---|
| **避免共享状态** | 共享状态需要同步，同步引入锁，锁引入不可预测延迟 |
| **无锁数据结构** | 使用 lock-free queue、atomic 等替代 mutex |
| **预分配与复用** | 运行时分配内存是延迟抖动的主要来源之一 |
| **批处理与流水线** | 把任务分成多个阶段，每个阶段绑定到特定线程/core |
| **减少系统调用** | 系统调用会触发内核态切换，开销大 |
| **控制内存布局** | 让相关数据在内存中相邻，提高缓存命中率 |
| **避免异常** | 异常处理路径有额外开销，且不利于分支预测 |
| **CPU 亲和性** | 把线程绑定到指定核心，减少缓存迁移和调度抖动 |

#### 2.4.2 延迟来源大排查

一个典型的低延迟系统链路可能包括：

```
用户输入 → 网络接收 → 协议解码 → 业务处理 → 网络发送 → 用户收到
```

每一个环节都可能引入延迟：

- **网络**：网卡中断、协议栈处理、内核到用户态拷贝
- **解码**：字符串解析、动态分配、哈希查找
- **业务处理**：锁竞争、缓存未命中、分支预测失败
- **发送**：系统调用、数据拷贝

> **核心思想**：低延迟不是某个地方优化一下，而是**全链路的工程化**。

#### 2.4.3 代码示例：感受锁带来的延迟抖动

```cpp
#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <vector>
#include <algorithm>

std::mutex mtx;
volatile int counter = 0;

void worker(int iterations) {
    for (int i = 0; i < iterations; ++i) {
        std::lock_guard<std::mutex> lock(mtx);
        ++counter;
    }
}

int main() {
    using namespace std::chrono;
    
    const int iterations = 100'000;
    std::vector<long long> times;
    times.reserve(iterations);
    
    // 单线程无锁测量
    auto start = high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        ++counter;
    }
    auto end = high_resolution_clock::now();
    std::cout << "单线程无锁总耗时: "
              << duration_cast<microseconds>(end - start).count()
              << " us\n";
    
    // 多线程加锁测量
    counter = 0;
    auto start2 = high_resolution_clock::now();
    std::thread t1(worker, iterations);
    std::thread t2(worker, iterations);
    t1.join();
    t2.join();
    auto end2 = high_resolution_clock::now();
    std::cout << "双线程加锁总耗时: "
              << duration_cast<microseconds>(end2 - start2).count()
              << " us\n";
    
    return 0;
}
```

> **思考点**：
> 1. 为什么多线程加锁版本会慢很多？
> 2. `std::mutex` 底层如何实现？
> 3. 如果竞争更激烈，会发生什么？
> 4. 这就是为什么后面要学 lock-free 和无锁队列。

<details>
<summary><b>思考点答案</b></summary>

1. **为什么多线程加锁版本会慢很多？**
   - 两个线程争用同一把锁，同一时刻只有一个线程能进入临界区执行 `++counter`。
   - 另一个线程必须等待，等待过程中可能发生：
     - 自旋（spin）：忙等，浪费 CPU。
     - 让出 CPU：进入睡眠，触发上下文切换，开销很大。
   - 即使一个线程拿到锁，另一个线程的缓存行也会失效，导致大量 cache coherence 流量。
   - 单线程无锁版本只需要一条 `inc` 指令，而加锁版本需要：加锁 → 进入临界区 → 修改 → 解锁，还有同步开销。

2. **`std::mutex` 底层如何实现？**
   - 现代 C++ 标准库的 `std::mutex` 通常基于 **futex（fast userspace mutex）** 实现。
   - 加锁过程：
     1. 先尝试 CAS（Compare-And-Swap）原子操作把锁状态从 0 改为 1。
     2. 如果成功，直接进入临界区，完全在用户态完成，开销很小。
     3. 如果失败（锁已被占用），则调用 `futex_wait` 进入内核态睡眠，等待被唤醒。
   - 解锁过程：
     1. 用原子操作把锁状态改回 0。
     2. 如果有等待线程，调用 `futex_wake` 唤醒一个或多个线程。
   - 关键问题：一旦进入内核态，开销就会急剧增加（微秒级）。

3. **竞争更激烈会发生什么？**
   - **上下文切换激增**：线程频繁睡眠和唤醒。
   - **缓存行乒乓（Cache Line Ping-Pong）**：多个核反复修改同一块内存的缓存状态。
   - **优先级反转**：低优先级线程持有锁，高优先级线程被阻塞。
   - **吞吐量下降**：大量时间花在等待而不是干活。
   - 更严重的情况：锁持有时间过长，导致整个系统延迟飙高。

4. **为什么要学 lock-free 和无锁队列？**
   - lock-free 算法不依赖锁，而是使用原子操作（如 CAS）来保证线程安全。
   - 在竞争不极端的情况下，lock-free 可以完全在用户态完成同步，避免进入内核态。
   - 它能提供更稳定、更可预测的延迟，特别适合 SPSC（单生产者单消费者）等模式。
   - 但要注意：lock-free 编程难度大，必须正确理解 C++ 内存模型（memory_order），否则会出现数据竞争或 ABA 问题。

</details>

---

## 三、延迟度量：你必须会看分布

### 3.1 常见的延迟指标

| 指标 | 含义 |
|---|---|
| **p50（中位数）** | 50% 的请求比这个值快 |
| **p99** | 99% 的请求比这个值快，通常看作“最差情况的常态” |
| **p999** | 99.9% 的请求比这个值快，用于发现长尾 |
| **max** | 最大延迟，通常由异常事件导致 |

> **低延迟系统的核心目标**：不只是降低 p50，而是**压缩 tail latency（p99/p999/max）**。

### 3.2 代码：测量并绘制延迟分布

```cpp
#include <iostream>
#include <vector>
#include <chrono>
#include <algorithm>
#include <random>

int main() {
    using namespace std::chrono;
    
    const int N = 10'000'000;
    std::vector<long long> latencies;
    latencies.reserve(N);
    
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, 100);
    
    for (int i = 0; i < N; ++i) {
        auto start = high_resolution_clock::now();
        
        // 模拟有微小波动的操作
        volatile int x = 0;
        for (int j = 0; j < dist(rng); ++j) {
            x += j;
        }
        
        auto end = high_resolution_clock::now();
        latencies.push_back(duration_cast<nanoseconds>(end - start).count());
    }
    
    std::sort(latencies.begin(), latencies.end());
    
    auto percentile = [&](double p) {
        return latencies[static_cast<size_t>(N * p)];
    };
    
    std::cout << "p50: " << percentile(0.50) << " ns\n";
    std::cout << "p90: " << percentile(0.90) << " ns\n";
    std::cout << "p99: " << percentile(0.99) << " ns\n";
    std::cout << "p999: " << percentile(0.999) << " ns\n";
    std::cout << "max: " << latencies.back() << " ns\n";
    
    return 0;
}
```

> **思考点**：
> 1. 为什么只看平均值会误导你？
> 2. 在交易系统中，p999 慢一次可能造成什么后果？
> 3. 后续如何获得更专业的延迟分布图？

<details>
<summary><b>思考点答案</b></summary>

1. **为什么只看平均值会误导你？**
   - 平均值对长尾不敏感。99% 的请求都很快，只有 1% 很慢，平均值可能仍然很好看。
   - 但在 latency-critical 场景中，那 1% 的慢请求可能正是造成损失的请求。
   - 平均值也无法反映系统的稳定性和抖动程度。
   - 例如：两个系统平均延迟都是 1 μs，一个 p99 是 2 μs，另一个 p99 是 100 μs，后者显然更差，但平均值看不出来。

2. **在交易系统中，p999 慢一次可能造成什么后果？**
   - 高频交易依赖极快的响应速度。p999 慢意味着某次下单或行情处理明显延迟。
   - 后果可能包括：
     - **滑点（Slippage）**：下单时价格已经变差，成交价不如预期。
     - **错失机会**：最优报价被其他更快参与者抢走。
     - **亏损**：行情已经反转，但订单还是按旧价格成交。
     - **风控触发**：如果延迟导致风险暴露时间延长，可能触发风控限制。
   - 因此，交易系统不仅追求平均快，更追求“每次都快”。

3. **如何获得更专业的延迟分布图？**
   - 使用 CPU 的 `RDTSC` 指令获得高精度时间戳（纳秒甚至亚纳秒级）。
   - 在关键路径插入时间戳，记录每个环节耗时。
   - 把数据输出到文件，用 Python + pandas + matplotlib 做分析：
     - 绘制 CDF（累积分布函数）图
     - 绘制直方图或热力图
     - 计算 p50/p99/p999/max
   - 第 11 章会专门讲插桩与性能测量，第 12 章讲数据分析与优化。

</details>

---

## 四、第一章实践任务清单

完成以下任务，才算真正掌握第一章：

### 必做

1. **环境准备**
   - 安装 GCC 11+ 或 Clang
   - 确认 `g++ -v` 可用
   - 学会用 `-O0`、`-O2`、`-O3`、`-Ofast` 编译

2. **延迟基础实验**
   - 编写程序测量 `1+1`、`a += i` 等简单操作的延迟
   - 观察 p50/p99/max 的差异
   - 思考为什么 max 会那么大

3. **编译优化对比**
   - 写一段简单循环代码
   - 分别用 `-O0` 和 `-O3` 编译
   - 对比运行时间和生成汇编的大小
   - 体会编译器优化的威力

4. **锁 vs 无锁的感受**
   - 运行上面的 mutex 示例
   - 把线程数从 2 增加到 4、8
   - 观察耗时如何变化

### 选做（加深理解）

5. **阅读拓展**
   - 了解 `std::atomic` 的基本用法
   - 了解 CPU 缓存结构（L1/L2/L3）
   - 了解什么是 false sharing

6. **思考题**
   - 如果让你设计一个高频交易系统，你会最关注哪些延迟来源？
   - 为什么 C++ 比 Java 更适合做低延迟？（从内存模型、GC、编译方式等角度）
   - 你能想到生活中哪些系统是 latency-critical 的？

> **思考点答案**：

- **高频交易系统最关注的延迟来源**：
  1. **网络 I/O**：网卡中断、内核协议栈、数据拷贝。常用方案：DPDK、kernel bypass、RDMA、专用网卡。
  2. **协议解码**：避免字符串解析，使用二进制协议（如 FIX 的快速子集、ITCH、OUCH、protobuf 等）。
  3. **锁与同步**：使用无锁队列、单线程设计、per-thread 数据结构。
  4. **内存分配**：预分配对象池、避免 `new`/`delete`、使用 arena allocator。
  5. **缓存与分支预测**：紧凑数据结构、减少分支、使用 `likely`/`unlikely`。
  6. **系统调用**：避免在热路径调用系统调用，必要时使用 `io_uring` 等异步接口。
  7. **CPU 调度**：线程绑定到指定核心、禁用超线程、禁用 CPU 节能。

- **为什么 C++ 比 Java 更适合低延迟**：
  1. **内存模型**：C++ 提供更细粒度的内存控制，如对齐、内存序、栈上分配。
  2. **GC 停顿**：Java 的垃圾回收会导致 STW（Stop-The-World）停顿，这在 latency-critical 场景中不可接受。虽然有 ZGC/Shenandoah 等低延迟 GC，但仍不如 C++ 的确定性。
  3. **编译方式**：C++ 是 AOT（Ahead-Of-Time）编译，运行时无 JIT 编译开销和去优化风险。
  4. **对象开销**：Java 对象有对象头、引用开销，C++ 可以精确控制每个字节。
  5. **异常**：C++ 可以禁用异常，Java 异常机制开销较大。
  6. **生态**：高频交易行业长期使用 C++，有大量成熟库和经验。

- **生活中 latency-critical 的系统**：
  - 自动驾驶（刹车控制、避障）
  - 工业控制（机器人、数控机床）
  - 航空航天（飞行控制）
  - 医疗设备（呼吸机、起搏器）
  - 电子竞技和云游戏（输入响应）
  - 高频交易
  - 实时音视频通话

---

## 五、常见误区与注意事项

| 误区 | 正确认识 |
|---|---|
| ❌ “C++ 本身就快” | ✅ C++ 只是给了你控制性能的能力，写不好照样慢 |
| ❌ “平均值低就够了” | ✅ 低延迟系统要关注 p99/p999/max |
| ❌ “用一个好算法就够了” | ✅ 还要考虑缓存、分支预测、内存布局、系统调用等 |
| ❌ “多线程一定快” | ✅ 多线程引入竞争和同步，可能更慢 |
| ❌ “优化越早越好” | ✅ 先保证正确性，再测量，再优化 |

---

## 六、下章预告

第二章会讲**常见低延迟应用场景**，包括：

- 视频直播
- 在线游戏
- IoT
- 零售分析
- **电子交易**（本书重点）

重点是理解这些场景中的**延迟来源**，为我们后面构建交易所系统做铺垫。

---

## 七、推荐学习节奏

| 时间 | 内容 |
|---|---|
| 第 1 天 | 第 1–2 小节：延迟概念、latency-sensitive vs critical |
| 第 2 天 | 第 3 小节：C++ 为什么是首选，编译优化实验 |
| 第 3 天 | 第 4 小节：设计原则、锁的延迟实验 |
| 第 4 天 | 实践任务：环境准备、延迟测量、编译对比 |
| 第 5 天 | 复习、思考题、准备进入第二章 |

---

## 八、本章核心金句

> “低延迟不是用某个技巧就能达成的，而是对系统全链路的深刻理解和工程化控制。”

> “C++ 的低延迟优势不是免费的，它来自于你能控制细节，也必须控制细节。”

> “优化之前，先测量；测量之前，先理解。”
