# Day 5：模板元编程基础 + STL 源码剖析

> **学习目标**：掌握模板特化、SFINAE、变参模板的核心概念，读懂 STL 核心容器的内部实现。
> **预计耗时**：3-4 小时

---

## 第一部分：模板元编程基础

### 1.1 模板特化（Specialization）—— 给特定类型开"后门"

```cpp
// 主模板（Primary Template）
template<typename T>
struct TypeInfo {
    static const char* name() { return "unknown"; }
};

// 全特化（Full Specialization）—— 给 int 定制版本
template<>
struct TypeInfo<int> {
    static const char* name() { return "int"; }
};

// 偏特化（Partial Specialization）—— 给任意指针类型定制版本
template<typename T>
struct TypeInfo<T*> {
    static const char* name() { return "pointer"; }
};

// 偏特化 —— 给任意常量类型定制版本
template<typename T>
struct TypeInfo<const T> {
    static const char* name() { return "const " + std::string(TypeInfo<T>::name()); };
};

// 使用时：
TypeInfo<double>::name();      // "unknown"（匹配主模板）
TypeInfo<int>::name();         // "int"（匹配全特化）
TypeInfo<int*>::name();        // "pointer"（匹配指针偏特化）
TypeInfo<const double>::name(); // "const unknown"（匹配 const 偏特化）
```

**匹配规则**：编译器选择"最特化"的版本。
- `TypeInfo<int*>` —— 主模板、指针偏特化都能匹配，**指针偏特化更特化**，选中它
- `TypeInfo<int>` —— 主模板、全特化都能匹配，**全特化最特化**，选中它

### 1.2 SFINAE —— 替换失败不是错误 ⭐

**SFINAE = Substitution Failure Is Not An Error**

```cpp
// 问题：如何让同一个函数名接受"有 .foo() 的类型"和"有 .bar() 的类型"？

// 版本 A：T 有 foo() 时才可用
template<typename T>
auto call_member(T& obj) -> decltype(obj.foo(), void()) {
    std::cout << "Called foo(): ";
    obj.foo();
}

// 版本 B：T 有 bar() 时才可用
template<typename T>
auto call_member(T& obj) -> decltype(obj.bar(), void()) {
    std::cout << "Called bar(): ";
    obj.bar();
}

struct WithFoo { void foo() {} };
struct WithBar { void bar() {} };

WithFoo a; call_member(a);  // 实例化版本 A 成功，版本 B 的 obj.bar() 替换失败 → 不是错误 → 选 A
WithBar b; call_member(b);  // 实例化版本 A 的 obj.foo() 替换失败 → 不是错误 → 选 B
```

**SFINAE 的核心思想**：当编译器尝试用某个类型实例化模板时，如果推导过程中发生替换失败，**不报错**，而是把这个候选从重载集合中移除，继续尝试其他重载。

```cpp
// std::enable_if 的实现
template<bool B, typename T = void>
struct enable_if {};

template<typename T>
struct enable_if<true, T> { using type = T; };

// 使用：
template<typename T>
typename std::enable_if<std::is_integral_v<T>, T>::type
absolute(T val) { return val < 0 ? -val : val; }
// 当 T 是整数时，enable_if<true, T>::type = T → 函数体有效
// 当 T 不是整数时，enable_if<false, T>::type 不存在 → 替换失败 → 从候选集移除
```

**SFINAE 到 C++20 Concepts 的演进**：

```cpp
// SFINAE (C++11) —— 难读
template<typename T>
auto f(T t) -> std::enable_if_t<std::is_integral_v<T>, void> { /* 整数版 */ }

// if constexpr (C++17) —— 稍好
template<typename T>
void f(T t) {
    if constexpr (std::is_integral_v<T>) { /* 整数版 */ }
    else { /* 非整数版 */ }
}

// Concepts (C++20) —— 最佳
template<std::integral T>
void f(T t) { /* 整数版 */ }
```

### 1.3 变参模板（Variadic Templates）—— 参数包展开

**核心语法**：

```cpp
// 定义：typename... Args 是模板参数包
template<typename... Args>
void print(Args... args) {  // args 是函数参数包
    // 展开方式 1：递归展开
    (print_one(args), ...);  // C++17 折叠表达式
}

// 递归展开模式（C++11/14 方式）
template<typename T>
void print(T val) {
    std::cout << val << "\n";  // 递归终止条件
}

template<typename T, typename... Args>
void print(T first, Args... rest) {
    std::cout << first << ", ";
    print(rest...);  // 每次递归剥去一个参数
}
```

**参数包展开的几种方式**：

```cpp
template<typename... Args>
void show(Args... args) {
    // 1. 折叠表达式（C++17）
    (std::cout << ... << args);        // 二元左折叠
    (args + ...);                       // 一元右折叠

    // 2. 初始化列表展开
    std::array<int, sizeof...(Args)> arr = {args...};

    // 3. 函数调用展开
    some_func(args...);

    // 4. 类型转换展开
    std::tuple<Args...> tup(args...);
}

// 查看参数个数
template<typename... Args>
void count() {
    constexpr size_t n = sizeof...(Args);  // 编译期获取参数个数
}
```

---

## 第二部分：STL 核心组件源码级理解 ⭐

### 2.1 std::vector —— 扩容策略深入分析

**核心内存模型**：三指针模型

```
vector<int> v:
┌──────────┐     ┌───┬───┬───┬───┬───┬───┬───┬───┐
│ _begin   │────→│ 1 │ 2 │ 3 │ 4 │   │   │   │   │
├──────────┤     └───┴───┴───┴───┴───┴───┴───┴───┘
│ _end     │────→ (指向最后一个元素的下一个位置)
├──────────┤
│ _cap_end │────→ (指向分配内存末尾的下一个位置)
└──────────┘
size()     = _end - _begin     = 4
capacity() = _cap_end - _begin = 8
```

**扩容算法**：

```cpp
// push_back 的简化逻辑
void push_back(const T& value) {
    if (_end == _cap_end) {         // 满了？
        size_t new_cap = size() == 0 ? 1 : size() * 2;  // 扩容因子 = 2
        T* new_data = allocate(new_cap);
        // 移动/拷贝旧元素到新内存
        for (size_t i = 0; i < size(); ++i) {
            construct(new_data + i, std::move_if_noexcept(_begin[i]));
        }
        deallocate(_begin, capacity());
        _begin = new_data;
        _cap_end = _begin + new_cap;
        _end = _begin + old_size;
    }
    construct(_end++, value);
}
```

**扩容因子为什么是 2（或 1.5）？**

> **核心权衡**：空间浪费 vs 均摊时间复杂度。

| 扩容因子 k | 每次扩容后浪费空间 | 为什么 |
|-----------|-------------------|--------|
| k = 2 | 最多 50% | 扩容后 size = old_cap + 1，capacity = 2×old_cap；浪费 ≈ (2×old_cap - old_cap - 1) / (2×old_cap) ≈ 50% |
| k = 1.5 | 最多 33% | 同理计算 ≈ 33% |
| k = 10 | 最多 90% | 极其浪费 |

**为什么大多数实现用 2（而非 1.5）？** 
- 2 是 2 的幂，分配器更容易优化（对齐友好）
- 均摊分析中每次 push_back 的均摊开销为常数 O(1)
- GCC libstdc++ 用 2，MSVC 用 1.5

**均摊分析**：虽然单次扩容是 O(N)，但把 N 次 push_back 的总开销平摊到每次：
```
总移动次数 = 1 + 2 + 4 + 8 + ... + N = 2N - 1 ≈ O(N)
均摊到每次 push_back = O(1)
```

### 2.2 std::deque —— 分段连续存储 ⭐

**问题**：`vector` 扩容时需要一块连续的大内存，且旧元素要全部移动。如果想要"两端都能高效插入且元素地址相对稳定"呢？

**deque 的设计**：分块存储

```
deque<int> d:
┌────────┐
│  map   │──→ ┌───┬───┬───┬───┬───┐  ← 中控器（指针数组）
├────────┤    │ ○ │ ○ │ ○ │   │   │
│ start  │    └─│───│───│───────────┘
├────────┤      │   │   │
│ finish │      ↓   ↓   ↓
└────────┘    ┌───┐┌───┐┌───┐
              │ 1 ││ 5 ││ 9 │   ← 每个 block 是一块连续内存
              │ 2 ││ 6 ││10 │      （通常 512 字节 或 4096 字节）
              │ 3 ││ 7 ││   │
              │ 4 ││ 8 ││   │
              └───┘└───┘└───┘
```

**关键设计**：
- 中控器（map）是指向块（block）的指针数组
- 每个块大小固定（通常是 512/sizeof(T)）
- **两端插入**：只需在 map 头/尾分配新块 —— O(1)
- **随机访问**：第 N 个元素 = map[N / block_size][N % block_size] —— O(1)，但比 vector 多一次间接寻址
- **元素地址相对稳定**：插入中间时会移动元素，但只移动少数块

### 2.3 std::map（红黑树）vs std::unordered_map（哈希表）

```cpp
// std::map 底层：红黑树
template<typename Key, typename Value, typename Compare = std::less<Key>>
class map {
    struct Node {
        Key key;
        Value value;
        Node* left;
        Node* right;
        Node* parent;
        bool is_red;       // 红黑树：红/黑标记
    };
    Node* root;
    // 操作复杂度：O(log n)，保证（最坏情况也是 O(log n)）
};

// std::unordered_map 底层：哈希表（通常用拉链法）
template<typename Key, typename Value, typename Hash = std::hash<Key>>
class unordered_map {
    struct Bucket {
        Node* head;        // 链表头（冲突元素形成链表）
    };
    std::vector<Bucket> buckets;  // 桶数组
    size_t element_count;
    // 操作复杂度：平均 O(1)，最坏 O(n)（哈希碰撞严重时）
};
```

**面试选择指南**：

| 场景 | 选择 | 原因 |
|------|------|------|
| 需要有序遍历 | `std::map` | 红黑树天然有序 |
| 需要范围查询 | `std::map` | `lower_bound`/`upper_bound` 是 O(log n) |
| 只做增删查 | `std::unordered_map` | 平均 O(1) |
| Key 是自定义类型 | 两者都可 | map 需实现 `operator<`，unordered_map 需实现 hash |
| 内存敏感 | `std::map` | 每个元素只需 3 个指针+颜色标记；unordered_map 需要桶数组+链表节点开销 |
| 极端稳定延迟 | `std::map` | 最坏情况 O(log n)；unordered_map 偶发 O(n) rehash |

### 2.4 std::string 的 SSO（Small String Optimization）

**问题**：每个 `std::string` 对象都要堆分配吗？

**SSO 回答：不！** 对于短字符串，直接存在对象内部（栈上），避免堆分配。

```cpp
// SSO 的实现示意（具体因标准库而异）
class string {
    union {
        char short_buf[16];    // SSO 缓冲区（短字符串时用）
        struct {
            char* data;        // 堆上分配的缓冲区（长字符串时用）
            size_t size;
            size_t capacity;
        } long_buf;
    };
    bool is_short;             // 区分短/长模式

    // 短字符串 (< 16 字节) → 直接存在 short_buf 中 → 零堆分配！
    // 长字符串 (>= 16 字节) → 堆分配，和 vector 类似
};
```

**SSO 的实际容量**（因实现而异）：

| 标准库 | SSO 容量 | sizeof(string) |
|--------|---------|----------------|
| libstdc++ (GCC) | 15 字符 | 32 字节 |
| libc++ (Clang) | 22 字符 | 24 字节 |
| MSVC STL | 15 字符 | 32 字节 |

> **面试要能说出来**：SSO 为什么重要？——绝大多数程序中的字符串都是短字符串（属性名、日志消息、JSON key），SSO 让它们完全不需要堆分配，大幅减少内存碎片和分配开销。

### 2.5 迭代器失效场景总结 ⭐ 面试高频

| 容器 | 操作 | 哪些迭代器失效 |
|------|------|---------------|
| `vector` | `push_back`（发生扩容） | 所有迭代器（因为内存重新分配了） |
| `vector` | `push_back`（没扩容） | `end()` 失效，其他不变 |
| `vector` | `insert`/`erase` | 插入点**之后**的迭代器全部失效 |
| `deque` | 两端插入 | 指针/引用有效，但 `begin()`/`end()` 可能失效 |
| `deque` | 中间插入/删除 | 所有迭代器失效 |
| `list` | 插入 | 所有迭代器有效 ✅ |
| `list` | 删除 | **只有被删除元素的迭代器失效** |
| `map/unordered_map` | 插入 | 所有迭代器有效 ✅ |
| `map/unordered_map` | 删除 | **只有被删除元素的迭代器失效** |

**关键原则**：
- 连续存储容器（vector）扩容时所有迭代器失效（因为内存搬家了）
- 节点容器（list/map）的迭代器在插入时永不失效，删除时只有被删元素的失效
- `deque` 有一定特殊性：两端插入不破坏现有元素地址

---

## 检验问题

### 基础题

**Q1：解释 SFINAE 的全称和含义，并举一个实际应用场景。**

<details>
<summary>点击查看答案</summary>

SFINAE = Substitution Failure Is Not An Error（替换失败不是错误）。

**含义**：模板实例化时，如果尝试某个候选模板时发生了类型替换失败（没有该成员函数、类型不匹配等），编译器不报编译错误，而是将该候选从重载集中移除，继续尝试其他候选。

**应用场景**：实现"根据类型是否有某成员函数来选择不同重载"。例如，一个通用的 `to_string` 函数，对有 `.to_string()` 方法的类型用一种实现，对整数用另一种实现。
</details>

**Q2：`std::vector` 扩容时，元素是拷贝还是移动？什么情况下会回退到拷贝？**

<details>
<summary>点击查看答案</summary>

**优先移动**（如果移动构造是 `noexcept` 的）。因为 `std::vector` 提供强异常保证——扩容失败时保证原始数据不变。

如果移动构造**没有标记 `noexcept`**，vector 无法保证移动过程不抛异常，就回退到拷贝（拷贝后即使新内存操作失败，旧内存数据也完好）。

这就是为什么移动构造函数几乎永远应该标记 `noexcept`。
</details>

### 进阶题

**Q3：如果要实现一个"在任意位置插入/删除都不使迭代器失效"的容器，你会怎么设计？**

<details>
<summary>点击查看答案</summary>

**方案**：采用链表式存储结构（如 `std::list` 的双向链表），每个元素是独立的节点分配，插入/删除只修改相邻节点的指针，不影响其他元素的地址。

**内存上的代价**：失去了缓存局部性（CPU cache 效率差），每次访问都是指针追踪，可能触发 cache miss。这就是为什么在实际应用中 `std::vector` 往往比 `std::list` 快——即使在 O(N) vs O(1) 的理论复杂度上看似劣势。

**折中方案**：`std::deque` 的分块结构——元素地址相对稳定（只有中间插入时才部分失效），同时保留了一定的缓存局部性。
</details>

**Q4：SSO 是什么？为什么它对性能至关重要？你能设计一个实验来验证你的 std::string 是否启用了 SSO 吗？**

<details>
<summary>点击查看答案</summary>

**SSO（Small String Optimization）**：对于短字符串，直接将其存储在 `std::string` 对象内部（栈上），避免堆分配。

**验证实验**：
```cpp
std::string s;
std::cout << "size: " << sizeof(s) << "\n";  // 如 32 字节

const char* p = s.data();   // 记录初始数据指针
for (int i = 0; i < 30; ++i) {
    s += 'x';
    const char* q = s.data();
    if (q != p) {
        std::cout << "指针变化！第 " << i << " 个字符时发生了堆分配\n";
        break;
    }
}
// 如果前 15 个字符时指针不变（即 SSO 容量 15），之后指针变化（触发堆分配），说明 SSO 启用了
```
</details>

### 综合题

**Q5：给出一段代码，用变参模板 + 折叠表达式实现一个类型安全的 `printf`。**

<details>
<summary>点击查看答案</summary>

```cpp
#include <iostream>
#include <string_view>

// 递归终止
void safe_print(std::string_view fmt) {
    std::cout << fmt;
}

// 递归展开：每次从 fmt 中找一个 {}，用第一个参数替换
template<typename T, typename... Args>
void safe_print(std::string_view fmt, T&& first, Args&&... rest) {
    auto pos = fmt.find("{}");
    if (pos == std::string_view::npos) {
        std::cout << fmt;  // 没有 {} 了
        return;
    }
    std::cout << fmt.substr(0, pos);  // 输出 {} 之前的部分
    std::cout << std::forward<T>(first);  // 输出参数
    safe_print(fmt.substr(pos + 2), std::forward<Args>(rest)...);
}

// 使用
safe_print("Hello, {}! You have {} new messages.\n", "World", 5);
// 输出: Hello, World! You have 5 new messages.
```

关键点：不需要 `printf` 的 `%d`/`%s` 格式指定符——编译器自动推导参数类型。如果参数个数和 `{}` 不匹配，也只在运行时暴露（`std::format` 在编译期就检查）。
</details>

---

**下一步**：[Day 6：周综合复习与模拟面试](day6-weekly-review-qa.md)
