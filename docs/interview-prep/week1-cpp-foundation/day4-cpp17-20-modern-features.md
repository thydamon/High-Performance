# Day 4：C++17/20 现代特性

> **学习目标**：掌握 C++17/20 引入的关键特性，理解它们"为什么"要引入，解决什么问题。
> **预计耗时**：3-4 小时

---

## 第一部分：C++17 关键特性

### 1.1 std::variant —— 类型安全的 union ⭐

**C++98 的 union**：类型不安全，不跟踪当前活跃成员，不自动调用析构函数。使用极其危险。

```cpp
// C++98 union 的问题
union Data {
    int i;
    double d;
    std::string s;  // C++98 不允许（有非平凡构造/析构）；C++11 允许但危险
};

Data d;
d.i = 42;    // 设置 int
d.d = 3.14;  // 设置 double —— int 值被覆盖，无警告！
// 更糟的是，如果 union 有 string 成员，析构不会自动调用 → 内存泄漏
```

**C++17 std::variant**：类型安全的联合体。

```cpp
std::variant<int, double, std::string> v;

v = 42;                         // 存储 int
v = 3.14;                       // 存储 double（int 被正确析构）
v = std::string("hello");       // 存储 string（double 被正确析构）
// string 析构时，variant 的析构函数自动调用 ~string() ✅

// 访问方式 1：std::get（错误类型抛 std::bad_variant_access）
double val = std::get<double>(v);

// 访问方式 2：std::get_if（安全版本，返回指针）
if (auto* p = std::get_if<int>(&v)) { /* p 指向 int */ }

// 访问方式 3：std::visit（函数式模式匹配）
auto visitor = [](auto&& arg) {
    using T = std::decay_t<decltype(arg)>;
    if constexpr (std::is_same_v<T, int>) std::cout << "int: " << arg;
    else if constexpr (std::is_same_v<T, double>) std::cout << "double: " << arg;
    else std::cout << "string: " << arg;
};
std::visit(visitor, v);
```

**内部实现**：`variant` 底层是一块足够大的栈缓冲区（大小为各类型中最大的），加上一个 `index` 字段标记当前活跃类型。是**栈**分配，不是堆分配。

### 1.2 std::optional —— 表示"可能不存在"的值 ⭐

```cpp
// 之前：用特殊值表示"无值"（歧义！）
int find_index() { return -1; }  // -1 表示没找到？还是真的返回了 -1？

// 之前：用指针表示（所有权不清晰！）
int* find_index();  // 返回新分配的内存？还是指向静态变量？谁负责 delete？

// C++17：语义明确
std::optional<int> find_index(const std::vector<int>& v, int target) {
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == target) return static_cast<int>(i);
    }
    return std::nullopt;  // 明确表示"没有值"
}

// 使用
auto result = find_index(vec, 42);
if (result) {  // 或 result.has_value()
    std::cout << "Found at: " << *result;  // 或 result.value()
}
int val = result.value_or(-1);  // 有值则返回，无值则返回 -1
```

**内部实现**：类似 `variant<T, std::monostate>`。一块缓冲区 + 一个 bool 标记。

### 1.3 std::string_view —— 零拷贝字符串引用 ⭐

```cpp
// 之前的痛苦
void parse_num(const std::string& str);  // 传引用避免了拷贝
parse_num(std::string("hello"));         // 但临时对象还是构造了

// C++17 string_view：不需要构造 std::string！
void parse_num(std::string_view sv);  // 只是指针+长度，零拷贝

parse_num("hello");                    // 直接传字符串字面量 ✅
parse_num(std::string("hello"));       // 从 std::string 隐式构造 ✅
parse_num(sv.substr(0, 3));            // substr 也是 O(1) 零拷贝 ✅
```

**内部实现**：
```cpp
class string_view {
    const char* data_;    // 指向已有字符串的指针
    size_t size_;         // 长度
    // 总共 2 个指针大小（16 字节在 64 位系统）
};
// 没有内存分配！没有拷贝！不拥有数据！
```

> ⚠️ **关键警示**：`string_view` 不拥有数据！它只是一个"视图"。如果它引用的底层 `std::string` 被销毁，`string_view` 就是悬空引用——未定义行为。

```cpp
std::string_view get_bad_view() {
    std::string s = "temporary";
    return std::string_view(s);  // ❌ s 返回后被销毁，view 悬空！
}

std::string_view get_good_view() {
    static const std::string s = "permanent";
    return std::string_view(s);  // ✅ 静态变量，生命周期有保证
}
```

### 1.4 结构化绑定（Structured Bindings）⭐

```cpp
// 之前
std::map<int, std::string>::iterator it = m.insert({1, "hello"});
bool inserted = it.second;
int key = it.first->first;

// C++17
auto [iter, inserted] = m.insert({1, "hello"});
// iter 是 std::map::iterator
// inserted 是 bool

// 更多用法
auto [x, y, z] = std::tuple{1, 2.0, "hello"};
auto& [key, value] = *map_iter;   // 引用的结构化绑定
// 修改 value 会直接修改 map 中的值！
```

**原理**：编译器把右边的表达式结果拆解为匿名变量，然后给每个结构化绑定名字创建别名。

### 1.5 if constexpr —— 编译期条件分支 ⭐

```cpp
// 传统的 SFINAE 方式（复杂）
template<typename T>
auto process(const T& val) ->
    std::enable_if_t<std::is_integral_v<T>, void> {
    // 整数分支
}

template<typename T>
auto process(const T& val) ->
    std::enable_if_t<!std::is_integral_v<T>, void> {
    // 非整数分支
}

// C++17 if constexpr（清晰明了）
template<typename T>
void process(const T& val) {
    if constexpr (std::is_integral_v<T>) {
        std::cout << "Integer: " << val;
    } else if constexpr (std::is_floating_point_v<T>) {
        std::cout << "Float: " << val;
    } else {
        std::cout << "Other type";
    }
}
// 条件在编译期求值，不满足条件的分支完全不参与编译
```

### 1.6 折叠表达式（Fold Expressions）

```cpp
// 之前用变参模板写 sum（麻烦）
template<typename T>
T sum(T v) { return v; }

template<typename T, typename... Args>
T sum(T first, Args... rest) {
    return first + sum(rest...);  // 递归展开
}

// C++17 折叠表达式 —— 一行搞定！
template<typename... Args>
auto sum(Args... args) {
    return (args + ...);  // 一元右折叠 → ((1 + 2) + 3) + 4
}
// 还有：一元左折叠 (... + args)
//      二元右折叠 (args + ... + init)
//      二元左折叠 (init + ... + args)
```

### 1.7 C++17 其他重要特性速览

| 特性 | 代码 | 解决了什么 |
|------|------|-----------|
| CTAD（类模板参数推导） | `std::pair p{1, 2.0};` 不需写 `std::pair<int, double>` | 减少冗余模板参数 |
| 内联变量 | `inline static int x = 42;` | header-only 库定义全局变量 |
| `std::filesystem` | `std::filesystem::path` | 跨平台文件系统操作 |
| 保证复制消除 | 纯右值返回时强制 RVO | 以前是可选优化，现在是强制 |
| `[[nodiscard]]` | 函数返回值不可丢弃 | 防止忘记使用返回值 |
| `[[maybe_unused]]` | 标记可能不用的变量 | 消除编译警告 |

---

## 第二部分：C++20 关键特性

### 2.1 Concepts —— 约束模板参数 ⭐⭐ 面试重点

**C++20 之前的问题**：

```cpp
template<typename T>
T add(T a, T b) { return a + b; }

add(1, 2);              // ✅ int 有 operator+
add(MyType{}, MyType{}); // ❌ 编译错误（如果 MyType 没有 operator+）
// 错误信息：几百行的模板实例化失败堆栈 🤯
```

**C++20 Concepts 解决**：

```cpp
// 定义一个 Concept：要求 T 是可加的
template<typename T>
concept Addable = requires(T a, T b) {
    { a + b } -> std::convertible_to<T>;  // a+b 合法且结果可转为 T
};

// 使用 Concept 约束模板
template<Addable T>
T add(T a, T b) { return a + b; }
// 现在错误信息清晰："T 不满足 Addable 约束"——一行就告诉你问题在哪！
```

**Concepts vs SFINAE vs enable_if**：

| 方案 | 示例 | 可读性 |
|------|------|--------|
| SFINAE | `auto f(T t) -> decltype(t.foo(), void())` | 🤯 |
| enable_if | `enable_if_t<is_integral_v<T>, void>` | 😕 |
| if constexpr | `if constexpr (is_integral_v<T>)` | 🙂 |
| Concepts | `requires Integral<T>` | 😊 |

### 2.2 Ranges —— 管道式集合操作 ⭐

```cpp
#include <ranges>

std::vector<int> v = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

// 传统的 STL 方式（不直观）
auto even_squares = std::vector<int>{};
std::copy_if(v.begin(), v.end(), std::back_inserter(even_squares),
             [](int i) { return i % 2 == 0; });
std::transform(even_squares.begin(), even_squares.end(),
               even_squares.begin(), [](int i) { return i * i; });

// C++20 Ranges 管道写法（从左到右自然阅读）
auto result = v
    | std::views::filter([](int i) { return i % 2 == 0; })
    | std::views::transform([](int i) { return i * i; })
    | std::ranges::to<std::vector>();  // C++23, C++20 需要手动 collect
```

**Ranges 的核心优势**：
1. **惰性求值**（Lazy Evaluation）：filter 和 transform 在迭代时才计算，不产生中间容器
2. **从左到右**的管道语法，比参数嵌套清晰
3. **编译期安全**：Concept 约束的接口

### 2.3 Coroutines —— 异步编程新范式

```cpp
// C++20 协程的核心价值：用同步写法写异步代码
#include <coroutine>

// 一个最简单的协程 return 类型
struct Task {
    struct promise_type {
        Task get_return_object() { return {}; }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() {}
    };
};

Task my_coroutine() {
    co_await std::suspend_always{};  // 挂起点
    co_return;                        // 返回
}
```

**核心关键词**：

| 关键词 | 含义 |
|--------|------|
| `co_await` | 挂起当前协程，等待某个操作完成 |
| `co_yield` | 挂起并返回一个值（用于生成器） |
| `co_return` | 返回最终结果，协程结束 |

**与回调/状态机的对比**：

```cpp
// 回调方式：回调地狱
async_read("file.txt", [](std::string data) {
    async_parse(data, [](Result r) {
        async_store(r, []() {
            // 三层嵌套就算了，10层呢？
        });
    });
});

// 协程方式：像同步代码一样写
std::string data = co_await async_read("file.txt");
Result r = co_await async_parse(data);
co_await async_store(r);  // 清晰明了
```

> **面试技巧**：虽然你不会从零实现协程基础设施，但面试官期待你理解协程的动机（解决回调地狱、简化异步状态机）和三个新关键词的含义。

### 2.4 constexpr 大解放 —— C++20 编译期计算飞跃

```cpp
// C++20 constexpr 可以做之前无法想象的事：

// 编译期 new/delete（配对使用）
constexpr auto create_array() {
    int* p = new int[100];
    for (int i = 0; i < 100; ++i) p[i] = i * i;
    // ...计算...
    delete[] p;  // 编译期 delete 必须配对的 new
    return result;
}

// 编译期 vector！
constexpr auto compile_time_vec() {
    std::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    return v;  // 编译期使用 vector！
}

// 编译期 string！
constexpr std::string greeting() {
    std::string s = "Hello, ";
    s += "World!";
    return s;
}
```

**consteval vs constexpr vs constinit**：

| | `constexpr` | `consteval` | `constinit` |
|------|------------|------------|------------|
| 含义 | **可能**在编译期执行 | **必须**在编译期执行 | **必须**在编译期初始化 |
| 运行时可用 | ✅ | ❌ | ✅ |
| 示例 | `constexpr int x = f();` | `consteval int f() {}` | `constinit static int x = 42;` |
| 解决什么问题 | 通用编译期计算 | 杜绝运行时调用 | 消除静态初始化顺序问题 |

### 2.5 Spaceship Operator（`<=>`）—— 三路比较

```cpp
// C++17 之前：写 6 个比较运算符（或者写 2 个 + 复用）
struct Point {
    int x, y;
    bool operator==(const Point& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Point& o) const { return !(*this == o); }
    bool operator<(const Point& o) const { return std::tie(x, y) < std::tie(o.x, o.y); }
    bool operator<=(const Point& o) const { return !(o < *this); }
    bool operator>(const Point& o) const { return o < *this; }
    bool operator>=(const Point& o) const { return !(*this < o); }
};

// C++20：= default 一行搞定！
struct Point {
    int x, y;
    auto operator<=>(const Point&) const = default;
    // 编译器自动生成全部 6 个比较运算符（还有 == 和 !=）
};

// 自定义实现
auto operator<=>(const Point& o) const {
    if (auto cmp = x <=> o.x; cmp != 0) return cmp;
    return y <=> o.y;
}
```

**返回类型**：

| `a <=> b` 的结果 | 含义 | 等价 |
|-----------------|------|------|
| `std::strong_ordering::less` | a < b | 严格全序 |
| `std::strong_ordering::equal` | a == b | 且可替换 |
| `std::strong_ordering::greater` | a > b | |
| `std::partial_ordering::unordered` | 不可比 | 如 NaN |

### 2.6 C++20 其他重要特性速览

| 特性 | 示例 | 解决了什么 |
|------|------|-----------|
| 模块（Modules） | `import std;` | 替代 `#include`，编译速度大幅提升 |
| `std::span` | `span<int> s(arr, 5);` | 零拷贝数组视图，替代指针+长度 |
| `std::format` | `std::format("{}-{}", x, y)` | 类型安全的字符串格式化 |
| `std::jthread` | `jthread t(func);` | 自动 join + 可中断的线程 |
| `std::source_location` | 替代 `__FILE__`/`__LINE__` 宏 | 标准化的源码位置信息 |
| Designated initializers | `Point{.x = 1, .y = 2}` | C 风格的指定初始化（受限） |
| `[[likely]]` / `[[unlikely]]` | 分支预测提示 | 指导编译器优化 |

---

## 检验问题

### 基础题

**Q1：`std::variant` 和 C++ 的 `union` 有什么本质区别？**

<details>
<summary>点击查看答案</summary>

| | `union` | `std::variant` |
|------|------|------|
| 类型安全 | ❌ 不知道当前活跃成员 | ✅ 记录了当前活跃成员索引 |
| 析构 | ❌ 不会调用成员的析构函数 | ✅ 自动调用活跃成员的析构 |
| 错误使用 | 读到错误类型 → 未定义行为 | `std::get<错误类型>` → 抛异常 |
| 成员类型 | 只允许平凡类型（C++11放宽但危险） | 任意类型 |

`variant` 本质上是将 union 的危险性封装在安全的 API 后面——类型安全的 tagged union。
</details>

**Q2：`std::string_view` 和 `const std::string&` 有什么区别？应该优先用哪个？**

<details>
<summary>点击查看答案</summary>

`string_view` 只需要指针+长度（16字节），不需要构造 `std::string`。对于字符串字面量或 `const char*`，传 `string_view` 完全不需要构造临时 `std::string`。

**优先用 `string_view`** 作为函数参数类型，除非函数内部最终需要一个 `std::string`（此时直接用 `std::string` 参数 + move）。

⚠️ 注意：`string_view` 不拥有数据，要确保引用的数据在 `string_view` 使用期间有效。
</details>

### 进阶题

**Q3：Concepts 和传统的 `std::enable_if` 相比，除了错误信息更好，还有什么优势？**

<details>
<summary>点击查看答案</summary>

1. **重载歧义消除**：Concepts 可以自然地对模板进行重载排序（更特化的 Concept 优先匹配），而 `enable_if` 需要复杂的否定模式
2. **语法简洁**：`template<Integral T>` vs `template<typename T, typename = enable_if_t<is_integral_v<T>>>`
3. **编译速度**：Concept 检查在模板实例化之前，失败时不会产生大量无效实例化尝试
4. **作为类型约束**：`std::integral auto x = f()` —— 变量声明也能用 Concepts
</details>

**Q4：`constexpr`、`consteval`、`constinit` 分别解决什么问题？给出使用场景。**

<details>
<summary>点击查看答案</summary>

- **`constexpr`**："这个函数/变量可以在编译期求值"——灵活性最高，是通用编译期计算的基础。使用场景：编译期计算常量表、constexpr 构造函数。
- **`consteval`**："这个函数必须在编译期求值"——杜绝运行时调用。适合编译期配置解析、必须编译期生成的代码。C++23 引入 `if consteval` 进一步精细化控制。
- **`constinit`**："这个变量必须在编译期初始化"——消除静态初始化顺序问题（Static Initialization Order Fiasco）。适合全局/静态变量，确保在 `main` 之前一定有确定的值。
</details>

### 综合题

**Q5：描述一个场景，你会同时用到 `std::variant`、`std::visit` 和 C++20 Concepts。**

<details>
<summary>点击查看答案</summary>

**场景**：一个表达式求值器，支持不同类型的表达式节点。

```cpp
using Expr = std::variant<int, double, std::string>;

template<typename T>
concept Numeric = std::is_arithmetic_v<T>;

auto eval = [](auto&& arg) -> double {
    using T = std::decay_t<decltype(arg)>;
    if constexpr (Numeric<T>) {
        return static_cast<double>(arg);
    } else {
        return 0.0; // 非数值处理
    }
};

Expr e = 42;
double result = std::visit(eval, e); // 42.0
```

这里 `variant` 统一存储不同类型的值，`visit` 通过 `if constexpr` + `Numeric` Concept 做编译期分发，保证每种类型都有正确且最优的处理路径。
</details>

---

**下一步**：[Day 5：模板元编程 + STL 源码剖析](day5-template-metaprogramming-stl.md)
