# Day 1：编译链接全流程与内存布局

> **学习目标**：理解从源码到可执行文件的全过程，掌握进程虚拟地址空间的每一块内存区域。
> **预计耗时**：3-4 小时

---

## 第一部分：编译与链接全流程

### 1.1 整体流程概览

源代码变成可执行文件，经历四个步骤：

```
main.cpp  ──→  预处理  ──→  编译  ──→  汇编  ──→  链接  ──→  a.out
 (源码)    (预处理器)    (编译器)    (汇编器)    (链接器)    (可执行文件)
              gcc -E       gcc -S      gcc -c       ld
```

```
main.cpp ──┐                               ┌── main.o ──┐
            │  预处理 → 编译 → 汇编          │            │
math.cpp ──┘                               └── math.o ──┼── 链接 → a.out
                                                        │
                                        libstdc++.so ──┘
```

### 1.2 预处理（Preprocessing）— "文本替换阶段"

**做了什么**：以 `#` 开头的指令在执行编译之前先进行文本替换。

```cpp
// 源码 main.cpp
#include <iostream>
#define MAX_SIZE 1024
#define SQUARE(x) ((x) * (x))   // 注意：宏的括号陷阱！

int main() {
    int arr[MAX_SIZE];
    std::cout << SQUARE(5 + 1) << std::endl;  // 展开为 ((5 + 1) * (5 + 1)) = 36
    return 0;
}
```

**预处理四大任务**：

| 任务 | 指令 | 本质 |
|------|------|------|
| 头文件展开 | `#include` | 递归地把头文件内容复制粘贴到当前位置 |
| 宏替换 | `#define` | 纯文本查找替换（不检查语法！） |
| 条件编译 | `#ifdef / #ifndef / #if / #else / #endif` | 选择性保留/丢弃代码块 |
| 删除注释 | `//` `/* */` | 注释替换为空格 |

**关键知识点**：

> **`#include` 的本质就是文本复制**。这也是为什么需要头文件保护（include guard）—— 同一个头文件被多个翻译单元 include，如果不保护，同一个文件中就会出现重复定义。

```cpp
// header.h 如果没有保护：
// 第一次 include → 展开内容
// 第二次 include → 再展开一遍 → 函数/变量重复定义，编译错误！

// 现代做法（推荐）：
#pragma once  // 编译器保证同一个物理文件只被包含一次

// 传统做法：
#ifndef HEADER_H
#define HEADER_H
// ...
#endif
```

**动手验证**：
```bash
g++ -E main.cpp -o main.i    # 只预处理，查看 .i 文件有多大
wc -l main.i                  # 通常几万行！（iostream 展开后巨大）
```

### 1.3 编译（Compilation）— "源码 → 汇编"

**做了什么**：将预处理后的 **翻译单元（Translation Unit）** 转换成 **汇编代码**。

这是一个完整的翻译单元模型：

```
翻译单元 = 一个 .cpp 文件 + 所有直接/间接 #include 的头文件
```

**编译阶段内部又分为**：

```
词法分析 → 语法分析 → 语义分析 → 中间代码生成 → 优化 → 目标代码生成
  │           │          │            │           │          │
把字符拆   构建AST    检查类型    生成平台无  各种优化   生成汇编
成Token   语法树     和作用域    关的IR       pass      .s 文件
```

**重点理解：每个 .cpp 文件独立编译**

```cpp
// a.cpp
int global_a = 42;
void foo() { /* ... */ }

// b.cpp
extern int global_a;   // 声明：告诉编译器"这个变量在别的翻译单元"
void foo();            // 声明：告诉编译器"这个函数在别的翻译单元"
```

编译器处理 `b.cpp` 时，不知道 `global_a` 和 `foo()` 的实际地址，只生成一个**重定位条目（Relocation Entry）**，留到链接时再填。

**动手验证**：
```bash
g++ -S main.cpp -o main.s    # 编译到汇编，阅读 .s 文件
g++ -S -O2 main.cpp          # 加 -O2 优化，对比汇编差异
```

### 1.4 汇编（Assembly）— "汇编 → 机器码"

**做了什么**：把汇编代码转换成 CPU 可执行的机器指令，产生 **目标文件（Object File, .o）**。

目标文件内部结构（ELF 格式）：

```
┌──────────────────────┐
│     ELF Header       │ ← 魔数(0x7F E L F)、架构(x86-64/ARM)、入口点
├──────────────────────┤
│   .text (代码段)      │ ← 函数的机器指令
├──────────────────────┤
│   .rodata (只读数据)   │ ← 字符串字面量、const 常量
├──────────────────────┤
│   .data (已初始化数据)  │ ← 已初始化的全局/静态变量
├──────────────────────┤
│   .bss (未初始化数据)   │ ← 未初始化的全局/静态变量（文件中不占空间！）
├──────────────────────┤
│   .symtab (符号表)     │ ← 导出/导入的符号（函数名、变量名）
├──────────────────────┤
│   .rel.text (重定位表) │ ← 代码段中需要链接器修补的位置
├──────────────────────┤
│   .rel.data (重定位表) │ ← 数据段中需要链接器修补的位置
└──────────────────────┘
```

**动手验证**：
```bash
g++ -c main.cpp -o main.o        # 编译+汇编到 .o
objdump -t main.o                 # 查看符号表
objdump -r main.o                 # 查看重定位表
objdump -h main.o                 # 查看段(section)布局
readelf -h main.o                 # 查看 ELF 头
nm main.o                         # 查看符号及其类型
```

### 1.5 链接（Linking）— "所有 .o + .a/.so → 可执行文件"

**做了什么**：把多个目标文件和库"缝合"在一起，解决符号引用，分配最终地址。

**核心任务**：

#### Step 1：符号解析（Symbol Resolution）

每一个目标文件都有**符号表**，记录了：
- **导出符号**（Defined）：本文件定义的函数/变量（如 `main`, `foo`）
- **导入符号**（Undefined）：本文件引用但未定义的符号（如 `printf`）

链接器读取所有 `.o` 文件和库，将每个"未定义"的引用匹配到一个"已定义"的符号。

```
main.o:  引用 printf、foo  ──→  未定义符号: printf、foo
foo.o:   定义 foo                               │
libc.so: 定义 printf                             │
                                                 ↓
                      链接器匹配: printf → libc.so, foo → foo.o
```

**常见错误理解**：
```
error: undefined reference to 'foo'
```
这不是"找不到声明"（那是编译错误），而是**编译通过了但链接时找不到定义**。

#### Step 2：重定位（Relocation）

链接器为每个符号分配最终运行时的**虚拟地址**，然后逐个修补所有重定位条目。

```
编译阶段生成的 .o:              链接后 .so / 可执行文件:
  call 0x00000000  (占位)  →    call 0x4005a0  (foo 的真实地址)
  mov  $0x0, %rax     →       mov  $0x601038, %rax  (global_a 的真实地址)
```

#### Step 3：段合并

```
main.o           math.o          合并后的可执行文件
┌───────┐      ┌───────┐       ┌───────────────┐
│ .text │  ──→ │ .text │  ──→  │ .text (合并)   │
│ .data │      │ .data │       │ .data (合并)   │
│ .bss  │      │ .bss  │       │ .bss (合并)    │
└───────┘      └───────┘       └───────────────┘
```

### 1.6 静态链接 vs 动态链接 ⭐ 面试高频

这是面试中几乎是必考的问题，且深度可以延伸到 GOT/PLT 机制。

#### 静态链接（Static Linking）

```bash
g++ main.cpp -static -o app     # 所有依赖复制进可执行文件
```

**特点**：
- 库的代码全部复制到可执行文件中，运行时**不依赖外部库**
- 文件大，但**启动速度快**（没有加载时符号解析开销）
- 库更新需要**重新编译**整个程序
- 每个使用该库的程序都有一份副本 → 内存浪费

#### 动态链接（Dynamic Linking）

```bash
g++ main.cpp -o app             # 默认动态链接
ldd app                         # 查看依赖哪些 .so
```

**特点**：
- 可执行文件只记录"我需要 libfoo.so"，运行时由**动态链接器**（`ld.so`）加载
- 文件小，多个进程可以**共享同一份** .so 的物理内存
- 库升级**不需重编译**程序（只要 ABI 兼容）
- **启动稍慢**（需要加载时做符号解析）

#### 延迟绑定与 GOT/PLT ⭐ 深入

动态链接有一个性能问题：如果一个程序导入了 1000 个动态库函数但只用到 10 个，加载时全部解析会很慢。

**解决方案：延迟绑定（Lazy Binding）**

```
调用 printf() 的流程（x86-64）：

第一次调用：
  call printf@plt ──→ PLT[printf]:
                        jmp *GOT[printf]     ← GOT 初始指向 PLT 的下一条指令
                        push reloc_index     ← 告诉动态链接器：我要解析 printf
                        jmp PLT[0]           ← 跳转到解析器
                          │
                          ↓ 动态链接器解析
                          │
                        GOT[printf] = printf 的真实地址

之后调用：
  call printf@plt ──→ PLT[printf]:
                        jmp *GOT[printf]     ← GOT 现在指向 printf 真实地址！
                                              （一步到位，零额外开销）
```

**关键数据结构**：

| 表 | 全称 | 作用 |
|----|------|------|
| **GOT** | Global Offset Table | 存储全局变量/函数的**运行时绝对地址**。`.got` 存变量地址，`.got.plt` 存函数地址 |
| **PLT** | Procedure Linkage Table | 每个动态链接函数对应一个 PLT 条目，作为跳板 |

**面试话术**："动态链接通过 GOT/PLT 实现延迟绑定——第一次调用时通过 PLT 触发动态链接器解析符号地址并写入 GOT 表项，后续调用直接通过 GOT 跳转，开销几乎为零。"

**补充——位置无关代码（PIC）**：

**问题**：多个进程加载同一个 `.so` 时，能不能共享 `.text` 段的物理内存？

如果不用 PIC，访问全局变量的指令会硬编码地址偏移：
```asm
mov  eax, [rip + 0x2000]   ; .data 地址硬编码在指令里
```
不同进程把 `.so` 加载到不同位置时，链接器必须**修改 .text 段中的地址**来适配——代码段被写脏了，每个进程都得有一份私有副本，无法跨进程共享物理内存。

用 PIC 后，所有地址访问**通过 GOT 间接完成**：
```asm
mov  rax, [rip + got_offset]  ; 相对偏移 → GOT 表项
mov  eax, [rax]               ; GOT 指向变量实际地址
```
代码段不包含任何绝对地址，加载时**完全不需要修改**，多进程可共享同一份 `.text` 物理页。每个进程只维护私有的 GOT + `.data`。

> **一句话**：PIC 让代码段自包含、不依赖加载地址，从而实现跨进程共享。x86-64 上 RIP-relative 寻址让 PIC 几乎零开销，所以强制要求。

```bash
g++ -fPIC -shared libfoo.cpp -o libfoo.so  # 编译动态库必须加 -fPIC
```

---

## 第二部分：内存模型与内存布局

### 2.1 进程虚拟地址空间全景 ⭐ 面试高频

```
高地址
  0x7fffffffffff ┌─────────────────────────┐
                 │    命令行参数/环境变量      │
                 ├─────────────────────────┤
                 │        栈 (Stack)         │ ← 向下增长（高地址→低地址）
                 │    局部变量、函数调用帧     │    rsp 寄存器指向栈顶
                 │          ↓↓↓             │
                 │                          │
                 │    (未分配区域/guard)       │ ← 栈溢出保护区间
                 │                          │
                 │          ↑↑↑             │
                 │        堆 (Heap)          │ ← 向上增长（低地址→高地址）
                 │    new/malloc 分配的内存   │    brk/sbrk 或 mmap
                 ├─────────────────────────┤
                 │       .bss 段             │ ← 未初始化全局/静态变量（全0）
                 ├─────────────────────────┤
                 │       .data 段            │ ← 已初始化全局/静态变量
                 ├─────────────────────────┤
                 │       .rodata 段          │ ← 只读数据（字符串字面量、const全局）
                 ├─────────────────────────┤
                 │       .text 段            │ ← 代码段（函数机器指令）
低地址   0x400000 └─────────────────────────┘
                 │       (NULL guard page)  │ ← 0x0 不可访问，捕获空指针解引用
      0x0        └─────────────────────────┘
```

### 2.2 各段详解

#### .text（代码段）
- 存放编译后的机器指令
- **只读、可共享**（多个进程运行同一程序时共享物理内存）
- `const char* p = "hello";`—— `p` 在栈上，`"hello"` 在 `.rodata`

```cpp
void func1() { /* ... */ }  // 机器码存在 .text
void func2() { /* ... */ }  // 机器码存在 .text
```

#### .rodata（只读数据段）
- 存放字符串字面量、`const` 修饰的全局变量
- 写操作会触发 **Segmentation Fault**

```cpp
const int global_const = 100;         // .rodata
const char* str = "Hello";            // "Hello" 在 .rodata, str 在栈上
char* bad = const_cast<char*>("Hi");  // 不推荐！修改会 segfault
```

#### .data（数据段）
- 存放**已初始化**的全局变量和静态局部变量
- **初始值直接存入可执行文件**（占用文件空间）

```cpp
int global_initialized = 42;       // .data (文件中存值为 42)
static int static_var = 100;       // .data
static int counter = 0;            // .data 还是 .bss？→ .bss（值为 0 也算未实质初始化）
```

#### .bss（Block Started by Symbol）
- 存放**未初始化**的全局/静态变量，或初始化为 0 的变量
- **不占用可执行文件空间**（只记录大小，加载时 OS 映射零页）

```cpp
int global_uninit;                 // .bss，文件中不占空间
static int buffer[1024 * 1024];    // .bss，1MB 数组不增大可执行文件
```

> **关键区别**：`int a = 0;` 也是 `.bss`，因为没必要在文件中存 4 字节的零——加载时直接映射零页就行。

#### 堆（Heap）
- 运行时通过 `new` / `malloc` 动态分配
- 起始位置由程序断点（`brk`）决定，向上增长
- `mmap` 映射的大块内存可能在堆和栈之间的任何位置

#### 栈（Stack）
- 存储局部变量、函数参数、返回地址、保存的寄存器
- **向下增长**（x86/x86-64 架构）
- 每个线程有自己的栈（通常 8MB，Linux 可通过 `ulimit -s` 调整）
- 函数调用时创建栈帧（Stack Frame），返回时销毁

```cpp
void func(int a, int b) {    // a,b 在调用者的栈帧或寄存器
    int local = a + b;       // local 在当前栈帧
    int* heap_var = new int; // heap_var(指针)在栈上，*heap_var 在堆上
}
```

### 2.3 对齐（Alignment）与填充（Padding）⭐ 易错点

CPU 访问对齐的数据更快（甚至有些架构必须对齐），编译器会自动插入填充字节。

```cpp
struct BadAlign {
    char  a;    // 1 byte, offset 0
    // padding: 3 bytes (让 int 4 字节对齐)
    int   b;    // 4 bytes, offset 4
    char  c;    // 1 byte, offset 8
    // padding: 3 bytes (让整个结构体大小是最大对齐要求的倍数)
    double d;   // 8 bytes, offset 16
};
// 总大小: 24 bytes（不是 1+4+1+8 = 14）

struct GoodAlign {
    double d;   // 8 bytes, offset 0
    int   b;    // 4 bytes, offset 8
    char  a;    // 1 byte, offset 12
    char  c;    // 1 byte, offset 13
    // padding: 2 bytes
};
// 总大小: 16 bytes（节省 33%！）
```

**对齐规则**：
1. 每个成员的偏移量必须是其大小的整数倍
2. 结构体总大小必须是最大成员对齐要求的整数倍
3. 可以用 `alignas(n)` 显式指定对齐，用 `alignof(T)` 查询

```cpp
alignas(64) struct CacheLineAligned {
    int data[4];
};
// 整个结构体对齐到 64 字节边界（防止伪共享）
```

### 2.4 字节序（Endianness）⭐ 半导体面试高频

|           | 小端（Little Endian） | 大端（Big Endian） |
|-----------|---------------------|-------------------|
| 定义      | 低位字节在低地址      | 高位字节在低地址    |
| 主流架构  | x86, x86-64, ARM(默认) | 网络字节序, 部分嵌入式 |
| 0x12345678 在内存中 | 78 56 34 12 | 12 34 56 78 |

```cpp
// 检测当前系统的字节序
bool is_little_endian() {
    uint16_t num = 0x0001;
    return *(uint8_t*)&num == 0x01;
}
// 或者 C++20: std::endian::native == std::endian::little
```

**字节序转换（面试常考）**：
```cpp
// 网络序（大端）↔ 主机序
uint32_t htonl(uint32_t hostlong);
uint16_t htons(uint16_t hostshort);
uint32_t ntohl(uint32_t netlong);
uint16_t ntohs(uint16_t netshort);
```

**半导体/ATE 行业特别关注**：与硬件设备通信（如 PCIe 寄存器读写）时必须处理字节序转换。ATE 测试向量数据也涉及跨平台字节序问题。

---

## 检验问题

### 基础题

**Q1：`#include <iostream>` 预处理后 .i 文件为什么有几万行？**

<details>
<summary>点击查看答案</summary>

`#include` 的本质是**递归文本复制**。`<iostream>` 本身会 include `<ios>`、`<streambuf>`、`<istream>`、`<ostream>` 等几十个头文件，每个又 include 更多，最终展开后相当于将大半个标准库的头文件都粘贴到了你的源文件中。用 `g++ -E main.cpp | wc -l` 可以看到实际行数。
</details>

**Q2：静态链接和动态链接各自适用什么场景？**

<details>
<summary>点击查看答案</summary>

**静态链接**适合：容器化部署（不想依赖宿主机 .so 版本）、嵌入式系统、对启动速度有极致要求
**动态链接**适合：桌面/服务器应用（节约磁盘和内存）、需要热更新库、使用 LGPL 协议的库
</details>

**Q3：`int a = 0;` 放在 `.data` 还是 `.bss`？为什么？**

<details>
<summary>点击查看答案</summary>

`.bss`。虽然显式初始化为 0，但编译器优化为"没必要在文件中存 4 字节的零"，加载时 OS 直接将 `.bss` 映射到零页即可。这减少了可执行文件大小。
</details>

### 进阶题

**Q4：GOT 和 PLT 的作用分别是什么？描述第一次调用 `printf` 的完整流程。**

<details>
<summary>点击查看答案</summary>

- **GOT（Global Offset Table）**：存储全局符号的运行时绝对地址
- **PLT（Procedure Linkage Table）**：每个动态链接函数对应一个 PLT 条目，作为跳板

第一次调用流程：
1. `call printf@plt` → 跳转到 PLT 条目
2. PLT 中 `jmp *GOT[printf]`，此时 GOT 指向 PLT 的下一条指令
3. `push reloc_index`，将重定位索引入栈
4. `jmp PLT[0]` 跳转到公共解析器
5. 解析器调用 `_dl_runtime_resolve` 查找 printf 真实地址并写入 GOT
6. 之后调用直接 `jmp *GOT[printf]` → printf，无额外开销
</details>

**Q5：下面结构体的大小是多少？如何优化？**

```cpp
struct S {
    char   c;    // 1
    double d;    // 8
    int    i;    // 4
    short  s;    // 2
};
```

<details>
<summary>点击查看答案</summary>

原始布局：`c[0] padding[7] d[8-15] i[16-19] s[20-21] padding[22-23]` = **24 bytes**

优化布局（按对齐要求降序排列）：
```cpp
struct S {
    double d;   // 8 (offset 0-7)
    int    i;   // 4 (offset 8-11)
    short  s;   // 2 (offset 12-13)
    char   c;   // 1 (offset 14)
    // padding: 1 byte → 16 bytes total
};
```
从 24 降到 16，节省 33%。
</details>

### 综合题

**Q6：写一个检测字节序的程序，并说明在半导体/ATE 设备通信中为什么字节序很重要。**

<details>
<summary>点击查看答案</summary>

```cpp
#include <cstdint>
#include <iostream>

bool is_little_endian() {
    uint16_t val = 0x0001;
    return *reinterpret_cast<uint8_t*>(&val) == 0x01;
}

// C++20
#include <bit>
// std::endian::native == std::endian::little

int main() {
    std::cout << (is_little_endian() ? "Little Endian" : "Big Endian") << "\n";
}
```

在半导体/ATE 场景中，ATE 设备通过 PCIe 与主机通信，PCIe 规范中某些字段使用 Little Endian（因为 x86 主导），而测试向量数据和 STDF 文件可能使用 Big Endian（网络序/历史惯例）。读取硬件寄存器时必须做字节序转换，否则测试数据完全错乱。
</details>

---

**下一步**：[Day 2：面向对象核心机制深入](day2-oop-vtable-inheritance.md)
