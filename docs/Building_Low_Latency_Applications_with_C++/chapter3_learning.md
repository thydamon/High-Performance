# 第三章：C++ 低延迟视角

> 教材：《Building Low Latency Applications with C++》  
> 阶段：第一阶段（第 1–3 章）  
> 目标：从 C++ 语言和编译器层面理解影响延迟的因素，建立“代码即延迟”的优化意识。

---

## 一、本章学习目标

学完本章后，你应该能够回答：

1. 为什么大 O 复杂度相同的算法，实际运行延迟可能差几倍甚至几十倍？
2. CPU 缓存结构（L1/L2/L3）如何影响 C++ 程序性能？
3. 什么是缓存行（cache line）、伪共享（false sharing）和缓存未命中（cache miss）？
4. 分支预测失败会带来多少性能损失？如何写出对分支友好的代码？
5. `virtual`、函数指针、`std::function`、模板各自在延迟上的优劣是什么？
6. 异常处理为什么在高频路径上是“隐形成本”？
7. 常见编译器优化 flag（`-O2`、`-O3`、`-Ofast`、`-march`、LTO）分别做了什么？

---

## 二、为什么要学这一章？

前两章建立了**业务场景**和**低延迟思维**。  
第三章进入**代码和编译器层面**，让你看到：

- 同样的业务逻辑，不同的 C++ 写法，延迟可能天差地别；
- 现代 CPU 不是“按指令一条条执行”那么简单，缓存、分支预测、流水线都会左右性能；
- C++ 的低延迟优势，很大程度上来自“**对内存和计算资源的精确控制**”。

> **核心思想**：写低延迟 C++，不是背优化技巧，而是理解“**代码在硬件上如何执行**”，并让两者匹配。

---

## 三、学习路线（分 5 个小节）

### 第 1 小节：数据结构与算法选择

#### 3.1.1 大 O 复杂度的局限

算法课上我们常比较时间复杂度，比如 `O(1)`、`O(log n)`、`O(n)`。  
但在低延迟系统中，**大 O 只描述渐进趋势，不描述常数因子和内存行为**。

| 操作 | `std::vector` | `std::list` | `std::map` |
|---|---|---|---|
| 随机访问 | `O(1)`，缓存友好 | `O(n)`，指针跳转 | `O(log n)`，指针跳转 |
| 尾部插入 | 均摊 `O(1)`，可能重新分配 | `O(1)`，内存分配 | `O(log n)`，内存分配 |
| 遍历 | 极快，连续内存 | 慢，cache miss 多 | 较慢，节点不连续 |

> **关键点**：低延迟场景下，**缓存友好的线性扫描**有时比理论更快的复杂数据结构更快。

**为什么？**

大 O 复杂度只描述算法随规模增长的趋势，不描述**缓存行为**和**常数因子**。现代 CPU 的内存访问代价差异巨大——访问主内存比访问 L1 缓存慢 **50–100 倍**——因此低延迟程序的首要目标，是让热数据尽量待在缓存里。

| 对比维度 | 缓存友好的线性扫描（如 `std::vector` 遍历） | 复杂数据结构（如 `std::map` / `std::list`） |
|---|---|---|
| 内存布局 | 连续存放 | 节点分散在堆上 |
| 缓存行利用 | 一次加载 64 字节，覆盖多个元素 | 每次访问可能加载新的缓存行 |
| 硬件预取 | 顺序访问可被预取器有效预测 | 指针跳转让预取器无能为力 |
| 单次操作复杂度 | 通常是 `O(n)` | 可能更优，如 `O(log n)` 或 `O(1)` |
| 实际延迟 | 低，命中 L1/L2 为主 | 高，cache miss 频繁时可能被放大几十倍 |

**一句话总结**：单次操作的理论复杂度更优，并不等同于实际延迟更低。一次主内存访问的代价，往往远高于多几次简单的 CPU 比较。

**什么时候线性扫描更优？**

- 数据量适中，能放入 L1/L2/L3 缓存，或可被顺序预取覆盖；
- 遍历/查找操作简单，每次比较开销很小；
- 数据在内存中连续存放（数组、`std::vector`、ring buffer 等）；
- 查找不特别频繁，或访问模式本身有规律。

**什么时候复杂数据结构仍然更好？**

- 数据量很大，无法放入缓存，且查找远多于遍历；
- 需要频繁在中间插入/删除，搬移成本极高；
- 需要稳定的迭代器或指针。

即便如此，低延迟系统也常选择**更缓存友好的复杂结构**，例如：
- `flat_hash_map` / `robin_hood`：开放寻址、连续存储；
- 侵入式链表 + 内存池：避免 `new/delete`，节点集中管理。

#### 3.1.2 低延迟系统偏好的数据结构

| 数据结构 | 适用场景 | 原因 |
|---|---|---|
| **固定大小数组 / std::array** | 已知数量的小集合 | 栈分配、零开销、缓存连续 |
| **std::vector（预分配）** | 动态但可预估数量的集合 | 连续内存、遍历快、可 `reserve` |
| **环形缓冲区（ring buffer）** | 生产者-消费者通信 | 固定大小、无锁友好、预分配 |
| **flat_hash_map / robin_hood** | 低延迟哈希表 | 开放寻址、连续存储、更缓存友好 |
| **侵入式链表 / 内存池内链表** | 需要频繁插入删除 | 避免 `new/delete`，节点来自内存池 |

#### 3.1.3 常见错误

1. **过度使用 `std::map`**  
   红黑树节点分散在堆上，遍历时 cache miss 严重。如果 key 是整数，优先考虑开放寻址哈希表。

2. **vector 频繁扩容**  
   `push_back` 触发重新分配时会分配新内存并搬移所有元素。用 `reserve` 预分配。

3. **在热路径上分配内存**  
   所有会调用 `new` 的操作都可能引入不可预测延迟。

#### 3.1.4 代码示例：vector 遍历 vs list 遍历

```cpp
#include <iostream>
#include <vector>
#include <list>
#include <chrono>

const int N = 1'000'000;

int main() {
    std::vector<int> vec(N);
    std::list<int> lst;
    for (int i = 0; i < N; ++i) {
        vec[i] = i;
        lst.push_back(i);
    }

    using namespace std::chrono;

    // vector 遍历
    long long sum_vec = 0;
    auto start = high_resolution_clock::now();
    for (int v : vec) sum_vec += v;
    auto end = high_resolution_clock::now();
    auto t_vec = duration_cast<microseconds>(end - start).count();

    // list 遍历
    long long sum_lst = 0;
    start = high_resolution_clock::now();
    for (int v : lst) sum_lst += v;
    end = high_resolution_clock::now();
    auto t_lst = duration_cast<microseconds>(end - start).count();

    std::cout << "vector 耗时: " << t_vec << " us, sum=" << sum_vec << "\n";
    std::cout << "list 耗时:   " << t_lst << " us, sum=" << sum_lst << "\n";
    std::cout << "list / vector 比值: " << static_cast<double>(t_lst) / t_vec << "\n";

    return 0;
}
```

> **思考点**：
> 1. 为什么 `std::vector` 遍历通常比 `std::list` 快几倍？
> 2. 什么时候 `std::list` 反而更合适？
> 3. 如果数据量很小（比如 10 个元素），差距还大吗？

<details>
<summary><b>思考点答案</b></summary>

1. **为什么 vector 遍历更快？**
   - `vector` 的元素在内存中连续存放，CPU 可以一次预取多个元素到缓存。
   - `list` 的节点分散在堆上，每次访问都要读下一个指针，产生大量 cache miss。
   - 现代 CPU 的预取器（prefetcher）对连续内存非常有效，对链表几乎无能为力。

2. **什么时候 list 更合适？**
   - 需要频繁在中间插入/删除，且元素很大、搬移成本高。
   - 需要稳定的迭代器/指针（vector 重新分配后失效）。
   - 在低延迟场景中，即使需要链表，也常用**侵入式链表 + 内存池**来避免 `new/delete`。

3. **数据量很小时差距还大吗？**
   - 不一定。小数据量可能全部落在 L1/L2 缓存内，差距会缩小。
   - 但当数据量超过缓存容量时，连续内存的优势会非常明显。

</details>

---

### 第 2 小节：缓存与内存访问成本

#### 3.2.1 CPU 缓存层次

现代 CPU 有多级缓存：

| 层级 | 典型大小 | 典型延迟 | 位置 |
|---|---|---|---|
| L1 缓存 | 32–64 KB | 约 1–4 个时钟周期 | 每个核心 |
| L2 缓存 | 256–512 KB | 约 10–20 个时钟周期 | 每个核心 |
| L3 缓存 | 数 MB 到数十 MB | 约 30–70 个时钟周期 | 共享 |
| 主内存（DRAM） | 数 GB | 约 100–300 个时钟周期 | 主板 |

> 访问主内存比访问 L1 缓存慢 **50–100 倍**。低延迟程序的首要目标之一，就是让热数据尽量待在缓存里。

#### 3.2.2 缓存行（Cache Line）

缓存不是按单个字节加载的，而是按**缓存行**（通常 64 字节）批量加载。

- 访问一个 8 字节整数时，CPU 会把包含它的整个 64 字节缓存行一起读入 L1。
- 如果后续访问的数据也在同一缓存行内，就是**缓存命中**。
- 如果不在，就是**缓存未命中**，需要从 L2/L3/内存加载。

#### 3.2.3 顺序访问 vs 随机访问

```
顺序访问：0, 1, 2, 3, 4, 5, 6, 7...
随机访问：102, 5, 887, 23, 456...
```

顺序访问能充分利用缓存行和硬件预取；随机访问几乎每次都要重新加载缓存行。

#### 3.2.4 伪共享（False Sharing）

当两个线程写入**不同变量**，但这两个变量恰好位于**同一个缓存行**时，它们会互相无效化对方的缓存，导致性能暴跌。这就是伪共享。

**解决方案**：让不同线程频繁写入的变量位于不同缓存行，使用 `alignas(64)` 填充。

#### 3.2.5 代码示例：顺序访问 vs 随机访问

```cpp
#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>

const int N = 10'000'000;

int main() {
    std::vector<int> data(N);
    for (int i = 0; i < N; ++i) data[i] = i;

    using namespace std::chrono;

    // 顺序访问
    long long sum_seq = 0;
    auto start = high_resolution_clock::now();
    for (int i = 0; i < N; ++i) {
        sum_seq += data[i];
    }
    auto end = high_resolution_clock::now();
    auto t_seq = duration_cast<microseconds>(end - start).count();

    // 随机访问
    std::vector<int> indices(N);
    for (int i = 0; i < N; ++i) indices[i] = i;
    std::shuffle(indices.begin(), indices.end(), std::mt19937{42});

    long long sum_rand = 0;
    start = high_resolution_clock::now();
    for (int idx : indices) {
        sum_rand += data[idx];
    }
    end = high_resolution_clock::now();
    auto t_rand = duration_cast<microseconds>(end - start).count();

    std::cout << "顺序访问耗时: " << t_seq << " us, sum=" << sum_seq << "\n";
    std::cout << "随机访问耗时: " << t_rand << " us, sum=" << sum_rand << "\n";
    std::cout << "随机 / 顺序 比值: " << static_cast<double>(t_rand) / t_seq << "\n";

    return 0;
}
```

> **思考点**：
> 1. 为什么随机访问比顺序访问慢很多？
> 2. 数组大小对两者差距有什么影响？
> 3. 在低延迟代码中，如何利用这一特性？

<details>
<summary><b>思考点答案</b></summary>

1. **为什么随机访问慢？**
   - 顺序访问时，CPU 预取器能提前把后续缓存行读入缓存。
   - 随机访问时，每次访问的位置不可预测，几乎每次都要从 L2/L3/内存重新加载。
   - 当数据量超过 L3 缓存时，随机访问可能每跳都要访问 DRAM。

2. **数组大小的影响**
   - 如果数组很小（全部在 L1/L2 内），差距较小。
   - 当数组超过 L3 缓存时，顺序访问仍可通过预取保持高效，随机访问则急剧变慢。

3. **如何利用**
   - 让热数据在内存中连续存放（数组/vector）。
   - 遍历容器时按顺序访问，避免跳转。
   - 结构体/类设计时，把热路径上一起访问的字段放在一起，避免跨缓存行。
   - 使用结构体数组（SoA, Structure of Arrays）而非数组结构体（AoS）来优化 SIMD/向量化。

</details>

#### 3.2.6 代码示例：伪共享与对齐

```cpp
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>

const int ITERATIONS = 10'000'000;

struct SharedBad {
    int64_t a = 0;
    int64_t b = 0;  // a 和 b 可能在同一个缓存行
};

struct SharedGood {
    alignas(64) int64_t a = 0;
    alignas(64) int64_t b = 0;  // 确保各自在独立缓存行
};

template<typename T>
void benchmark(const char* name) {
    T data;

    using namespace std::chrono;
    auto start = high_resolution_clock::now();

    std::thread t1([&]() {
        for (int i = 0; i < ITERATIONS; ++i) ++data.a;
    });
    std::thread t2([&]() {
        for (int i = 0; i < ITERATIONS; ++i) ++data.b;
    });
    t1.join();
    t2.join();

    auto end = high_resolution_clock::now();
    auto us = duration_cast<microseconds>(end - start).count();
    std::cout << name << ": " << us << " us, a=" << data.a << " b=" << data.b << "\n";
}

int main() {
    benchmark<SharedBad>("未对齐 (可能伪共享)");
    benchmark<SharedGood>("对齐到缓存行");
    return 0;
}
```

> **思考点**：
> 1. 为什么 `SharedBad` 比 `SharedGood` 慢？
> 2. `alignas(64)` 的作用是什么？为什么是 64？
> 3. 在实际代码中，哪些场景容易出现伪共享？

<details>
<summary><b>思考点答案</b></summary>

1. **为什么 SharedBad 慢？**
   - 两个线程分别写 `a` 和 `b`，但如果它们在同一个缓存行，每次写入都会让另一个核心的该缓存行失效。
   - 两个核心反复互相无效化缓存，导致大量缓存一致性流量和重载。

2. **alignas(64) 的作用**
   - 把变量对齐到 64 字节边界，确保它独占一个缓存行（主流 x86 缓存行为 64 字节）。
   - 注意：实际缓存行大小可能因架构不同而变化，但 64 字节是常见默认值。

3. **常见伪共享场景**
   - 多线程计数器放在相邻位置。
   - 无锁队列的头/尾指针未对齐。
   - per-thread 统计变量未按缓存行对齐。

</details>

---

### 第 3 小节：类型选择与分支预测

#### 3.3.1 选择合适的数据类型

| 类型 | 特点 | 低延迟场景建议 |
|---|---|---|
| `int32_t` | 32 位，CPU 原生支持 | 默认整数类型，优先使用 |
| `int64_t` | 64 位 | 价格、订单量、时间戳用 64 位 |
| `double` | 64 位浮点 | 金融计算常用，但避免比较相等 |
| `float` | 32 位浮点 | 缓存更友好，但精度有限 |
| `fixed-point` | 定点数 | 避免浮点误差，金融常用 |
| `char*` / 字符串 | 变长 | 热路径避免动态字符串 |

> **原则**：用刚好能表示业务范围的类型；避免不必要的宽类型；避免在热路径上使用 `std::string`。

#### 3.3.2 分支预测（Branch Prediction）

CPU 会猜测条件跳转的方向，并提前执行预测路径。如果猜错，需要清空流水线，代价很高。

- 现代 CPU 分支预测准确率通常 > 90%。
- 但如果分支模式混乱（如随机数据），预测失败率会上升。
- 一次预测失败可能损失 **10–20 个时钟周期**。

#### 3.3.3 如何写出分支友好的代码

1. **让常见路径连续出现**  
   编译器/CPU 会学习分支历史，有规律的模式更容易预测。

2. **减少不必要的 if**  
   用查表、位运算、条件移动（CMOV）替代分支。

3. **排序数据后再处理**  
   如果判断条件依赖数据，先排序让分支模式更可预测。

4. **使用 `[[likely]]` / `[[unlikely]]`**（C++20）  
   提示编译器哪个分支更常见。

#### 3.3.4 代码示例：分支预测的影响

```cpp
#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>

const int N = 100'000'000;

int main() {
    std::vector<int> data(N);
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, 255);

    for (int i = 0; i < N; ++i) {
        data[i] = dist(rng);
    }

    using namespace std::chrono;

    // 未排序：分支随机
    long long sum_unsorted = 0;
    auto start = high_resolution_clock::now();
    for (int v : data) {
        if (v < 128) sum_unsorted += v;
    }
    auto end = high_resolution_clock::now();
    auto t_unsorted = duration_cast<milliseconds>(end - start).count();

    // 排序后：分支可预测
    std::sort(data.begin(), data.end());
    long long sum_sorted = 0;
    start = high_resolution_clock::now();
    for (int v : data) {
        if (v < 128) sum_sorted += v;
    }
    end = high_resolution_clock::now();
    auto t_sorted = duration_cast<milliseconds>(end - start).count();

    std::cout << "未排序耗时: " << t_unsorted << " ms, sum=" << sum_unsorted << "\n";
    std::cout << "排序后耗时: " << t_sorted << " ms, sum=" << sum_sorted << "\n";

    return 0;
}
```

> **思考点**：
> 1. 为什么排序后遍历快很多？
> 2. 在低延迟系统中，排序是否总是值得？
> 3. 除了排序，还有什么方法减少分支预测失败？

<details>
<summary><b>思考点答案</b></summary>

1. **为什么排序后快？**
   - 排序后，数据呈现“前一半 <128，后一半 ≥128”的规律。
   - CPU 分支预测器很快学会这个模式，预测成功率接近 100%。
   - 未排序数据是随机的，分支方向无法预测，频繁刷新流水线。

2. **排序是否总是值得？**
   - 不一定。排序本身有 `O(n log n)` 开销。
   - 只有当数据会被多次遍历时，排序的收益才能摊平。
   - 实时交易数据流通常按时间顺序到达，可能本身就有一定规律。

3. **其他减少分支的方法**
   - 用位掩码、查表法替代 `if`。
   - 使用 `std::conditional` 或编译期分支。
   - 数据预处理：把“可能满足条件”和“不满足条件”的数据分开存放。
   - 用 C++20 `[[likely]]` / `[[unlikely]]` 提示编译器。

</details>

---

### 第 4 小节：内联、编译时多态与异常

#### 3.4.1 内联（Inline）

函数调用有开销：保存寄存器、跳转、参数传递、返回。  
内联函数把函数体直接展开到调用处，消除这些开销。

- 小函数、频繁调用的函数非常适合内联。
- `inline` 关键字只是建议，编译器会自己决定是否内联。
- 虚函数、递归、过大函数通常不会被内联。

#### 3.4.2 运行时多态 vs 编译时多态

| 特性 | `virtual` 运行时多态 | 模板 / CRTP 编译时多态 |
|---|---|---|
| 调度时机 | 运行时查虚函数表 | 编译期确定 |
| 开销 | 间接调用、无法内联、虚表指针 | 零开销、可内联 |
| 灵活性 | 运行时替换实现 | 编译期确定 |
| 二进制大小 | 较小 | 可能膨胀 |
| 延迟 | 更高 | 更低 |

> 低延迟热路径上，优先使用**编译时多态**（模板、CRTP、函数指针）。

#### 3.4.3 CRTP 简介

CRTP（Curiously Recurring Template Pattern，奇异递归模板模式）是一种用模板在**编译期实现多态**的技术，用来替代运行时的 `virtual` 虚函数机制。

**基本形式**

```cpp
template<typename Derived>
class Base {
public:
    void interface() {
        // 编译期将 this 转换为 Derived*，调用派生类实现
        static_cast<Derived*>(this)->implementation();
    }
};

// 派生类把自己作为基类模板参数传入
class Derived : public Base<Derived> {
public:
    void implementation() { /* ... */ }
};
```

核心要点：

| 特性 | 说明 |
|---|---|
| 派生类继承 `Base<Derived>` | 把自己作为基类的模板参数 |
| 基类通过 `static_cast<Derived*>(this)` 调用 | 在编译期确定实际类型 |
| 没有 `virtual` 关键字 | 不需要虚函数表（vtable） |
| 可被内联 | 编译器在编译期知道调用哪个函数 |

**为什么 CRTP 更快？**

相比 `virtual` 运行时多态，CRTP 在延迟敏感路径上有明显优势：

| 特性 | `virtual` 运行时多态 | CRTP 编译时多态 |
|---|---|---|
| 调度时机 | 运行时查虚函数表 | 编译期确定 |
| 内存开销 | 每个对象带一个虚表指针（vptr） | 无额外指针 |
| 调用方式 | 间接跳转，CPU 难以预测 | 直接调用，可预测 |
| 内联优化 | 通常无法内联 | 可被完全内联 |
| 延迟 | 更高 | 更低 |

根本原因：

1. **没有虚函数表指针**：`virtual` 对象内部隐藏一个 vptr，占用内存并可能影响缓存行布局；CRTP 对象没有。
2. **没有间接跳转**：`virtual` 调用要先读 vptr，再读虚函数表，再跳转到函数地址，CPU 分支预测和预取都较难处理；CRTP 直接定位到具体函数。
3. **可被内联展开**：编译器在编译期知道调用 `Derived::impl`，可以把函数体直接展开到调用处，消除函数调用开销，甚至进一步优化（如常量折叠、死代码消除）。

**CRTP 的代价**

CRTP 并非银弹，它用灵活性换取性能：

| 代价 | 说明 |
|---|---|
| 编译期绑定 | 运行时无法替换实现，不能从配置文件或插件动态加载策略 |
| 二进制膨胀 | 每个派生类都会实例化一套基类模板代码，可能增加可执行文件大小 |
| 代码可读性 | 模板技巧比 `virtual` 更难理解，调试时类型信息更复杂 |
| 无法异构存储 | 不能像 `std::vector<BaseVirtual*>` 那样把不同类型派生类统一存储和管理 |

**什么时候用 virtual，什么时候用 CRTP？**

| 场景 | 推荐 |
|---|---|
| 热路径上调用上亿次，类型编译期已知 | **CRTP / 模板** |
| 策略需在运行时根据配置/插件选择 | `virtual` |
| 对象生命周期和类型编译期无法确定 | `virtual` |
| 非热路径，追求可维护性和接口清晰 | `virtual` |

**一句话总结**

> CRTP 是用模板在编译期“冒充”多态的技术。它牺牲了运行时的灵活性，换取了零开销、可内联的确定性调用，因此在低延迟热路径上比 `virtual` 更适合。

#### 3.4.4 `std::function`、函数指针与模板

| 机制 | 灵活性 | 开销 |
|---|---|---|
| **函数指针** | 低，只能指向函数 | 最小，直接跳转 |
| **模板 callable** | 中，编译期绑定 | 零额外开销，可内联 |
| **`std::function`** | 高，运行时擦除 | 类型擦除、堆分配（大对象时）、间接调用 |

> 低延迟路径避免 `std::function`，它可能分配内存且无法内联。

#### 3.4.5 异常处理（Exceptions）

C++ 异常在**不抛出时**几乎没有直接开销，但：

- 编译器需要生成异常处理表，可能抑制某些优化。
- 抛异常时开销极大，涉及栈展开。
- 在低延迟热路径上，通常禁用异常或保证不抛异常。
- 常用替代：返回错误码、`std::optional`、`std::expected`（C++23）。

#### 3.4.6 代码示例：virtual vs CRTP

```cpp
#include <iostream>
#include <vector>
#include <chrono>
#include <memory>

const int N = 100'000'000;

// 运行时多态
class BaseVirtual {
public:
    virtual int compute(int x) const = 0;
    virtual ~BaseVirtual() = default;
};

class DerivedVirtual : public BaseVirtual {
public:
    int compute(int x) const override {
        return x * x + 1;
    }
};

// 编译时多态：CRTP
template<typename Derived>
class BaseCRTP {
public:
    int compute(int x) const {
        return static_cast<const Derived*>(this)->impl(x);
    }
};

class DerivedCRTP : public BaseCRTP<DerivedCRTP> {
public:
    int impl(int x) const {
        return x * x + 1;
    }
};

int main() {
    using namespace std::chrono;

    // 虚函数版本
    std::vector<std::unique_ptr<BaseVirtual>> vobjs;
    for (int i = 0; i < 1000; ++i) {
        vobjs.push_back(std::make_unique<DerivedVirtual>());
    }

    volatile int result = 0;
    auto start = high_resolution_clock::now();
    for (int i = 0; i < N; ++i) {
        result = vobjs[i % vobjs.size()]->compute(i);
    }
    auto end = high_resolution_clock::now();
    auto t_virtual = duration_cast<milliseconds>(end - start).count();

    // CRTP 版本
    std::vector<DerivedCRTP> cobjs(1000);
    start = high_resolution_clock::now();
    for (int i = 0; i < N; ++i) {
        result = cobjs[i % cobjs.size()].compute(i);
    }
    end = high_resolution_clock::now();
    auto t_crtp = duration_cast<milliseconds>(end - start).count();

    std::cout << "virtual 耗时: " << t_virtual << " ms\n";
    std::cout << "CRTP 耗时:    " << t_crtp << " ms\n";

    return 0;
}
```

> **思考点**：
> 1. 为什么 CRTP 通常比 virtual 快？
> 2. 什么情况下仍然需要使用 virtual？
> 3. 为什么说 `std::function` 比函数指针慢？

<details>
<summary><b>思考点答案</b></summary>

1. **为什么 CRTP 更快？**
   - `virtual` 调用需要通过虚函数表指针间接跳转，CPU 难以预测和预取。
   - CRTP 在编译期就知道调用哪个函数，编译器可以内联展开。
   - 内联后，函数调用开销和间接跳转开销都被消除。

2. **什么时候仍然用 virtual？**
   - 运行时才知道对象类型，比如从配置文件加载策略、插件系统。
   - 对象生命周期和类型在编译期无法确定。
   - 非热路径上，virtual 的灵活性和可维护性更好。

3. **为什么 std::function 比函数指针慢？**
   - `std::function` 是类型擦除包装，内部可能用虚函数或函数指针调用。
   - 捕获较大的 lambda 时，`std::function` 可能需要在堆上分配内存。
   - 编译器通常无法内联 `std::function` 的调用。
   - 函数指针虽然也无法内联，但开销更确定、无分配。

</details>

---

### 第 5 小节：编译器优化与 flag

#### 3.5.1 常见优化等级

| Flag | 含义 | 适用场景 |
|---|---|---|
| `-O0` | 几乎不优化，便于调试 | 开发调试 |
| `-O1` | 基本优化 | 平衡调试与性能 |
| `-O2` | 常规优化，不牺牲代码大小 | 生产环境常用 |
| `-O3` | 激进优化，启用向量化、循环展开等 | 追求性能 |
| `-Ofast` | 在 `-O3` 基础上放宽 IEEE 浮点标准 | 对浮点精度不敏感时 |
| `-Og` | 优化但保留调试信息 | 调优时调试 |

> 低延迟生产环境通常用 `-O2` 或 `-O3`，配合 `-DNDEBUG` 关闭断言。

#### 3.5.2 架构相关 flag

| Flag | 作用 |
|---|---|
| `-march=native` | 针对本机 CPU 指令集生成代码（如 AVX2、AVX-512） |
| `-mtune=native` | 针对本机 CPU 微架构调优，但保留兼容性 |
| `-march=x86-64-v3` | 使用较新的 x86-64 微架构级别 |

> 注意：`-march=native` 生成的二进制在其他 CPU 上可能无法运行。

#### 3.5.3 链接时优化（LTO）

| Flag | 作用 |
|---|---|
| `-flto` | 链接时优化，跨模块内联和死代码消除 |
| `-fwhole-program` | 告诉编译器整个程序可见，进一步激进优化 |

LTO 可以让编译器在链接阶段看到所有代码，做跨模块内联。对静态链接的大型程序很有用。

#### 3.5.4 静态链接与 `-static`

| Flag | 作用 |
|---|---|
| `-static` | 静态链接 C/C++ 运行时库 |
| `-static-libgcc` / `-static-libstdc++` | 只静态链接特定运行时库 |

> 第二章提到过，超低延迟系统倾向静态链接以避免运行时加载和解析开销。

#### 3.5.5 代码示例：对比 -O0 和 -O3

```cpp
// sum.cpp
#include <iostream>
#include <chrono>

const int N = 100'000'000;

int main() {
    long long sum = 0;

    using namespace std::chrono;
    auto start = high_resolution_clock::now();

    for (int i = 0; i < N; ++i) {
        sum += i;
    }

    auto end = high_resolution_clock::now();
    auto ms = duration_cast<milliseconds>(end - start).count();

    std::cout << "sum=" << sum << " time=" << ms << " ms\n";
    return 0;
}
```

编译对比：

```bash
# 无优化
g++ -O0 -o sum_o0 sum.cpp

# 激进优化
g++ -O3 -march=native -o sum_o3 sum.cpp

# 静态链接 + LTO
g++ -O3 -march=native -flto -static -o sum_static_lto sum.cpp

./sum_o0
./sum_o3
./sum_static_lto
```

> **思考点**：
> 1. `-O3` 通常比 `-O2` 快吗？有没有反例？
> 2. `-march=native` 有什么风险？
> 3. LTO 为什么能进一步提升性能？代价是什么？

<details>
<summary><b>思考点答案</b></summary>

1. **-O3 一定比 -O2 快吗？**
   - 不一定。`-O3` 会做更激进的循环展开、向量化、内联，有时会增加代码体积。
   - 更大的代码可能降低指令缓存命中率，反而变慢。
   - 通常做法是：用 `-O2` 跑基准测试，再尝试 `-O3`，根据实测结果选择。

2. **-march=native 的风险**
   - 生成的二进制依赖本机 CPU 的特定指令集（如 AVX-512）。
   - 在其他不支持这些指令的 CPU 上运行会崩溃（非法指令）。
   - 部署固定环境时可用；发布到多种硬件时需谨慎。

3. **LTO 的好处与代价**
   - **好处**：跨模块内联、更好的死代码消除、全局优化。
   - **代价**：编译和链接时间显著增加，内存消耗大；调试更困难。
   - 对于静态链接的超低延迟程序，LTO 通常值得开启。

</details>

---

## 四、第三章实践任务清单

完成以下任务，才算真正掌握第三章：

### 必做

1. **编译优化对比实验**
   - 用同一段计算密集型代码（如大数组求和），分别用 `-O0`、`-O2`、`-O3`、`-O3 -march=native` 编译。
   - 记录运行时间和二进制大小。
   - 用 `objdump -d` 查看不同优化等级下的汇编差异。

2. **顺序 vs 随机访问 benchmark**
   - 编写 benchmark 测试数组顺序访问和随机访问的耗时。
   - 改变数组大小（1万、100万、1亿），观察延迟变化。
   - 用 `perf stat -e cache-misses,cache-references` 观察缓存未命中。

3. **容器对比实验**
   - 对比 `std::vector`、`std::list`、`std::deque` 的遍历和插入性能。
   - 思考：在交易所订单簿或行情队列中，你会选哪个？

4. **virtual vs CRTP 对比**
   - 实现一个简单策略接口，分别用 `virtual` 和 CRTP 实现。
   - 在循环中调用 1 亿次，测量耗时。
   - 观察 `-O3` 下两者差距是否缩小。

5. **分支预测实验**
   - 用随机数据和排序后数据分别测试带 `if` 的循环。
   - 尝试使用 `[[likely]]` / `[[unlikely]]`，观察是否有改善。

### 选做（加深理解）

6. **伪共享实验**
   - 编写两个线程分别自增两个相邻变量的程序。
   - 对比未对齐和 `alignas(64)` 对齐后的性能。
   - 用 `perf c2c`（如果可用）观察缓存一致性事件。

7. **std::function 开销分析**
   - 对比函数指针、模板 lambda、`std::function` 调用开销。
   - 尝试捕获大量数据的 lambda，观察 `std::function` 是否触发堆分配。

8. **异常开销测试**
   - 写一个频繁抛异常的函数，与返回错误码版本对比。
   - 观察异常路径和非异常路径的巨大差异。

9. **思考题**
   - 为什么金融系统常用定点数或整数（如把价格存为 `int64_t` 分）而不是 `double`？
   - 在行情处理热路径上，你会避免使用哪些 C++ 特性？为什么？
   - 编译器优化是否会改变你的代码语义？如何验证？

---

## 五、常见误区与注意事项

| 误区 | 正确认识 |
|---|---|
| ❌ “大 O 小就一定快” | ✅ 缓存行为、常数因子、内存分配同样重要 |
| ❌ “链表插入总是 O(1)，所以比 vector 快” | ✅ 链表的内存不连续，cache miss 可能让整体更慢 |
| ❌ “virtual 开销可以忽略” | ✅ 在热路径上亿次调用时，虚函数表开销很明显 |
| ❌ “异常不抛就没开销” | ✅ 不抛时直接开销小，但可能影响编译器优化和代码体积 |
| ❌ “-O3 一定最好” | ✅ 有时 `-O2` 更稳；务必用 benchmark 验证 |
| ❌ “缓存未命中只和算法有关” | ✅ 数据结构和对象布局同样决定缓存命中率 |
| ❌ “现代 CPU 很快，不用关心这些细节” | ✅ 微秒级延迟场景下，这些细节正是核心竞争力 |

---

## 六、下章预告

第四章会进入**低延迟基础组件的实现**，包括：

- 线程库与线程亲和性
- 内存池与对象复用
- 无锁队列（SPSC/MPSC）与内存序
- 异步日志框架
- 网络 socket 封装

这是从“语言层面优化”过渡到“系统组件实现”的关键一章，也是后续交易所和客户端策略的基石。

---

## 七、推荐学习节奏

| 时间 | 内容 |
|---|---|
| 第 1 天 | 第 1 小节：数据结构与算法选择 |
| 第 2 天 | 第 2 小节：缓存与内存访问成本 |
| 第 3 天 | 第 3 小节：类型选择与分支预测 |
| 第 4 天 | 第 4 小节：内联、编译时多态与异常 |
| 第 5 天 | 第 5 小节：编译器优化与 flag + 实践任务 |

---

## 八、本章核心金句

> “算法复杂度只是起点，内存访问模式才是决定延迟的关键。”

> “缓存未命中的代价，远高于一条指令的代价。”

> “在延迟敏感路径上，virtual 不是免费的，std::function 也不是。”

> “编译器优化能把好代码变快，但无法把坏架构变好。”

> “量化一切：用 perf、benchmark 和汇编验证你的假设，而不是凭感觉。”
