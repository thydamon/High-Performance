# malloc/free 内存分配与回收详解

> 目标：从用户态到内核态，完整理解一次 `malloc` 和 `free` 到底做了什么，以及为什么它们的延迟不可预测。

---

## 一、整体架构概览

一次 `malloc`/`free` 调用背后是多层协作：

```
应用程序
    ↓ 调用 malloc(size)
C++ new / malloc   (用户态入口)
    ↓
glibc ptmalloc / jemalloc / tcmalloc   (用户态分配器，核心层)
    ↓
brk() / mmap() / sbrk()                (系统调用，向内核要/还内存)
    ↓
Linux 内核虚拟内存管理 (VMA)
    ↓
物理页分配 (page fault 时触发)
```

下面以 Linux 下最常用的 **glibc ptmalloc** 为例，自下而上讲解。

---

## 二、底层层：内核如何管理内存

### 2.1 进程地址空间

Linux 下每个进程有自己的虚拟地址空间：

```
高地址
  ┌──────────────┐
  │   Stack      │ ← 向下增长
  ├──────────────┤
  │   ↓          │
  │   (空洞)     │ ← 动态 mmap 区域
  │   ↑          │
  ├──────────────┤
  │   Heap       │ ← brk() 控制的堆，向上增长
  ├──────────────┤
  │   BSS        │
  ├──────────────┤
  │   Data       │
  ├──────────────┤
  │   Text (代码) │
  └──────────────┘
低地址
```

### 2.2 两种获取内存的系统调用

malloc 向内核申请内存时使用两种系统调用：

| 系统调用 | 管理区域 | 特点 |
|---|---|---|
| `brk()` / `sbrk()` | 堆（Heap） | 连续增长，移动 program break，不释放具体块 |
| `mmap()` / `munmap()` | 动态映射区 | 独立映射，可以单独释放，适合大块 |

**`brk()` 的工作原理**：

```
初始状态:

Heap:  ┌──────────────────────┐
       │   已分配区域          │← program break (brk)
       └──────────────────────┘
       │   未映射区域          │← 如果访问则触发 segfault

sbrk(0x1000) 后 (扩展 4KB):

Heap:  ┌──────────────────────┐
       │   已分配区域          │
       ├──────────────────────┤
       │   新分配的 4KB 区域   │← brk 移动到这里
       └──────────────────────┘
```

- `brk(addr)`：直接把 program break 移动到指定地址；
- `sbrk(increment)`：在当前位置基础上增加 `increment` 字节；
- 释放比较麻烦：`brk` 只能整体回退，不能单独释放中间某一块——释放小块通常只是标记为"空闲"而不归还内核。

**`mmap()` 的工作原理**：

```
进程地址空间:

  ┌──────────────────────┐
  │   Stack               │
  ├──────────────────────┤
  │   (空洞)              │
  ├──────────────────────┤
  │   mmap 区域 (新映射)   │← mmap(NULL, size, PROT_READ|PROT_WRITE,
  │   独立的虚拟地址区域   │   MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)
  ├──────────────────────┤
  │   Heap                │
  └──────────────────────┘
```

- `mmap` 从"动态映射区"分配一段独立虚拟地址空间；
- `munmap` 可以把这段空间整体归还给内核；
- 每次 `mmap` 最少分配一个页（4KB），即使只请求 256 字节也是如此。

**两种策略的分工**：

| 分配大小 | 典型策略 |
|---|---|
| < 128 KB（默认阈值 `M_MMAP_THRESHOLD`） | 从 `brk` 管理的堆中分配 |
| ≥ 128 KB | 直接调用 `mmap` 分配并单独管理 |

### 2.3 物理页的延迟分配（Lazy Allocation）

关键点：`brk()` 或 `mmap()` 成功返回时，**物理页还没有真正分配！**

```
调用 mmap(4096) → 返回虚拟地址 0x7f0011000000

此时:
  虚拟地址 0x7f0011000000 → 合法可访问
  物理内存                → 还没有分配！

第一次访问 0x7f0011000000 时:
  1. CPU 触发 page fault（缺页异常）
  2. 内核接管：确实虚拟地址 → 对应 /dev/zero
  3. 内核分配一个 4KB 物理页（从 buddy allocator）
  4. 更新进程页表：虚拟地址 → 物理页
  5. 返回用户态，指令重试

延迟: 第一次访问的时间包含了页表填充 + 物理页分配 + TLB 刷新
```

这也是为什么 `malloc` 的第一次写访问有时会比后续写入慢很多——不是因为 `malloc` 慢，而是因为**物理页是延迟分配的**。

---

## 三、中间层：ptmalloc 分配器的核心数据结构

### 3.1 chunk — 内存块的基本单元

每个分配出去或空闲的内存块，在 ptmalloc 内部都用一个 `chunk` 结构管理：

```
分配的 chunk:
  ┌──────────────────────┐
  │ prev_size  (8 bytes) │  ← 前一个 chunk 的大小（如果前一个是空闲的）
  ├──────────────────────┤
  │ size       (8 bytes) │  ← 本 chunk 的大小，最低 3 bits 是标志位
  ├──────────────────────┤  ← mem 指针指向这里（返回给用户的地址）
  │           │
  │ user data (malloc 返回的用户可用区域)
  │           │
  └──────────────────────┘

空闲 chunk (free 后):
  ┌──────────────────────┐
  │ prev_size  (8 bytes) │
  ├──────────────────────┤
  │ size       (8 bytes) │
  ├──────────────────────┤
  │ fd   (8 bytes)       │  ← 单向链表：指向下一个空闲 chunk
  ├──────────────────────┤
  │ bk   (8 bytes)       │  ← 双向链表：指向上一个空闲 chunk
  ├──────────────────────┤
  │ (大型 bin 中还有)     │
  │ fd_nextsize (8 bytes)│  ← 指向下一个不同大小的空闲 chunk
  ├──────────────────────┤
  │ bk_nextsize (8 bytes)│
  └──────────────────────┘
```

**关键设计**：
- 空闲 chunk 的用户数据区域被复用存放链表指针 `fd`/`bk`——不浪费空间；
- `malloc` 返回的指针指向 `用户数据区`，而不是 `chunk` 头部；
- `size` 字段的最低 3 bits 用作标志位：

| 标志位 | 含义 |
|---|---|
| `PREV_INUSE (bit 0)` | 前一个 chunk 是否正在使用（0 = 空闲，1 = 使用中） |
| `IS_MMAPPED (bit 1)` | 这个 chunk 是否由 mmap 直接分配 |
| `NON_MAIN_ARENA (bit 2)` | 是否属于非主 arena |

### 3.2 arena — 分配区域

ptmalloc 将内存组织为多个 **arena**（分配区域），每个 arena 有独立的锁和数据结构：

```
进程:
  ┌─────────────────────────────┐
  │ main_arena (进程启动时创建)   │  ← 从 brk() 管理的主堆
  │   锁: arena->mutex          │
  │   bins: 一组空闲链表        │
  │   top chunk                 │
  ├─────────────────────────────┤
  │ arena 1 (按需创建)          │  ← 从 mmap() 映射
  │   锁: arena->mutex          │
  │   ...
  ├─────────────────────────────┤
  │ arena 2 ...                 │
  └─────────────────────────────┘
```

- 多个线程并发调用 `malloc` 时，每个线程会被分配到一个 arena；
- 同一个 arena 上的并发访问需要锁保护；
- arena 数量上限由 `MALLOC_ARENA_MAX` 控制。

### 3.3 bins — 空闲链表系统

ptmalloc 使用 **bins**（一组精心组织的空闲链表）管理空闲 chunk：

```
      ┌──────────────┐
      │  fastbins    │ ← 大小 16~80 字节的小块，LIFO，单链表，最快路径
      │  [16][24][32]│← 32
      │  [40][48][56]│← 56
      │  [64][72][80]│← 80
      ├──────────────┤
      │  smallbins   │ ← 大小 16~512 字节，FIFO，双向链表
      │  [ 32][ 48]  │   每个 bin 对应一个固定大小
      │  [ 64][128]  │
      │  [256][512]  │
      ├──────────────┤
      │  largebins   │ ← 大小 > 512 字节，按范围组织，双向链表 + 排序
      │  [512-576]   │   包含 fd_nextsize / bk_nextsize
      │  [576-640]   │
      │  [640-...]   │
      ├──────────────┤
      │ unsorted bin │ ← 刚 free 的块先放这里，后续分配时归类
      └──────────────┘
```

| bin 类型 | 大小范围 | 数量 | 数据结构 | 特点 |
|---|---|---|---|---|
| fastbins | 16–80 字节（步长 8） | 10 个 | 单向链表，LIFO | 查找最快，不合并，不清理 |
| smallbins | 16–512 字节（步长 16） | 62 个 | 双向链表，FIFO | 精确 fit，快速查找 |
| largebins | > 512 字节 | 63 个 | 双向链表，按大小排序 | 范围匹配，best-fit 搜索 |
| unsorted bin | 任意 | 1 个 | 双向链表 | 缓存刚 free 的块，延迟归类 |

### 3.4 top chunk — 最后的资源池

每个 arena 有一个 **top chunk**，位于 arena 的最高地址处：

```
Arena 布局:
  ┌──────────────────┐ ← arena 起始
  │  已分配 chunk 1   │
  ├──────────────────┤
  │  已分配 chunk 2   │
  ├──────────────────┤
  │  空闲 chunk       │  ← 在某个 bin 中
  ├──────────────────┤
  │  ...             │
  ├──────────────────┤
  │  ...             │
  ├──────────────────┤
  │  Top Chunk       │  ← 最后一大块，所有 bin 都找不到时从这里切割
  └──────────────────┘ ← arena 结束（或未映射边界）
```

- 当所有 bins 都找不到合适的空闲块时，从 **top chunk** 切割一块；
- 如果 top chunk 也不够大：
  - `main_arena`：调用 `sbrk()` 扩展 top chunk；
  - 其他 arena：调用 `mmap()` 分配新的子堆。
- 如果进行 `free` 的 chunk 紧邻 top chunk，会直接合并入 top chunk。

---

## 四、malloc 分配过程详解

### 4.1 完整流程图

```
用户调用 malloc(size)
      │
      ▼
  ┌─ 对齐 size 到 16 字节边界 ─┐
  │ (实际分配量 = chunk header + │
  │  size + 对齐填充)           │
  └────────────────────────────┘
      │
      ▼
  ┌─ size ≥ MMAP_THRESHOLD(128KB) ─┐
  │  是 → 调用 mmap() 分配           │
  │       │  设置 IS_MMAPPED 标志    │
  │       │  返回用户内存地址        │
  │       └─ 结束                   │
  │  否 → 继续                      │
  └─────────────────────────────────┘
      │
      ▼
  ┌─ 检查 fastbins ────────────────┐
  │  (size ≤ 80 字节)               │
  │  命中 → 从 fastbin 头部取 chunk │
  │        │  不检查合并             │
  │        │  返回用户内存地址      │
  │        └─ 结束                  │
  │  否 → 继续                      │
  └─────────────────────────────────┘
      │
      ▼
  ┌─ 检查 smallbins ───────────────┐
  │  (size ≤ 512 字节)              │
  │  命中 → 从 bin 尾部取 chunk     │
  │        │  FIFO 策略             │
  │        │  返回用户内存地址      │
  │        └─ 结束                  │
  │  否 → 继续                      │
  └─────────────────────────────────┘
      │
      ▼
  ┌─ 处理 unsorted bin ────────────┐
  │  遍历 unsorted bin：            │
  │  ┌─ 精确匹配？→ 直接返回       │
  │  └─ 不匹配？→ 根据大小放入     │
  │              smallbin/largebin │
  └─────────────────────────────────┘
      │
      ▼
  ┌─ 检查 largebins ───────────────┐
  │  (size > 512 字节)              │
  │  ┌─ 找到 ≥ size 的最小块        │
  │  │  (best-fit 策略)             │
  │  │  切割：如果剩余部分大于阈值  │
  │  │  切出需要的部分，剩余放回    │
  │  │  unsorted bin               │
  │  └─ 未找到 → 继续              │
  └─────────────────────────────────┘
      │
      ▼
  ┌─ 尝试 top chunk ───────────────┐
  │  top chunk 够大？               │
  │  是 → 从 top chunk 切割         │
  │  │   top chunk 地址下移         │
  │  │   返回用户内存地址           │
  │  └─ 结束                       │
  │  否 → 继续                      │
  └─────────────────────────────────┘
      │
      ▼
  ┌─ 扩展堆 ───────────────────────┐
  │  main_arena? → sbrk() 扩展     │
  │  其他 arena? → mmap() 扩展      │
  │  返回 NULL？→ 失败，errno=ENOMEM│
  └─────────────────────────────────┘
```

### 4.2 各阶段详解

**阶段 0：对齐计算**

`malloc(100)` 实际请求的可能不是 100 字节：

```
最小 chunk 大小 = 2 * SIZE_SZ + 2 * SIZE_SZ = 32 字节 (64位系统)
  - prev_size: 8 字节
  - size:       8 字节
  - fd:         8 字节 (空闲时复用)
  - bk:         8 字节 (空闲时复用)

100 字节请求:
  chunk_size = max(100 + 16(chunk开销), 32) = 128 字节 (对齐到 16)
```

实际返回给用户的区域是 128 − 16 = 112 字节可用空间（比请求的 100 还多，是内部碎片）。

**阶段 1：mmap 大对象**

对于大块内存（≥ 128KB），ptmalloc 不通过 bins 管理，而是直接调用 `mmap`：

- 优点：避免大块内存碎片化堆，`free` 时直接 `munmap` 归还内核；
- 缺点：每次 `malloc`/`free` 都要系统调用，内核态开销大；
- 可以通过 `mallopt(M_MMAP_THRESHOLD, ...)` 调整阈值。

**阶段 2：fastbins 快速路径**

这是最常见的分配路径（绝大多数小对象分配走这里）：

- fastbins 是 LIFO（后进先出）的单链表；
- 不合并，不检查相邻块状态——所以最快；
- 但可能导致碎片（已释放的小块不会立即合并）。

**阶段 3–5：smallbins / unsorted bin / largebins**

这些是"慢路径"，涉及更复杂的查找和合并逻辑：

- smallbins 是 FIFO（先入先出），按精确大小索引；
- unsorted bin 是"中转站"：刚 `free` 的 chunk 先放在这，下次 `malloc` 时再归类；
- largebins 按大小排序，使用 best-fit 策略——找不小于请求大小的最小空闲块。

**阶段 6–7：top chunk / 扩展堆**

所有 bins 都没有合适块时，最后的兜底策略：

- 从 top chunk 切割；
- top chunk 耗尽时，系统调用扩展：`sbrk()` 或 `mmap()`。

---

## 五、free 释放过程详解

### 5.1 完整流程图

```
用户调用 free(ptr)
      │
      ▼
  ┌─ ptr 是 NULL？→ 直接返回 ────┐
  └────────────────────────────────┘
      │
      ▼
  ┌─ 从 ptr 回推 chunk 地址 ──────┐
  │  chunk = ptr - 2*SIZE_SZ       │
  └────────────────────────────────┘
      │
      ▼
  ┌─ 检查 IS_MMAPPED 标志位 ──────┐
  │  是 → munmap() 归还内核        │
  │       结束                     │
  └────────────────────────────────┘
      │
      ▼
  ┌─ chunk 大小在 fastbin 范围？───┐
  │  是 → 放回 fastbin 头部 (LIFO) │
  │   │  不合并相邻 chunk          │
  │   └─ 结束                     │
  └────────────────────────────────┘
      │
      ▼
  ┌─ 检查前一个 chunk 是否空闲 ────┐
  │  (检查 PREV_INUSE 标志)        │
  │  空闲 → 从对应 bin 摘除前 chunk│
  │          合并为一个大 chunk    │
  └────────────────────────────────┘
      │
      ▼
  ┌─ 检查后一个 chunk 是否空闲 ────┐
  │  (通过 size 定位后 chunk,       │
  │   再检查后后 chunk 的           │
  │   PREV_INUSE 标志)              │
  │  空闲 → 从对应 bin 摘除后 chunk│
  │          合并为一个大 chunk    │
  └────────────────────────────────┘
      │
      ▼
  ┌─ 合并后的 chunk 紧邻 top chunk?│
  │  是 → 合并入 top chunk         │
  │  │    (内存被复用，不归还内核)  │
  │  否 → 放入 unsorted bin        │
  └────────────────────────────────┘
```

### 5.2 各阶段详解

**阶段 0：指针回推**

```
用户看到的:      ptr → [user data area]
chunk 头部: chunk → [prev_size][size][fd/bk...][user data area...]
                      ↑                        ↑
                     chunk                    ptr

chunk = (mchunkptr)((char*)ptr - 2 * SIZE_SZ)
```

`free(ptr)` 时，分配器需要从 `ptr` 反推出 chunk 的起始地址，才能读取 `size` 和标志位。

**阶段 1：mmap 大对象**

如果 `size` 字段的 `IS_MMAPPED` 位为 1，说明这是一个 mmap 分配的独立大块，直接 `munmap()` 释放——不需要 bins 操作。

**阶段 2：fastbin 快路径**

如果 chunk 大小在 fastbin 范围内（≤ 80 字节），直接放到对应 fastbin 的头部（LIFO）：

```
原来:  fastbin[48] → chunk_A → chunk_B
现在:  fastbin[48] → chunk_C → chunk_A → chunk_B
```

注意：fastbin 中的 chunk 的 `PREV_INUSE` 标志位**不修改**，意味着：
- 后续分配走 fastbin 时不检查合并；
- 只有当触发 `malloc_consolidate`（如从 smallbin 拿不到时）时才会合并 fastbin。

**阶段 3–4：合并相邻空闲块**

对于非 fastbin 大小的 chunk，需要检查前后相邻块是否空闲：

前方合并（前一个块空闲）：
```
检查当前 chunk 的 PREV_INUSE = 0（前面是空闲的）
  → 读 prev_size 知道前块大小
  → unlink(前块) 从对应 bin 取出
  → 当前 chunk = 前块 + 当前块（合并）
```

后方合并（后一个块空闲）：
```
当前块 size → 定位后块位置
检查后后块的 PREV_INUSE = 0（后块是空闲的）
  → unlink(后块) 从对应 bin 取出
  → 当前 chunk = 当前块 + 后块（合并）
```

**阶段 5：最终处理**

- 如果合并后的 chunk **紧邻 top chunk**：直接合并入 top chunk（有效复用，不归还内核）；
- 否则：放入 **unsorted bin**，等下次 `malloc` 时再归类到合适的 bin。

---

## 六、为什么延迟不可预测：一个完整的例子

假设以下时间线（实际发生顺序）：

```
T1: malloc(64)  → 命中 fastbin，30ns，完成

T2: malloc(64)  → 命中 fastbin，30ns，完成

T3: malloc(256) → fastbin 不匹配，查 smallbin，不匹配
                  → 处理 unsorted bin（此时为空）
                  → 查 largebin，不匹配
                  → 从 top chunk 切割，100ns，完成

T4: malloc(64)  → 命中 fastbin，30ns，完成

T5: malloc(1MB) → 超过 MMAP_THRESHOLD
                  → mmap() 系统调用
                  → 内核切换、VMA 注册、TLB flush
                  → 3000ns，完成

T6: malloc(64)  → 命中 fastbin，30ns，完成

T7: free(大多个) → 触发合并逻辑
                  → unlink、bin 调整、合并入 top chunk
                  → 250ns

T8: malloc(64)  → fastbin 空了！
                  → 查 smallbin，不匹配
                  → 处理 unsorted bin，发现大块，切割
                  → 800ns，完成
```

**同一个 `malloc(64)` 的延迟：30ns ~ 800ns，波动 25 倍以上。**

---

## 七、不同分配器对比

| 特性 | ptmalloc (glibc) | jemalloc (Facebook) | tcmalloc (Google) | mimalloc (Microsoft) |
|---|---|---|---|---|
| 线程缓存 | arena 级别 | per-thread tcache | per-thread cache | per-thread free list |
| 碎片控制 | 一般 | 较好（size class 精细） | 较好 | 较好 |
| 大对象释放 | 阈值以上 mmap | 同 | 同 | 同 |
| 内存归还内核 | 保守，很少主动归还 | 较积极 | 较积极 | 较积极 |
| 适用场景 | 通用 | 多线程服务端 | 多线程服务端 | 多线程 / 低延迟 |

**低延迟系统常见选择**：
- jemalloc：Facebook 为多线程服务端设计，per-thread cache 减少锁竞争；
- mimalloc：微软发布，专为低延迟和高并发设计，free lists sharding。

---

## 八、关键概念速查

| 概念 | 一句话解释 |
|---|---|
| chunk | ptmalloc 中内存块的基本结构，分配和空闲统一用 chunk 管理 |
| arena | 独立的内存分配区域，每 arena 有独立锁和 bins |
| fastbin | 小对象（≤ 80B）的快速缓存，LIFO 单链表，不合并 |
| smallbin | 中等对象（≤ 512B）的固定大小 bins，FIFO 双链表 |
| largebin | 大对象（> 512B）的范围 bins，best-fit 查找 |
| unsorted bin | 刚 free 的 chunk 的中转站，延迟归类 |
| top chunk | arena 末端的大空闲块，所有 bin 找不到时从这里切割 |
| mmap threshold | 超过此阈值的分配直接用 mmap，默认 128KB |
| Lazy Allocation | 系统调用成功 ≠ 物理页存在，首次访问时触发 page fault |
| sbrk() | 扩展/收缩堆的连续区域，main_arena 使用 |
| mmap()/munmap() | 独立映射虚拟内存区域，大对象和非主 arena 使用 |

---

## 九、常见面试/思考题

1. **`malloc(0)` 返回什么？**
   - glibc 返回一个合法的非 NULL 指针（最小 chunk 32 字节），但不应解引用。某些实现可能返回 NULL。

2. **`free(ptr)` 后 `ptr` 变成什么？**
   - 指针本身的值不变（还是之前的地址），但它变成了"悬垂指针"（dangling pointer），再次解引用是未定义行为。

3. **为什么 free 不需要传大小？**
   - 大小信息存储在 chunk 头部的 `size` 字段，`free` 通过 `ptr` 回推 chunk 地址读取大小。

4. **为什么 long-running 程序内存会越来越大？**
   - 碎片化 + 分配器保守策略：小块释放后不立即归还内核，只在 top chunk 收缩或 munmap 时归还。

5. **为什么低延迟系统用内存池而不是优化后的 malloc？**
   - 即使 jemalloc/tcmalloc 也无法彻底消除最坏情况的系统调用和锁等待，内存池提供完全可预测的 O(1) 分配。

