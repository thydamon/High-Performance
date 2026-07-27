# 第 2 周：操作系统 + 数据结构算法 — 详细学习指南

> **总目标**：深入操作系统原理（面试高频），同步启动 LeetCode 刷题计划。
>
> **本周定位**：OS 是 C++ 后端/高性能岗位的必考领域，数据结构算法是笔试和手撕代码环节的核心。两者并行推进，互为补充。
>
> **学习方式**：每天上午 OS 理论（2h）+ 下午/晚上刷题（1.5h），周末集中刷题。

---

## Day 1：进程与线程深度解析

> **核心目标**：彻底理解进程线程的本质区别、上下文切换开销、Linux 进程模型。

### 进程管理

- [ ] **进程状态转换图**
  - 三态模型（就绪 / 运行 / 阻塞）与五态模型（+ 新建 / 终止）
  - 挂起状态（Suspend）：就绪挂起 / 阻塞挂起的引入原因——内存不足时的交换（Swap）机制
  - 状态转换的触发条件：中断 / 系统调用 / 调度器决策
  - Linux 进程状态（`R / S / D / T / Z / X`）与 `/proc` 查看方法

- [ ] **PCB（进程控制块）核心字段**
  - `task_struct` 结构体关键字段：PID / state / mm（内存描述符） / fs / files / signals / thread_info
  - 进程标识符管理：PID 分配与回收、最大 PID 限制（`/proc/sys/kernel/pid_max`）
  - PCB 在内核中的存储：双向循环链表 + 哈希表

- [ ] **上下文切换的开销来源** ⭐ 高频考点
  - 直接开销：寄存器保存与恢复（通用寄存器、PC、栈指针、页表基址）
  - 间接开销：TLB 刷新 → 后续访存缺页 → Cache Miss 率上升
  - 线程上下文切换 vs 进程上下文切换：前者共享地址空间（无需切换页表），开销更小
  - 测量上下文切换时间：`lmbench` 中的 `lat_ctx` 工具
  - **面试话术**："上下文切换的瓶颈不在寄存器保存（几十条指令），而在 TLB 和 Cache 的温降——这是为什么无锁编程和绑核能带来显著性能提升。"

- [ ] **`fork()` / `exec()` 系列函数的内部原理**
  - `fork()`：复制 PCB → 分配新的 PID → 复制地址空间（Copy-on-Write 优化后延迟）
  - Copy-on-Write（COW）机制：父子进程共享物理页，标记为只读 → 写时触发缺页中断 → 复制页面
  - `vfork()`：更极致的优化——不复制页表，父进程阻塞直到子进程 `exec()` 或 `exit()`
  - `exec()` 系列（`execl` / `execv` / `execle` / `execve` / `execlp` / `execvp`）：替换当前进程的地址空间、堆栈、代码段
  - `clone()` 系统调用：`fork` 的底层实现，可通过标志位控制共享资源（`CLONE_VM` / `CLONE_FILES`）——线程的底层实现

- [ ] **僵尸进程 / 孤儿进程 / 守护进程 (Daemon)**
  - 僵尸进程：子进程已退出但父进程未 `wait()` → PCB 仍保留（`Z` 状态） → 资源泄漏
  - 孤儿进程：父进程先退出 → 子进程被 init/systemd 收养 → 不会变僵尸
  - 守护进程：后台运行、无控制终端、`setsid()` 创建新会话
  - **实战**：如何避免僵尸进程 —— `SIGCHLD` 信号处理 / `waitpid()` 循环 / 双重 `fork()` 技巧

### 线程模型

- [ ] **内核线程 vs 用户线程**
  - 1:1 模型（Linux NPTL）—— 每个用户线程对应一个内核线程，优点：真正的并行，缺点：创建/切换开销大
  - N:1 模型（GNU Pth）—— 用户级线程复用 1 个内核线程，优点：轻量，缺点：无法利用多核，一个阻塞全阻塞
  - M:N 模型—— 混合模型，Linux 已放弃（曾经在 2.4 内核尝试过）

- [ ] **`std::thread` vs `pthread` 对比**
  - `std::thread` 是 C++ 标准封装，底层调用 `pthread_create`（Linux）或 `CreateThread`（Windows）
  - `pthread` 提供更细粒度的控制：线程属性（`pthread_attr_t`）、分离状态（`detach`）、栈大小设置、CPU 亲和性
  - **值得深入的点**：`std::thread` 如何传递可调用对象？内部通过类型擦除 + 完美转发
  - 线程局部存储（TLS）：`__thread`（GCC） / `thread_local`（C++11）—— 每个线程独立副本，编译期通过 `__tls_get_addr` 或 `fs` 段寄存器偏移量访问

- [ ] **C++20 `std::jthread` 的改进**
  - 自动 `join()`：析构函数中调用 `join()`，避免忘记 join 导致 `std::terminate()`
  - 可中断性：通过 `std::stop_token` 协作式取消线程
  - **对比**：`std::jthread` 并不能强制终止线程，只能设置停止标志（类似 `atomic<bool> stop_flag`），线程需要主动检查

### 经验串联 💡

- 你在恒生电子的风控模块中，**按股票代码哈希分片 + CPU 绑定** 的设计本质上是 M:N 模型的工程实践——每个线程绑定一个核，避免上下文切换
- **面试话术**："在我设计的交易风控系统中，线程数 = CPU 核心数，通过 `pthread_setaffinity_np` 将线程绑定到特定核，消除了上下文切换和 TLB 刷新对延迟的影响。"

### 📖 推荐资料
- 《深入理解计算机系统》（CSAPP）第 8 章 —— 异常控制流
- 《Linux/UNIX 系统编程手册》第 24-29 章 —— 进程与线程
- `man 2 fork` / `man 7 pthreads` / `man 7 sched`

### 🛠 推荐练习
- 用 `strace -f` 跟踪 `fork()` + `exec()` 的系统调用序列
- 写一个小程序产生僵尸进程，然后用 `SIGCHLD` 处理消灭它
- 用 `pthread_create` 创建 100 个线程，观察 `/proc/<pid>/status` 中的 `Threads:` 变化

---

## Day 2：内存管理深度剖析

> **核心目标**：理解虚拟内存的底层机制，从分页到分配器的完整链路。

### 虚拟内存管理

- [ ] **分页机制（Paging）与多级页表**
  - 虚拟地址 → 物理地址的翻译：页号（VPN）→ 页表项（PTE）→ 物理帧号（PFN）+ 偏移量
  - 多级页表（x86-64 四级页表）：PML4 → PDPT → PD → PT → 4KB 页
  - 为什么需要多级页表：避免为整个虚拟地址空间创建一级页表（4KB 页面、48 位地址空间 → 一级页表需要 512GB）
  - 大页（Huge Pages）：2MB / 1GB 页，减少 TLB 缺失。透明大页（THP）vs 显式大页
  - **面试高频**："页表有多大？多级页表省了多少内存？配页表需要多少内存？"

- [ ] **TLB（快表）原理与 TLB Shootdown**
  - TLB 是页表的硬件 Cache，存储最近使用的 VPN → PFN 映射
  - TLB 缺失代价：x86 上约 10-100 个周期（命中）/ 数百到上千周期（缺失并查页表）
  - TLB Shootdown：多核 CPU 上，一个核修改页表后需要通知其他核刷新 TLB——通过 IPI（核间中断）实现，开销很大
  - ASID（Address Space Identifier）/ PCID：避免进程切换时刷新 TLB，允许不同进程的 TLB 条目共存

- [ ] **页面置换算法**
  - LRU（最近最久未使用）：理论最优近似，实现需要维护访问时间链表
  - Clock 算法（二次机会）：近似 LRU 的工程实现——使用位循环扫描，脏页写回
  - LFU（最不经常使用）：考虑访问频率，适合长期热数据
  - **Linux 实际使用**：`LRU 链表`（active_list + inactive_list） + `kswapd` 后台回收

- [ ] **缺页中断处理流程**
  - 硬件处理：CPU 发起到 MMU 的内存访问 → MMU 查 TLB 未命中 → 查页表 → PTE 的 Present 位为 0 → 触发 Page Fault
  - Minor Page Fault（轻微缺页）：页面已在物理内存中（如共享内存、COW 写时复制），只需建立映射
  - Major Page Fault（严重缺页）：页面在磁盘上（Swap 换出或 mmap 文件映射），需要 I/O 读取
  - **性能影响**：Major PF ≈ 几毫秒（磁盘 I/O），Minor PF ≈ 微秒级

### 内存分配器设计

- [ ] **`malloc` / `free` 内部实现**
  - ptmalloc（glibc 默认）：多 Arena + Per-thread Cache（tcache） + Fast/Small/Large bins
  - jemalloc：arena + tcache（per-thread） + 大小类别分级 + 内存池化，Facebook/Redis/Firefox 使用
  - tcmalloc：Thread Cache → Central Free List → Page Heap，Google 出品
  - **对比维度**：内存碎片、多线程扩展性、大块内存管理效率、内存归还速度
  - **面试高频**："频繁 `malloc/free` 为什么慢？"——系统调用（`brk`/`mmap`）、锁竞争、内存碎片化

- [ ] **内存池设计方法论回顾** ⭐ 结合你的实践经验
  - Thread Cache：每个线程独享的 Free List（无锁），小型对象缓存
  - Central Cache：全局 Central Free List（自旋锁保护），Thread Cache 不足时从 Central 批量获取
  - Page Heap：大块内存管理，向 OS 申请/释放虚拟内存
  - **面试话术**："在交易系统中我们采用了分层内存池设计——Thread Cache 无锁、Central Cache 细粒度锁、Page Heap 批量管理。针对交易场景对象集中在 64~256 字节的特点做了特化，P99 分配延迟从 1.2μs 降至 0.1μs。"

- [ ] **Slab 分配器（Linux 内核）**
  - 内核对象缓存：每个 Slab 由 1 个或多个物理页组成，存放同类型对象
  - 状态：满 → 部分空闲 → 完全空闲
  - **思想借鉴**：用户态内存池的 "大小类分级" 与 Slab 思想一脉相承

- [ ] **内存碎片**
  - 内部碎片（Internal Fragmentation）：分配的空间大于实际需求（如对齐导致的间隙）
  - 外部碎片（External Fragmentation）：空闲内存被分割成小块，无法满足大块连续分配
  - 解决方案：伙伴系统（Buddy System）解决外部碎片 + Slab/Size Classes 解决内部碎片

### 📖 推荐资料
- 《深入理解计算机系统》（CSAPP）第 9 章 —— 虚拟内存
- 《Linux 内核设计与实现》第 15-16 章 —— 页框管理 + 内存区管理
- jemalloc 源码阅读：重点看 arena/tcache 设计（`jemalloc/src/jemalloc.c`）

### 🛠 推荐练习
- 用 `pmap -x <pid>` 查看进程的内存映射分布
- 用 `numastat -p <pid>` 查看 NUMA 内存分配情况
- 写一个内存分配压力测试：多线程高频 `malloc/free`，对比 `glibc malloc` vs `jemalloc` 性能
- 打开 `/proc/<pid>/pagemap` 查看虚拟页到物理页的映射（需 root）

---

## Day 3：并发与同步机制

> **核心目标**：掌握所有面试常考的同步原语、锁机制、无锁编程概念。

### 锁与同步原语

- [ ] **`std::mutex` / `std::shared_mutex` / `std::recursive_mutex`**
  - `std::mutex`：独占所有权，非递归（同一线程第二次 lock 会死锁）
  - `std::shared_mutex`（C++17）：读写锁——`lock_shared()` 共享读，`lock()` 独占写
  - `std::recursive_mutex`：允许同一线程重复锁定（内部记录锁定次数），但意味着设计可能有问题
  - **底层实现**：Linux 上基于 `futex`（快速用户空间互斥锁），无竞争时在用户态自旋，有竞争时陷入内核睡眠
  - `std::lock_guard` / `std::unique_lock` / `std::scoped_lock`（C++17）：RAII 封装

- [ ] **自旋锁（Spinlock）vs 互斥锁**
  | 维度 | Spinlock | Mutex |
  |------|----------|-------|
  | 等待方式 | 忙等（CPU 空转） | 睡眠（上下文切换） |
  | 临界区长度 | 极短（< 几十条指令） | 任意长 |
  | 单核问题 | 需要关抢占，否则死锁 | 可用 |
  | 性能 | 无争用时极快 | 无争用时较慢 |
  | 适用场景 | 中断上下文 / 内核代码 | 用户态长时间等待 |

  - **自旋锁的实现**：`atomic_flag` + `test_and_set` 或 `CAS` 循环
  - **带超时自旋锁**：自旋 N 次后放弃，避免 CPU 空转

- [ ] **CAS（Compare-And-Swap）原子操作**
  - 硬件指令：x86 `CMPXCHG` / ARM `LDREX-STREX`
  - C++ 封装：`std::atomic<T>::compare_exchange_weak()` / `compare_exchange_strong()`
  - **Weak vs Strong**：Weak 可能在相同值时虚假失败（某些架构如 ARM LL/SC），需在循环中重试；Strong 保证不虚假失败

- [ ] **C++ `std::atomic` 内存序（`memory_order`）详解** ⭐ 高频难点
  - `memory_order_relaxed`：无跨线程顺序保证（计数器场景）
  - `memory_order_consume`：数据依赖排序（C++17 建议避免，实现有缺陷）
  - `memory_order_acquire`：Load 后的操作不会被重排到 Load 之前
  - `memory_order_release`：Store 之前的操作不会被重排到 Store 之后
  - `memory_order_acq_rel`：Load + Store 操作（RMW），acquire + release
  - `memory_order_seq_cst`：全局顺序一致（默认，最安全但最昂贵）
  - **典型例子**：生产者-消费者模型中的 `store(release)` + `load(acquire)`

- [ ] **虚假唤醒（Spurious Wakeup）与条件变量的正确用法**
  - 条件变量 `pthread_cond_wait` / `std::condition_variable` 可能在没有 notify 的情况下返回
  - **正确模式**：`while (!predicate) { cv.wait(lock); }` 而非 `if (!predicate)`
  - 为什么虚假唤醒存在：POSIX 标准允许，为了简化实现
  - `notify_one()` vs `notify_all()`：唤醒一个 vs 唤醒所有等待线程

### 无锁数据结构

- [ ] **Lock-Free 队列回顾** ⭐ 结合你项目中的实践
  - SPSC（Single Producer Single Consumer）：环形缓冲区 + 读写分离指针（每个 core 各写各的），无需 CAS
  - MPMC（Multi Producer Multi Consumer）：需要 CAS + ABA 问题处理
  - Dmitry Vyukov 的 Bounded MPMC 队列：基于环形缓冲区 + `sequence` 原子变量
  - **面试话术**："在风控模块中我们用了 SPSC 无锁队列连接行情接收和风控计算线程。SPSC 不需要 CAS，只需要对每个指针做 `store(relaxed)`/`load(acquire)` 即可——因为生产者和消费者各自操作不同的指针。"

- [ ] **ABA 问题的产生与解决**
  - ABA：CAS 期望值为 A，实际值从 A→B→A，CAS 误判为"未被修改"
  - 解决方案：Tagged Pointer（指针高位存版本号 / `uint128` 原子操作） / RCU
  - C++ `std::atomic` 中避免 ABA：`std::atomic<std::shared_ptr<T>>`（但 C++20 才提供）

- [ ] **Lock-Free Stack：Treiber Stack**
  - 基于单链表 + CAS 的栈：`push` = CAS 更新头指针，`pop` = CAS 摘下头节点
  - ABA 问题明显——用 Tagged Pointer 解决（`std::atomic<void*>` 不足以处理版本号）
  - **用 C++ 实现 Treiber Stack（重点练习）**

- [ ] **Hazard Pointer / Epoch-Based Reclamation**
  - 无锁数据结构的内存回收难题：不知道何时可以安全释放节点（其他线程正在访问）
  - Hazard Pointer（危险指针）：读者线程声明正在访问的指针，写者延迟释放
  - Epoch-Based Reclamation（EBR）：全局纪元计数，所有线程同一纪元内完成操作后才回收
  - **对比**：HP 适合读多写少，EBR 吞吐更高但实现更复杂

### 经验串联 💡

**整理你在恒生电子的无锁队列和并发内存池实践，准备 5 分钟技术亮点展示：**
- 场景：风控模块中行情数据从网络接收到多线程处理的完整数据流
- 挑战：P99 延迟要求在毫秒级，不能有任何锁竞争
- 方案：SPSC 无锁环形缓冲区 + Thread Cache 内存池 + CPU Affinity
- 效果数据：准备具体的 P50/P99/P999 延迟指标
- **反思点**："如果当时知道 Hazard Pointer，也许能更优雅地处理内存回收问题"

### 📖 推荐资料
- 《C++ Concurrency in Action》第 5-8 章 —— 内存模型、无锁数据结构
- 《Linux 多线程服务端编程》（陈硕）第 3 章 —— 多线程编程的最佳实践
- Dmitry Vyukov 的 Lock-Free 博客：`https://www.1024cores.net`

### 🛠 推荐练习
- 实现 SPSC Queue（环形缓冲区版本）
- 实现 Treiber Stack（含 Tagged Pointer ABA 保护）
- 用 `std::atomic` 实现一个自旋锁，对比其与 `std::mutex` 的性能差异

---

## Day 4：I/O 与零拷贝

> **核心目标**：系统化 I/O 模型知识，掌握零拷贝实现原理。

### I/O 模型

- [ ] **五种 I/O 模型的全景对比**

  | 模型 | 阻塞阶段 | 数据复制阶段 | 特点 |
  |------|---------|-------------|------|
  | 阻塞 I/O | 阻塞等待数据 | 阻塞复制到用户空间 | 简单但效率低 |
  | 非阻塞 I/O | 轮询检查（`EWOULDBLOCK`）| 阻塞复制 | 用户态轮询浪费 CPU |
  | I/O 多路复用 | `select/poll/epoll` 阻塞 | 阻塞复制 | 单线程管理千级连接 |
  | 信号驱动 I/O | 数据就绪发信号通知 | 阻塞复制 | 异步通知 + 同步复制 |
  | 异步 I/O（AIO）| 不阻塞 | 完成通知（不阻塞） | 真正的异步，复杂度高 |

- [ ] **`select` / `poll` / `epoll`（Linux）/ `IOCP`（Windows）对比** ⭐ 必考

  | 特性 | `select` | `poll` | `epoll` | `IOCP` |
  |------|---------|-------|---------|--------|
  | 最大连接数 | FD_SETSIZE（1024）| 无限制 | 无限制 | 无限制 |
  | 注册方式 | 每次调用传入全部 fd | 每次传入 pollfd 数组 | `epoll_ctl` 注册，`epoll_wait` 获取 | `CreateIoCompletionPort` |
  | 触发方式 | 水平触发（LT）| 水平触发（LT）| LT + ET（边缘触发）| 异步完成回调 |
  | 内部实现 | 轮询所有 fd（O(N)）| 轮询所有 fd（O(N)）| 回调机制（O(1)）| 完成端口队列 |
  | 跨平台 | POSIX | POSIX | Linux only | Windows only |

  - **epoll 水平触发（LT）vs 边缘触发（ET）**：
    - LT：只要 fd 还有数据可读，每次 `epoll_wait` 都返回 → 编程简单
    - ET：仅在状态变化时通知（不可读→可读），必须一次读完所有数据 → 需用非阻塞 I/O + 循环读（否则漏数据）
    - **面试话术**："生产环境我用 LT 模式——虽然每次 `epoll_wait` 可能重复通知，但代码更健壮。ET 模式在数据量很大时有性能优势但容易丢数据，需要非常小心地处理 EAGAIN。"

- [ ] **Reactor 与 Proactor 模式**
  - Reactor：事件多路分离 + 事件处理器（I/O 就绪后同步读写）。`epoll` 天然适合 Reactor
  - Proactor：异步操作发起 + 完成处理器（I/O 完成后通知应用）。IOCP 天然是 Proactor
  - **Reactor 的变体**：单 Reactor + 线程池、多 Reactor（主从 Reactor，如 Netty/Node.js）

### 零拷贝技术全景 ⭐ 高频考点

- [ ] **传统 I/O 路径的问题**
  - `read()` + `write()`：磁盘 → 内核缓冲区（DMA）→ 用户缓冲区（CPU）→ Socket 内核缓冲区（CPU）→ 网卡（DMA）—— **4 次上下文切换 + 2 次数据拷贝（CPU 参与）**

- [ ] **`mmap()` + `write()` 方案**
  - 内核缓冲区和用户空间共享同一块物理内存 → 减少一次 CPU 拷贝
  - 路径：磁盘 → 内核缓冲区（DMA）→ 通过共享映射被用户访问（无需 CPU 拷贝）→ Socket 内核缓冲区（CPU）→ 网卡（DMA）
  - **节省**：1 次 CPU 拷贝，但仍有 4 次上下文切换

- [ ] **`sendfile()` 系统调用**
  - 文件描述符 → Socket 描述符，完全在内核态完成——**0 次 CPU 拷贝**
  - 路径：磁盘 → 内核缓冲区（DMA）→ Socket 内核缓冲区（CPU 拷贝少量描述信息）→ 网卡（DMA）
  - 如果网卡支持 SG-DMA（Scatter-Gather DMA），可进一步减少到 **0 次 CPU 拷贝**
  - **限制**：源必须是文件，目标必须是 Socket（普通文件之间不行）
  - `splice()`：`sendfile` 的通用化——在两个 fd 之间移动数据，无需经过用户空间

- [ ] **`tee()` 系统调用**
  - 在两个管道 fd 之间拷贝数据（不消耗数据），常配合 `splice` 实现数据分流
  - 适用场景：日志同时写文件 + 发网络

- [ ] **DMA（直接内存访问）与内核旁路（Kernel Bypass）**
  - DMA：设备直接读写内存，无需 CPU 逐字节搬运
  - 内核旁路：绕过内核协议栈，用户态直接操作网卡
  - DPDK（Data Plane Development Kit）：Intel 推出的用户态网卡驱动库
    - UIO（Userspace I/O）机制：将网卡中断映射到用户态
    - 大页内存 + CPU 绑定 + 无锁环形缓冲区（rte_ring）
    - **场景**：在交易系统低延迟场景中，使用 DPDK 可以绕过内核协议栈，延迟从微秒级降到纳秒级
  - RDMA（Remote Direct Memory Access）：跨机器零拷贝——InfiniBand / RoCE / iWARP

### 经验串联 💡

**结合你项目中"基于零拷贝的数据流转方案使关键路径延迟降低 50%"：**
- **具体场景**：交易网关算子之间、以及算子与共享内存之间的数据传输
- **原来方案**：`malloc` + `memcpy` + `send`（4 次拷贝，2 次上下文切换）
- **优化后方案**：`mmap` 共享内存 + 无锁环形缓冲区传递数据描述符（指针 + 长度）
- **数据效果**：准备 P99 延迟的 before/after 数值
- **深度追问准备**："mmap 的页对齐限制怎么处理的？""数据可写时怎么办？（需分配新块，不能直接在内核缓冲区写）""Ring buffer 满了的背压策略？"
- **面试话术**："零拷贝的本质不是'不拷贝'，而是消除不必要的 CPU 参与拷贝。在我们的方案中，通过 mmap 共享内存 + 环形缓冲区传递数据引用，将数据路径上的 CPU 拷贝次数从 4 次降到了 0 次——数据直接从写线程的内存区域被读线程引用，不需要搬动。"

### 📖 推荐资料
- 《深入理解计算机系统》（CSAPP）第 10-11 章 —— 系统级 I/O、网络编程
- 《Linux/UNIX 系统编程手册》第 63 章 —— I/O 多路复用
- DPDK 官方入门指南：`https://doc.dpdk.org/guides/linux_gsg/`
- `man 2 sendfile` / `man 2 mmap` / `man 7 epoll`

### 🛠 推荐练习
- 用 `epoll` 写一个简单的 TCP echo server（LT + ET 各实现一次）
- 用 `sendfile` 实现一个静态文件服务器，并与 read+write 版本做性能对比
- 用 `perf stat -e context-switches` 测量两种方案的内核态切换次数

---

## Day 5：数据结构 — 面试高频考点精讲

> **核心目标**：掌握面试必考数据结构的核心原理，每个结构准备 3 句话面试话术。

### 高频数据结构

- [ ] **哈希表**
  - **开放定址法**：线性探测 / 二次探测 / 双重哈希。优点：空间利用率高；缺点：删除麻烦、聚集
  - **链地址法**：每个槽位一条链表/红黑树。优点：实现简单、容忍高负载因子
  - **`std::unordered_map` 陷阱**：
    - 迭代器失效：插入导致 rehash 时所有迭代器失效（C++11 保证 rehash 仅在负载因子超过阈值时发生）
    - 自定义 Key 需提供哈希函数 + `operator==`
    - 性能退化：哈希冲突严重时退化为链表 → C++11 开始部分实现使用红黑树优化长链表
  - **Resize 策略**：增长因子通常 2 倍，rehash 开销大——可预分配（`reserve()`）避免频繁 rehash
  - **面试话术**："哈希表是典型的时间换空间结构。当 Key 是简单整数且数据量可预估时，我选择开放定址法——缓存利用率更高。对于通用场景，链地址法更安全，配合好的哈希函数（如 `std::hash` 特化）可以保证 O(1) 平均复杂度。"

- [ ] **红黑树 vs AVL 树 vs B/B+ 树**
  | 特性 | 红黑树 | AVL 树 | B/B+ 树 |
  |------|-------|--------|---------|
  | 平衡条件 | 近似平衡（最长 ≤ 2× 最短）| 严格平衡（左右高度差 ≤ 1）| 所有叶节点同层 |
  | 插入/删除 | O(log N)，旋转少（≤ 3 次）| O(log N)，旋转多（可能到 log N）| O(log N) |
  | 查找 | O(log N)（略差于 AVL）| O(log N)（最严格）| O(log N)（树矮，磁盘 I/O 少）|
  | 适用场景 | 通用内存字典（`std::map`）| 查找极频繁的场景 | 数据库/文件系统（磁盘 I/O）|
  | 存储结构 | 二叉树 | 二叉树 | 多叉树（B+ 树叶节点链表）|

  - **红黑树的 5 条性质**：节点红或黑 / 根黑 / 叶（NIL）黑 / 红节点的子都是黑 / 任何节点到叶子的黑高相等
  - **B+ 树 vs B 树**：B+ 树只有叶节点存数据，内部节点只存索引 → 更大扇出（更矮） → 更适合范围查询和磁盘访问

- [ ] **跳表（Skip List）**
  - 原理：有序链表 + 多层索引（每层是下层的子集），查找时从顶层向下
  - 时间复杂度：期望 O(log N)，最坏 O(N)
  - 空间复杂度：O(N log N) / O(N)（索引节点平均 2 个指针）
  - **实际应用**：
    - Redis Sorted Set（ZSet）—— 跳表实现（为什么不用红黑树？更简单、支持范围查询、支持多线程无锁实现）
    - LevelDB / RocksDB MemTable——跳表（支持无锁并发写）
  - **面试话术**："跳表相比红黑树的优势在于实现简单且支持无锁并发。在 Redis 中，ZSet 使用跳表 + 哈希表的组合——跳表维护有序性（支持范围查询、排名），哈希表提供 O(1) 查找。这个组合设计值得借鉴。"

- [ ] **前缀树（Trie）**
  - 空间换时间的字符串查找结构，按字符（或字节）分叉
  - 适用场景：自动补全 / 字典 / IP 路由最长前缀匹配
  - **压缩变体**：Radix Tree（基数树）/ Patricia Trie —— 压缩单分支节点，Linux 页缓存和路由表中使用
  - **面试亮点**："在某些数据结构中可以结合 Trie 和哈希表的各自优势——Trie 做前缀匹配，哈希表做精确查找。"

- [ ] **布隆过滤器（Bloom Filter）**
  - 用 k 个哈希函数将元素映射到位数组的 k 个位置
  - **特性**：判断不存在是确定的（100%），判断存在有误判率（False Positive）
  - **核心设计参数**：位数组长度 m / 哈希函数数 k / 预期元素数 n → 误判率公式 `(1 - e^{-kn/m})^k`
  - **实际应用**：缓存穿透防护（布隆过滤器判断 key 不存在则直接拒绝查询 DB）/ 爬虫 URL 去重 / 垃圾邮件过滤
  - **升级**：Counting Bloom Filter（支持删除）/ Cuckoo Filter（支持删除 + 更低误判率）
  - **面试话术**："布隆过滤器不是万能的——它不能删除元素，误判率随数据量增大而上升。我在缓存穿透防护场景中使用它，配合缓存空值做兜底，误判率控制在 1% 以内。"

- [ ] **环形缓冲区（Ring Buffer）**
  - **核心结构**：固定大小数组 + 写指针（head）+ 读指针（tail），模运算环绕
  - **SPSC 优化**：每个指针各自一个 Cache Line（避免 False Sharing），写者只更新 head，读者只更新 tail
  - **边界处理**：空（head == tail）vs 满（(head + 1) % size == tail）—— 空一格检测
  - MPMC 扩展：每个槽位配 `sequence` 原子变量（Dmitry Vyukov 方案）

### 🛠 推荐练习（数据结构专项）
- 实现一个跳表（含查找/插入/删除）
- 实现一个带有布隆过滤器的缓存系统
- 实现 SPSC Ring Buffer（无锁版本）

---

## Day 6：算法题型分类刷题 + 本周复习

> **核心目标**：掌握高频算法题型，完成周回顾与模拟面试。

### LeetCode 分类刷题计划

#### 双指针 / 滑动窗口（5 题）
- [ ] **LeetCode 3** — 无重复字符的最长子串（滑动窗口 + HashSet）
- [ ] **LeetCode 11** — 盛最多水的容器（双指针）
- [ ] **LeetCode 15** — 三数之和（排序 + 双指针）
- [ ] **LeetCode 42** — 接雨水（双指针 / 单调栈）
- [ ] **LeetCode 76** — 最小覆盖子串（滑动窗口 + 计数）

> **面试重点**：滑动窗口的关键是窗口扩张和收缩的条件——收缩条件 = 窗口已满足目标约束，尝试缩小寻找最优解。

#### 二分查找及其变体（5 题）
- [ ] **LeetCode 33** — 搜索旋转排序数组（先找转折点，再二分）
- [ ] **LeetCode 34** — 在排序数组中查找元素的第一个和最后一个位置（边界二分）
- [ ] **LeetCode 69** — x 的平方根（二分逼近）
- [ ] **LeetCode 153** — 寻找旋转排序数组中的最小值（二分变体）
- [ ] **LeetCode 240** — 搜索二维矩阵 II（Z 字形搜索 / 行列二分）

> **面试重点**：二分查找的循环不变量 `[left, right]` 和 `[left, right)` 两种区间的差异，以及 `mid` 计算的溢出保护（`left + (right - left) / 2`）。

#### 链表操作（3 题）
- [ ] **LeetCode 25** — K 个一组翻转链表（Hard，高频手撕，分组反转 + 递归调用）
- [ ] **LeetCode 141** — 环形链表（快慢指针判环）
- [ ] **LeetCode 142** — 环形链表 II（找环入口——快慢指针相遇后从头同步走）

> **面试亮点**："环形链表 II 的数学原理——快慢指针相遇时，head 到环入口 = 相遇点到环入口的距离（考虑环长度）。手动推导这个公式。"

#### 树的遍历与构建（5 题）
- [ ] **LeetCode 94** — 二叉树的中序遍历（递归 + 迭代栈 + Morris）
- [ ] **LeetCode 102** — 二叉树的层序遍历（BFS 队列）
- [ ] **LeetCode 105** — 从前序与中序遍历序列构造二叉树（核心高频）
- [ ] **LeetCode 236** — 二叉树的最近公共祖先（递归回溯 LCA）
- [ ] **LeetCode 297** — 二叉树的序列化与反序列化（BFS/DFS 序列化）

> **面试重点**：从前序+中序重建二叉树的思路——前序第一个是 root，中序中 root 左边是左子树、右边是右子树，递归构建。**C++ 实现注意**：传递子数组的索引范围（`inL, inR, preL, preR`）而非拷贝子数组。

#### 动态规划经典题（5 题）
- [ ] **LeetCode 53** — 最大子数组和（Kadane 算法，一维 DP）
- [ ] **LeetCode 72** — 编辑距离（二维 DP，经典面试题）
- [ ] **LeetCode 1143** — 最长公共子序列（二维 DP 标准模板）
- [ ] **LeetCode 300** — 最长递增子序列（DP O(N²) + 贪心+二分 O(N log N)）
- [ ] **LeetCode 322** — 零钱兑换（完全背包，一维 DP 优化）

> **面试重点**：DP 三步法——① 状态定义（`dp[i]` / `dp[i][j]` 的含义）② 状态转移方程 ③ 边界初始化。**面试中先说暴力解法再优化**是得分技巧。

#### 图算法（3 题）
- [ ] **LeetCode 200** — 岛屿数量（DFS/BFS 图遍历，高频面试）
- [ ] **LeetCode 207** — 课程表（拓扑排序 Kahn 算法 / DFS 判环）
- [ ] **LeetCode 743** — 网络延迟时间（Dijkstra 最短路径，优先级队列实现）

> **面试重点**：拓扑排序的 Kahn 算法（入度表 + 队列）是高频手撕题。Dijkstra 用 priority_queue 实现，注意 C++ 默认最大堆——用 `greater<pair<int,int>>` 转为最小堆。

#### 多线程编程题（3 题）
- [ ] **LeetCode 1114** — 按序打印（`std::promise` / `std::atomic` 实现并发同步）
- [ ] **LeetCode 1115** — 交替打印 FooBar（条件变量 / 信号量）
- [ ] **LeetCode 1117** — H₂O 生成（信号量计数同步）

> **C++ 实现的考察重点**：`condition_variable` 的虚假唤醒处理、`std::atomic` 的正确内存序使用、`std::promise`/`std::future` 的线程间同步。

### 本周知识点回顾

- [ ] **OS 知识点自检清单**：
  - [ ] 能画出进程状态转换图并解释每个转换条件
  - [ ] 能解释 fork 的 COW 机制和上下文切换的主要开销
  - [ ] 能解释四级页表的翻译过程和大页的应用场景
  - [ ] 能对比三种内存分配器（ptmalloc/jemalloc/tcmalloc）
  - [ ] 能解释五种 I/O 模型和 epoll LT vs ET 的区别
  - [ ] 能画图解释零拷贝的完整数据流（mmap / sendfile / DPDK）
  - [ ] 能解释 CAS 的 ABA 问题及解决方案
  - [ ] 能写出正确的条件变量使用模式（while 循环 + predicate）

- [ ] **数据结构自检清单**：
  - [ ] 能对比红黑树 / AVL 树 / B+ 树的差异和适用场景
  - [ ] 能解释跳表的基本原理和为什么 Redis ZSet 用它
  - [ ] 能解释布隆过滤器的误判率和参数计算
  - [ ] 能写出 SPSC Ring Buffer 的核心实现

### 模拟问答练习

1. **"请描述 Linux 下 `fork()` 一个子进程时，内核做了哪些事？"**
   - 答：复制 `task_struct` → 分配 PID → 复制打开的文件描述符 → 复制信号处理 → 设置 COW 标记共享地址空间 → 将子进程加入调度队列 → 返回两次（子进程返回 0，父进程返回子进程 PID）

2. **"虚拟地址到物理地址的翻译过程是怎样的？四级页表怎么工作的？"**
   - 答：CPU 发出虚拟地址 → MMU 拆分为 PML4(9bit) → PDPT(9bit) → PD(9bit) → PT(9bit) → Offset(12bit) → 每级从 CR3 寄存器开始逐级查表 → 最终得到物理地址 → TLB 缓存结果加速下次访问。

3. **"`epoll` 的边缘触发（ET）和水平触发（LT）有什么区别？实际项目中你选哪种？为什么？"**
   - 答：LT 是只要数据没读完就重复通知；ET 只在状态变化时通知一次。我选 LT——编程更简单不易出错，ET 虽然高效但需要循环读取直到 EAGAIN，容易漏数据。在连接数 < 1 万时性能差距可以忽略。

4. **"零拷贝为什么能提升性能？mmap 和 sendfile 哪个更好？"**
   - 答：零拷贝消除了数据在内核空间和用户空间之间来回拷贝的 CPU 开销。`mmap` 适合重复读写同一份数据（如共享内存），`sendfile` 适合文件到 Socket 的传输（零上下文切换 + 零 CPU 拷贝）。如果网卡支持 SG-DMA，`sendfile` 可以实现真正的零 CPU 拷贝。

5. **"CAS 操作中的 ABA 问题是什么？怎么解决？"**
   - 答：ABA 问题的本质是 CAS 无法感知值从 A→B→A 的变化，误以为未被修改过。解决方案：带版本号的指针（Tagged Pointer，如 `uintptr_t` 高位存储版本号递增）或 RCU（读-拷贝-更新，通过宽限期保证不存在并发访问）。C++ 可以用 `std::atomic` 配合 `uint64` 同时存指针和版本号。

6. **"`std::vector` 扩容为什么通常用 2 倍或 1.5 倍？"**
   - 答：保证均摊 O(1) 的插入时间复杂度（每次扩容的拷贝次数 ≈ 已插入元素的总数）。2 倍是时间最优（最多浪费 50% 空间），1.5 倍是空间友好（因为 `1 + r + r² + ... < 1/(1-r)`，r=2 时均摊 2 次拷贝，r=1.5 时均摊 3 次但内存复用率更高）。Facebook 的 FBVector 用 1.5 倍并配合 `realloc` 提高内存复用。

### 本周薄弱点记录

| 知识点 | 掌握度 (1-5) | 备注 |
|--------|-------------|------|
| 进程线程模型 | | |
| 虚拟内存/分页 | | |
| 锁/同步/无锁 | | |
| I/O 模型/epoll | | |
| 零拷贝 | | |
| 哈希表/红黑树 | | |
| 跳表/布隆过滤器 | | |
| 算法题量（本周） | | 完成 ____ 题 |

### 本周刷题进度总表

| 题型 | 计划 | 实际完成 | 掌握度 |
|------|------|---------|-------|
| 双指针/滑动窗口 | 5 | | |
| 二分查找 | 5 | | |
| 链表 | 3 | | |
| 树 | 5 | | |
| 动态规划 | 5 | | |
| 图 | 3 | | |
| 多线程 | 3 | | |
| **合计** | **29** | | |

---

## 📚 本周推荐资源汇总

### 书籍
- 《深入理解计算机系统》（CSAPP）第 8-11 章 —— 核心必读
- 《C++ Concurrency in Action》第 5-8 章 —— 并发必读
- 《Linux/UNIX 系统编程手册》相关章节 —— 系统编程参考

### 在线资源
- **刷题**：LeetCode Hot 100 + 剑指 Offer 精选
- **可视化学习**：`https://www.cs.usfca.edu/~galles/visualization/`（数据结构可视化）
- **内存管理**：`https://www.kernel.org/doc/html/latest/mm/index.html`
- **无锁编程**：Dmitry Vyukov 博客 `https://www.1024cores.net`
- **Linux 性能工具**：`perf` / `flamegraph` / `bcc` / `bpftrace`

### 练习项目建议
1. 用 `epoll` + 线程池写一个简易 HTTP server（复用零拷贝技巧）
2. 实现一个 SPSC Lock-Free Queue + Benchmark（对比加锁版本）
3. 实现一个简易内存分配器（`malloc`/`free` 的简化替代）

---

> 📅 **更新日志**：2026-07-27 基于主计划第 2 周内容扩展生成
>
> 🔄 **建议**：完成本周全部内容后，可进入第 3 周（跨进程通信 + Protobuf & RESTful API）学习
