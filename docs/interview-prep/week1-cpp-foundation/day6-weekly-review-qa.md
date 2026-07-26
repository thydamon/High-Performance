# Day 6：周综合复习与模拟面试

> **学习目标**：通过 20 道模拟面试题检验本周学习成果，查缺补漏。每道题都应能脱稿口述回答。
> **预计耗时**：3-4 小时

---

## 第一部分：知识体系回顾（15 分钟）

在开始做题之前，先用纸笔画出本周的知识体系思维导图：

```
C++ 深度系统化
├── 编译链接
│   ├── 四阶段：预处理 → 编译 → 汇编 → 链接
│   ├── 静态链接 vs 动态链接（GOT/PLT 延迟绑定）
│   └── 每个 .cpp 独立编译（翻译单元）
├── 内存模型
│   ├── 进程地址空间：.text / .rodata / .data / .bss / heap / stack
│   ├── 对齐与填充
│   └── 字节序（大端/小端）
├── 面向对象核心
│   ├── vtable 布局：单继承 / 多重继承 / 虚继承
│   ├── this 指针调整（thunk）
│   ├── dynamic_cast 原理与 RTTI
│   └── RAII / Rule of Three / Five / Zero
├── C++11/14/17 现代特性
│   ├── 移动语义（右值引用、万能引用、引用折叠）
│   ├── 智能指针（unique_ptr / shared_ptr / weak_ptr）
│   ├── Lambda 与 std::function（类型擦除）
│   └── variant / optional / string_view / if constexpr
├── C++20 前沿
│   ├── Concepts（约束模板参数）
│   ├── Ranges（管道式操作）
│   ├── Coroutines（co_await / co_yield / co_return）
│   └── constexpr 增强 / <=> 运算符
└── STL 源码
    ├── vector：三指针 + 扩容因子
    ├── deque：中控器 + 分块
    ├── map：红黑树 vs unordered_map：哈希表
    └── string SSO + 迭代器失效规则
```

---

## 第二部分：模拟面试题（20 题）

### 编译链接与内存（Day 1 相关知识）

**1. 从 `main.cpp` 到可执行文件经历了哪些阶段？每个阶段的核心工作是什么？**

<details>
<summary>点击查看答案</summary>

四个阶段：
1. **预处理**（`g++ -E`）：宏替换、头文件展开、条件编译、删除注释。`#include` 本质是递归文本复制。
2. **编译**（`g++ -S`）：词法→语法→语义分析，生成汇编代码。每个 .cpp 独立编译，只看到本翻译单元的内容。
3. **汇编**（`g++ -c`）：汇编→机器码，生成 .o 目标文件。包含代码段、数据段、符号表、重定位表。
4. **链接**（`ld`）：符号解析（匹配未定义符号与定义符号）+ 重定位（分配最终地址）+ 段合并。最终生成可执行文件。
</details>

**2. 解释 GOT 和 PLT 在动态链接中的作用。为什么第一次调用 `printf` 比后续调用慢？**

<details>
<summary>点击查看答案</summary>

- **GOT**（Global Offset Table）：存储全局符号的运行时绝对地址
- **PLT**（Procedure Linkage Table）：每个动态链接函数的跳板

**首次调用**：`call printf@plt` → PLT 中 `jmp *GOT[printf]`，此时 GOT 指向 PLT 中解析代码 → 触发 `_dl_runtime_resolve` 查找 printf 真实地址并写入 GOT → 再跳转到 printf。涉及动态链接器的符号查询（字符串匹配），开销较大。

**后续调用**：`call printf@plt` → PLT 中 `jmp *GOT[printf]`，GOT 已经指向 printf 真实地址。直接跳转，一条额外的间接跳转指令开销，几乎忽略不计。

这就是"延迟绑定"（Lazy Binding）——只为实际用到的符号付出解析开销。
</details>

**3. 解释 `.bss` 段的特殊之处。`int a = 0;` 和 `int a;` 在全局作用域下有什么区别？**

<details>
<summary>点击查看答案</summary>

`.bss` 段存放未初始化或初始化为 0 的全局/静态变量，**不在可执行文件中占空间**——只记录大小，加载时 OS 映射零页（zero-fill-on-demand）。

`int a = 0;` 和 `int a;` 在全局作用域下**没有区别**——两者都放在 `.bss`。编译器把显式初始化为 0 优化为"没必要在文件中存 4 字节的零"。

在局部作用域下，`int a;` 是**未初始化**的（值不确定），`int a = 0;` 是初始化为 0——有本质区别。但这是栈上的行为，与段的讨论无关。
</details>

### 面向对象与虚函数（Day 2 相关知识）

**4. 虚函数表在单继承、多重继承、虚继承三种情况下分别如何布局？**

<details>
<summary>点击查看答案</summary>

**单继承**：派生类 vtable 拷贝基类 vtable 布局，覆盖的函数替换指针，新增的函数追加到末尾。vptr 在对象开头。

**多重继承**：每个基类在对象中有自己的子对象和对应的 vptr。派生类 vtable 按基类分别存放，覆盖的函数替换对应基类位置，新增的函数挂在第一个基类的 vtable 后面。派生类指针转基类指针时可能需要**this 指针调整**（thunk）。

**虚继承**：虚基类子对象放在派生类对象末尾（而不是每个路径一份）。vtable 中包含 virtual base offset 来定位虚基类。访问虚基类成员需要两次间接：vptr → offset → 成员。
</details>

**5. 为什么在构造函数/析构函数中调用虚函数不会发生多态？**

<details>
<summary>点击查看答案</summary>

因为构造顺序是从基类到派生类。在基类构造函数执行期间，派生类部分尚未初始化，此时 vptr 指向当前正在构造的类的 vtable（基类的 vtable）。编译器故意这样做，防止虚函数访问尚未初始化的派生类成员。

同样，析构时从派生类到基类，在基类析构函数执行期间，vptr 已经从派生类 vtable 回退指向基类 vtable。

这是一种安全机制——编译器的"防守性"行为。
</details>

**6. `dynamic_cast` 的内部实现原理是什么？相比 `static_cast`，它的性能开销在哪里？**

<details>
<summary>点击查看答案</summary>

`dynamic_cast` 通过 vtable 第一项的 `type_info*` 获取运行时类型信息，然后**遍历整个继承树**进行类型比较（字符串比较 `type_info::name()`），复杂度 O(继承深度)。

相比 `static_cast`（编译期直接生成代码，零运行时开销），`dynamic_cast` 的开销在于：
1. 遍历继承树做字符比较（可能多次字符串比较调用）
2. 可能涉及 this 指针调整（计算 top_offset）
3. 完全无法内联优化

在性能敏感的代码路径中，应避免使用 `dynamic_cast`。可以用虚函数分发替代。
</details>

### 移动语义与智能指针（Day 3 相关知识）

**7. `std::move` 和 `std::forward` 的实现原理是什么？什么时候用哪个？**

<details>
<summary>点击查看答案</summary>

**`std::move`**：无条件转换为右值引用。本质是 `static_cast<std::remove_reference_t<T>&&>(t)`。它不"移动"任何东西，只是对类型做 cast。

**`std::forward`**：有条件转发——保持参数的原始值类别。用于万能引用（`T&&`）场景，配合引用折叠规则实现完美转发。

**使用选择**：
- 明确要放弃所有权 → `std::move`
- 万能引用模板参数的转发 → `std::forward`
- 对局部变量的 return → **什么都不加**（信任编译器 RVO/NRVO）

**陷阱**：对 `const` 对象 `std::move` 无效（移动构造不可用，回退到拷贝）；return 时加 `std::move` 会阻止 RVO。
</details>

**8. `shared_ptr` 的引用计数是线程安全的吗？被管理对象呢？举一个具体场景说明。**

<details>
<summary>点击查看答案</summary>

**引用计数（控制块操作）**：线程安全——`use_count` 的增减使用原子操作（`std::atomic` 的 fetch_add/fetch_sub）。

**被管理对象**：不是线程安全的——`shared_ptr` 不负责所管理对象的并发访问保护。

**shared_ptr 变量本身**：对同一个 `shared_ptr` 对象的并发读写（赋值、reset）是数据竞争——不是线程安全的。

```cpp
std::shared_ptr<int> global_sp;  // 全局 shared_ptr 变量

// 线程 A：读 global_sp
void reader() {
    auto local = global_sp;  // ❌ 如果线程 B 同时在写 global_sp，数据竞争！
}

// 线程 B：写 global_sp
void writer() {
    global_sp = std::make_shared<int>(42);  // ❌ 同时读写同一个 shared_ptr 对象
}

// 正确做法：用 atomic_shared_ptr（C++20）或加锁保护 global_sp 的读写
```
</details>

**9. 解释 `unique_ptr` 为什么能做到零开销（默认删除器的情况下）。EBO 是什么？**

<details>
<summary>点击查看答案</summary>

`unique_ptr<T>`（默认删除器 `std::default_delete<T>`）的大小 = **一个裸指针的大小**（64位系统为 8 字节）。

**EBO（Empty Base Optimization，空基类优化）**：C++ 保证空类的基类子对象在派生类中占 0 字节。`default_delete` 是一个空类（没有任何成员变量），通过 EBO 不会增加 `unique_ptr` 的大小。

```
sizeof(unique_ptr<int>) = sizeof(int*) = 8 字节
```

如果使用自定义的带状态的删除器（如捕获了变量的 Lambda），`unique_ptr` 的大小会增加。
</details>

### C++17/20 现代特性（Day 4 相关知识）

**10. 解释 `std::string_view` 的适用场景和危险陷阱。它和 `const std::string&` 有什么区别？**

<details>
<summary>点击查看答案</summary>

**`string_view`**：不拥有数据的"视图"——只有两个成员：`const char*` + `size_t`（共 16 字节）。不会触发任何内存分配或拷贝。

**优势场景**：函数参数（替代 `const std::string&`）、解析字符串、substr 零拷贝。

**核心危险**：`string_view` 不拥有数据！如果底层字符串被销毁，`string_view` 变成悬空引用。

```cpp
std::string_view get_bad() {
    std::string s = "hello";
    return s;  // ❌ s 返回后被销毁，string_view 悬空
}
```

**vs `const std::string&`**：传 `"literal"` 给 `const std::string&` 参数会触发临时 `std::string` 构造 + 堆分配；传 `string_view` 无此开销。
</details>

**11. Concepts 解决了什么问题？和 SFINAE、`enable_if`、`if constexpr` 的演进关系是什么？**

<details>
<summary>点击查看答案</summary>

**演进链**：SFINAE → `enable_if` → `if constexpr` → Concepts。每一步都提升了可读性和可用性。

**Concepts 解决的独特问题**：
1. **错误信息**：模板实例化失败不再输出几百行的模板堆栈，而是一行 "T 不满足 Concept 约束"
2. **重载歧义**：可对模板重载按 Concept 特化程度排序
3. **编译速度**：Concept 检查在实例化之前完成，提前排除不匹配的候选
4. **自文档化**：`template<std::integral T>` 比 `template<typename T>` 更清晰地表达了意图
</details>

**12. 解释 C++20 协程的三个关键词：`co_await`、`co_yield`、`co_return`。**

<details>
<summary>点击查看答案</summary>

| 关键词 | 含义 | 类比 |
|--------|------|------|
| `co_await` | 挂起当前协程，等待异步操作完成 | Python 的 `await` |
| `co_yield` | 挂起协程，向调用者返回一个值（用于生成器） | Python 的 `yield` |
| `co_return` | 返回最终结果，协程结束 | 普通函数的 `return` |

协程的价值是将异步回调转成同步写法，消除回调地狱。注意 C++20 只提供了协程的底层基础设施（promise_type、awaiter 等），没有提供现成的高级抽象（如 Python 的 `asyncio`）。生产使用通常依赖第三方库（如 cppcoro、folly::coro）。
</details>

### 模板与 STL（Day 5 相关知识）

**13. 什么是 SFINAE？给出一个在 `enable_if` 和 Concepts 两种方式下的实现对比。**

<details>
<summary>点击查看答案</summary>

SFINAE = Substitution Failure Is Not An Error（替换失败不是错误）。当模板实例化发生类型替换失败时，编译器不报错，而是将该候选从重载集中移除。

```cpp
// enable_if 方式（C++11）
template<typename T>
typename std::enable_if_t<std::is_integral_v<T>, void>
process(T val) { /* 整数处理 */ }

// Concepts 方式（C++20）
template<std::integral T>
void process(T val) { /* 整数处理 */ }
```

Concepts 版本在可读性、错误信息、重载排序上都远超 `enable_if`。
</details>

**14. `std::vector` 的扩容策略——为什么扩容因子通常是 2 或 1.5？为什么不是 10？**

<details>
<summary>点击查看答案</summary>

这是**空间浪费**与**均摊时间复杂度**的权衡。

均摊分析：扩容因子为 k，N 次 push_back 的总移动次数为 N + N/k + N/k² + ... = N × k/(k-1)。均摊开销 = k/(k-1)。

| 因子 k | 均摊移动次数 | 最大空间浪费 |
|--------|------------|------------|
| 2 | 2 | 50% |
| 1.5 | 3 | 33% |
| 10 | 1.11 | 90% |

**为什么不是 10**？虽然均摊移动次数更少（1.11 vs 2），但内存浪费高达 90%，对于大 vector 不可接受。实际选择 1.5-2 是在空间和时间之间的工程平衡。

**为什么微软用 1.5**？据说是因为 1.5 倍增长可以复用之前释放的内存块（在某些分配器策略下），减少内存碎片。
</details>

**15. `std::map` 和 `std::unordered_map` 各适合什么场景？面试时你如何快速回答这个问题？**

<details>
<summary>点击查看答案</summary>

**快速回答**：需要排序 → `map`；只做增删查 → `unordered_map`；内存敏感 → `map`；延迟敏感且可接受偶尔慢一点 → `unordered_map`。

| 维度 | `map` | `unordered_map` |
|------|-------|-----------------|
| 底层 | 红黑树 | 哈希表 |
| 有序性 | ✅ 有序遍历 | ❌ 无序 |
| 增删查复杂度 | O(log n) 保证 | 平均 O(1)，最坏 O(n) |
| 内存占用 | 每节点 3指针+颜色 | 桶数组 + 链表节点 |
| 缓存友好度 | 差（节点分散） | 差（链式存储） |
| 范围查询 | ✅ O(log n) | ❌ 不支持 |
| 自定义类型门槛 | 需 `operator<` | 需 hash 函数 + `operator==` |
</details>

---

## 第三部分：综合代码阅读题

**16. 以下代码输出什么？为什么？**

```cpp
#include <iostream>

class A {
public:
    A() { std::cout << "A"; f(); }
    virtual ~A() { std::cout << "~A"; }
    virtual void f() { std::cout << "fA"; }
};

class B : public A {
    int* p;
public:
    B() : p(new int(42)) { std::cout << "B"; }
    ~B() { std::cout << "~B"; delete p; }
    void f() override { std::cout << "fB(" << *p << ")"; }
};

int main() {
    A* obj = new B();
    obj->f();
    delete obj;
}
```

<details>
<summary>点击查看答案</summary>

**输出**：`AfABfB(42)~B~A`

逐步分析：

1. `new B()` → 先构造 A → `A()` 输出 `"A"`，然后调用 `f()`。此时 vptr 指向 A 的 vtable，`f()` 调用 `A::f()`，输出 `"fA"`
2. A 构造完成 → 构造 B → `B()` 输出 `"B"`
3. `obj->f()` → 虚函数调用，vptr 指向 B 的 vtable，调用 `B::f()`，输出 `"fB(42)"`
4. `delete obj` → 先析构 B → `~B()` 输出 `"~B"`
5. 再析构 A → `~A()` 输出 `"~A"`

**关键考点**：构造函数中虚函数的非多态行为（第 1 步 `f()` 输出 `"fA"` 而非 `"fB"`）。
</details>

**17. 以下代码有 bug 吗？如果有，怎么修复？**

```cpp
class Resource {
    FILE* f;
public:
    Resource(const char* path) : f(fopen(path, "w")) {}
    ~Resource() { if (f) fclose(f); }
    void write(const char* data) { fprintf(f, "%s", data); }
};

void process() {
    Resource r1("a.txt");
    Resource r2 = r1;
    r2.write("hello");
}
```

<details>
<summary>点击查看答案</summary>

**有严重 bug**：`r2 = r1` 触发了**浅拷贝**（编译器自动生成的拷贝构造函数），`r1.f` 和 `r2.f` 指向同一个 `FILE*`。

- `r2` 析构 → `fclose(f)` → 文件关闭
- `r1` 析构 → `fclose(f)` → **对已关闭的文件描述符再次调用 fclose → 未定义行为（Double Close）**

**修复方案**：
1. **禁止拷贝**：`Resource(const Resource&) = delete; Resource& operator=(const Resource&) = delete;`
2. **实现移动语义**（Rule of Five）：
```cpp
Resource(Resource&& other) noexcept
    : f(std::exchange(other.f, nullptr)) {}
```
3. **用 RAII 包装**：`std::unique_ptr<FILE, decltype(&fclose)> f;`
</details>

**18. 分析以下代码的 `sizeof` 和内存布局（64位系统）**

```cpp
class Base {
    virtual void f() {}
    int x;
};

class Derived : public Base {
    virtual void g() {}
    double y;
};

class Empty {};
class VirtualEmpty {
    virtual ~VirtualEmpty() = default;
};
```

<details>
<summary>点击查看答案</summary>

- `sizeof(Base)` = 8(vptr) + 4(int x) + 4(padding) = **16 bytes**
- `sizeof(Derived)` = 8(vptr) + 4(Base::x) + 4(padding) + 8(double y) = **24 bytes**
- `sizeof(Empty)` = **1 byte**（C++ 要求每个完整对象有唯一地址）
- `sizeof(VirtualEmpty)` = **8 bytes**（只有一个 vptr）

**关键点**：
- Base 的尾部 padding(4) 是用来保证数组中下一个元素的对齐
- Empty 类虽然是"空的"，但必须至少占 1 字节
- 一旦有了虚函数，vptr 占据 8 字节，上一条的 1 字节规则不再适用
</details>

**19. 下面这段代码执行后，`v` 的 `size()` 和 `capacity()` 是多少？**

```cpp
std::vector<int> v;
v.reserve(10);
for (int i = 0; i < 15; ++i) {
    v.push_back(i);
}
```

<details>
<summary>点击查看答案</summary>

最终：`size() = 15, capacity() = 20`

逐步分析：
1. `v.reserve(10)` → `capacity() = 10, size() = 0`
2. push_back 0~9 (10 次) → `size() = 10, capacity() = 10`
3. `push_back(10)` → 满了！扩容：`new_cap = 10 * 2 = 20`（GCC libstdc++ 用 2 倍因子）
4. push_back 11~14 (5 次) → `size() = 15, capacity() = 20`

**注意**：`reserve` 只改变 capacity，不改变 size。
</details>

**20. 以下代码会输出什么？**

```cpp
#include <iostream>

template<typename T>
void f(T&& val) {
    if constexpr (std::is_lvalue_reference_v<T>) {
        std::cout << "Lvalue: " << val << "\n";
    } else {
        std::cout << "Rvalue: " << val << "\n";
    }
}

int main() {
    int x = 42;
    f(x);
    f(100);
    f(std::move(x));
}
```

<details>
<summary>点击查看答案</summary>

```
Lvalue: 42
Rvalue: 100
Lvalue: 42      ← 注意！不是 Rvalue
```

**详细分析**：

- `f(x)`：`T` 推导为 `int&`（参数 `val` 类型：`int& &&` → 折叠为 `int&`）→ `is_lvalue_reference_v<int&>` = true → Lvalue
- `f(100)`：`T` 推导为 `int`（参数 `val` 类型：`int&&`）→ `is_lvalue_reference_v<int>` = false → Rvalue
- `f(std::move(x))`：`std::move(x)` 返回 `int&&`！但 `std::move(x)` **本身是一个有名字的表达式** → 它是一个**将亡值（xvalue）**。`T` 推导时，`int&&` 传进来后，`T` 推导为 `int`，**val 本身在函数体内是一个左值**（有名字的变量总是左值）。

等等——实际输出需要更精确地分析：

- `f(std::move(x))`：传入的实参类型是 `int&&`。`T&&` 匹配 `int&&`，`T` 推导为 `int`。此时 `is_lvalue_reference_v<int>` = false → 输出 "Rvalue: 42"。

实际上 `std::move(x)` 作为实参，其值类别是**xvalue**（将亡值），它匹配 `T&&` 时 `T` 推导为 `int`（非引用），所以输出的是 `Rvalue`。我之前的分析有误。

让我纠正：`f(std::move(x))` → 实参是 `int&&`（xvalue），`T` 推导为 `int`，`val` 类型为 `int&&`，`is_lvalue_reference_v<int>` = false → **"Rvalue: 42"**
</details>

---

## 第四部分：本周自检

完成全部 20 题后，根据答题情况标记掌握程度：

| 题目范围 | 题目编号 | 自评（✅/⚠️/❌）| 需要回顾的文件 |
|---------|---------|----------------|---------------|
| 编译链接 | 1, 2, 3 | __ | day1 |
| 虚函数布局 | 4, 5, 6 | __ | day2 |
| 移动语义 | 7 | __ | day3 |
| 智能指针 | 8, 9 | __ | day3 |
| C++17/20 | 10, 11, 12 | __ | day4 |
| 模板与STL | 13, 14, 15 | __ | day5 |
| 综合题 | 16-20 | __ | 综合 |

⚠️ = 能答出要点但不流畅；❌ = 需要重新学习对应章节。

---

**本周已完成！** 接下来进入 [第 2 周：操作系统 + 数据结构算法](../../../Interview_Prep_Plan_Senior_C++_Engineer.md#第-2-周操作系统--数据结构算法)。
