# Day 3：移动语义、智能指针、Lambda

> **学习目标**：深入掌握右值引用与移动语义的底层原理，理解三种智能指针的内部实现，精通 Lambda 表达式。
> **预计耗时**：3-4 小时

---

## 第一部分：移动语义——C++11 最重要的变革

### 1.1 为什么需要移动语义——从拷贝的痛点说起

```cpp
std::vector<int> create_large_vector() {
    std::vector<int> v(1000000);
    // ... 填充数据 ...
    return v;
}

// C++98：惨烈的三次拷贝
std::vector<int> result = create_large_vector();
// 1. 函数返回时拷贝到临时对象（100万元素！）
// 2. 临时对象拷贝到 result（又100万元素！）
// 3. 析构临时对象
// 总计：200万次元素拷贝，100万次元素析构 🤯
```

**C++11 移动语义**：不做拷贝，直接"窃取"临时对象的内部指针——O(1) 而非 O(N)。

### 1.2 左值 vs 右值——移动语义的基石

```cpp
int a = 42;           // a 是左值（有名字，可取地址）
int& ref = a;         // 左值引用绑定到左值

int&& rref = 42;      // 右值引用绑定到右值（临时值）
int&& rref2 = a + 1;  // a+1 是右值（表达式产生的临时值）

// 判断法则：
// 左值：有名字、可取地址、生命周期持续
// 右值：没有名字、不可取地址、通常是临时的
```

**关键记忆法**：
```
             ┌── 有名字，可取地址 ──→ 左值（lvalue）
表达式 ──→   │
             └── 没有名字，临时的 ──→ 右值（rvalue）
                                        │
                                    ┌── 纯右值（prvalue）：字面量、表达式结果
                                    └── 将亡值（xvalue）：std::move() 的结果
```

### 1.3 std::move 和 std::forward —— 不要被名字迷惑

#### std::move：无条件转为右值

```cpp
// std::move 的本质实现（简化）
template<typename T>
typename std::remove_reference<T>::type&& move(T&& t) noexcept {
    return static_cast<typename std::remove_reference<T>::type&&>(t);
}
// 就是一个强制类型转换！把任何类型变成右值引用。
// std::move 不"移动"任何东西，它只是"标记"。
```

```cpp
std::string s1 = "hello";
std::string s2 = std::move(s1);  // s1 的内容被"窃取"到 s2
// 之后 s1 处于"有效但未指定"的状态（通常是空字符串）
// 不应该再使用 s1（除了赋值或析构）
```

**常见误区**：
```cpp
// ❌ 错误：对 const 对象 move 没用
const std::string s = "hello";
std::string s2 = std::move(s);  // s 是 const，移动构造不可用，回退到拷贝构造！

// ❌ 错误：return 时不加 std::move（阻止 RVO）
std::string create() {
    std::string s = "hello";
    return std::move(s);  // 反而阻止了 RVO/NRVO 优化！
}
// ✅ 正确：直接 return 局部变量，编译器自动优化
std::string create() {
    std::string s = "hello";
    return s;  // NRVO 自动生效
}
```

**RVO/NRVO 详解**：Return Value Optimization —— 编译器直接在调用者的栈上构造返回值，完全消除拷贝/移动。C++17 起对纯右值强制保证。

#### std::forward：有条件地转发 ⭐

```cpp
// 完美转发（Perfect Forwarding）的核心
template<typename T>
void wrapper(T&& arg) {        // T&& 是万能引用（Universal Reference）
    target(std::forward<T>(arg));  // 保持 arg 的原始值类别
}

// 具体行为：
// wrapper(42);    → T=int,  forward→右值 (int&&)
// wrapper(x);     → T=int&, forward→左值 (int&)
```

**什么时候用 `move`，什么时候用 `forward`？**

| 操作 | 使用 | 原因 |
|------|------|------|
| `T&&` 参数转发 | `std::forward<T>(arg)` | 不知道 arg 是左值还是右值，需要条件转发 |
| 明确要窃取所有权 | `std::move(obj)` | 确定 obj 不再需要，无条件转移 |
| 返回局部变量 | 什么都不加 | 依赖 RVO/NRVO |

### 1.4 引用折叠（Reference Collapsing）

**万能引用**的本质依赖引用折叠规则：

```cpp
template<typename T>
void f(T&& arg) { ... }   // T&& 是万能引用

f(42);   // T 推导为 int，    参数类型 int&&
f(x);    // T 推导为 int&，   参数类型 int& (引用折叠)
```

**引用折叠规则**（只有一条规则）：

| 组合 | 结果 |
|------|------|
| `T& &` | `T&` |
| `T& &&` | `T&` |
| `T&& &` | `T&` |
| `T&& &&` | `T&&` |

> **记忆**：只要有一个是左值引用，结果就是左值引用。全右值才得右值。

### 1.5 移动操作应该标记 noexcept ⭐

```cpp
class MyVector {
public:
    MyVector(MyVector&& other) noexcept   // 必须标记 noexcept！
        : data_(std::exchange(other.data_, nullptr))
        , size_(std::exchange(other.size_, 0)) {}

    MyVector& operator=(MyVector&& other) noexcept { /* ... */ }
};
```

**为什么移动构造需要 `noexcept`？** `std::vector` 扩容时，如果可以安全移动（移动构造是 noexcept 的），就用移动；否则回退到拷贝。这意味着如果你的移动构造没标记 `noexcept`，`vector<YourType>` 扩容时永远做拷贝而非移动——性能灾难！

---

## 第二部分：智能指针——告别手动 delete

### 2.1 unique_ptr —— 独占所有权 ⭐

```cpp
// 基础用法
std::unique_ptr<Widget> p = std::make_unique<Widget>();  // C++14
auto p2 = std::make_unique<Widget>(arg1, arg2);          // 推荐

// 不可拷贝，只能移动
auto p3 = p;              // ❌ 编译错误！
auto p3 = std::move(p);   // ✅ 转移所有权，p 变为 nullptr

// 自定义删除器
auto deleter = [](FILE* f) { if (f) fclose(f); };
std::unique_ptr<FILE, decltype(deleter)> file(fopen("a.txt", "r"), deleter);
```

**内部实现要点**：

```cpp
// unique_ptr 的核心实现（简化版）
template<typename T, typename Deleter = std::default_delete<T>>
class unique_ptr {
    T* ptr_;           // 裸指针
    Deleter deleter_;  // 删除器
public:
    unique_ptr(const unique_ptr&) = delete;               // 禁止拷贝
    unique_ptr& operator=(const unique_ptr&) = delete;

    unique_ptr(unique_ptr&& other) noexcept               // 移动构造
        : ptr_(std::exchange(other.ptr_, nullptr))
        , deleter_(std::move(other.deleter_)) {}

    ~unique_ptr() { if (ptr_) deleter_(ptr_); }

    T* get() const noexcept { return ptr_; }
    T* release() noexcept { return std::exchange(ptr_, nullptr); }
    void reset(T* p = nullptr) { /* 释放旧指针，接管新指针 */ }
};
```

**编译期零开销**：`unique_ptr`（使用默认删除器时）的大小等于一个裸指针。删除器通过 **EBO（Empty Base Optimization）** 优化掉。

### 2.2 shared_ptr —— 共享所有权 ⭐

```cpp
auto p1 = std::make_shared<Widget>();  // 推荐（一次分配，性能更好）
auto p2 = p1;            // 引用计数 +1
p2.reset();              // 引用计数 -1
// 当最后一个 shared_ptr 销毁时，自动 delete Widget
```

**内部实现：控制块（Control Block）** ⭐⭐ 面试必考

```
shared_ptr<Widget> p1 ──→ ┌──────────────────┐
                           │  Widget 对象      │
shared_ptr<Widget> p2 ──→ │                  │
                           └──────────────────┘
                           ┌──────────────────┐
                    ┌────→ │  控制块          │
                    │      │  ├─ use_count: 2 │  ← 强引用计数
                    │      │  ├─ weak_count: 0│  ← 弱引用计数
                    │      │  └─ deleter      │
                    │      └──────────────────┘
shared_ptr<Widget> p3 ─┘
```

**控制块什么时候创建？**

| 创建方式 | 控制块 |
|---------|--------|
| `std::make_shared<T>()` | 与对象一起分配（一次 new） |
| `std::shared_ptr<T>(new T)` | 单独分配（两次 new） |
| `std::shared_ptr<T>(unique_ptr<T>)` | 转移时创建 |

> **最佳实践**：永远用 `std::make_shared`（除非需要自定义删除器）。一次内存分配 vs 两次，性能更好且缓存友好。

**shared_ptr 的线程安全性** ⭐ 常见误区：

```cpp
// ✅ 控制块（引用计数）的操作是线程安全的（原子操作）
auto p1 = std::make_shared<int>(42);
auto p2 = p1;          // use_count 原子递增，线程安全

// ❌ 共享对象本身的修改不是线程安全的！
*p1 = 100;             // 需要自己加锁！

// ❌ 对同一个 shared_ptr 变量的并发修改不是线程安全的！
std::shared_ptr<int> global_p;
// 线程1: global_p = p1;     ← 同时读写 global_p，数据竞争！
// 线程2: global_p = p2;
```

### 2.3 weak_ptr —— 打破循环引用

```cpp
class Node {
public:
    std::vector<std::shared_ptr<Node>> children;  // 拥有子节点
    std::weak_ptr<Node> parent;                   // 不拥有父节点！← 关键
};

auto root = std::make_shared<Node>();
auto child = std::make_shared<Node>();
child->parent = root;      // weak_ptr 不增加 use_count
root->children.push_back(child);

// 当 root 和 child 都离开作用域时，两者都能正确析构
// 如果 parent 用 shared_ptr，use_count 永远不会归零 → 内存泄漏
```

**weak_ptr 使用模式**：

```cpp
std::weak_ptr<Widget> wp = sp;

// 1. 检查是否过期
if (auto sp2 = wp.lock()) {  // lock 返回 shared_ptr
    sp2->doSomething();       // 安全使用——sp2 持有引用，对象不会被销毁
}

// 2. 检查是否过期但不获取所有权
if (!wp.expired()) { /* ... */ }  // 存在 TOCTOU 竞态，不推荐

// 3. 异常版
try {
    std::shared_ptr<Widget> sp2(wp);  // 已过期则抛 std::bad_weak_ptr
} catch (const std::bad_weak_ptr&) { }
```

### 2.4 三种智能指针对比

| | unique_ptr | shared_ptr | weak_ptr |
|------|-----------|------------|----------|
| 所有权 | 独占 | 共享 | 不拥有（观察者） |
| 拷贝 | 禁止 | 引用计数+1 | 允许（不影响计数） |
| 大小 | = 1 个指针（默认删除器） | = 2 个指针 | = 2 个指针 |
| 开销 | 零运行时开销 | 原子引用计数 | 零运行时开销 |
| 使用场景 | 明确唯一所有权 | 共享所有权/运行时决定生命周期 | 打破循环引用/缓存观察 |

---

## 第三部分：Lambda 表达式与 std::function

### 3.1 Lambda 的完整语法

```cpp
// 完整形式：
[capture](params) mutable noexcept -> ret { body }

// 常见形式：
auto f = [](int x) { return x * 2; };               // 最简单
auto g = [&](int x) -> int { return x + offset; };   // 带返回类型
auto h = [=, &counter]() mutable { ++counter; };      // mutable + 混合捕获
```

**捕获方式详解**：

| 捕获 | 含义 | 捕获时机 |
|------|------|---------|
| `[=]` | 按值捕获所有外部变量 | Lambda 创建时（非调用时！） |
| `[&]` | 按引用捕获所有外部变量 | Lambda 创建时 |
| `[x]` | 按值捕获 x | Lambda 创建时 |
| `[&x]` | 按引用捕获 x | Lambda 创建时 |
| `[this]` | 捕获 this 指针 | C++17 前（C++20 推荐 `[=, this]`） |
| `[*this]` | 按值捕获 `*this` | C++17+ |
| `[x = std::move(x)]` | 初始化捕获 | C++14+ |

### 3.2 Lambda 的底层实现 —— 函数对象（Functor）

```cpp
// 这个 Lambda：
auto lambda = [x = 42](int y) { return x + y; };

// 编译器展开成类似这样的类：
class __lambda_1 {
    int x;  // 捕获的变量成为成员
public:
    explicit __lambda_1(int _x) : x(_x) {}
    auto operator()(int y) const { return x + y; }  // operator()
};
__lambda_1 lambda(42);
```

**重要含义**：
- **Lambda 一定有大小**！无论多小的 Lambda，都至少占 1 字节（C++ 要求每个对象有唯一地址）
- 按值捕获的变量默认是 `const`——需要 `mutable` 才能修改
- 无捕获的 Lambda 可以转换为函数指针

### 3.3 std::function —— 类型擦除的魔法 ⭐

```cpp
std::function<int(int, int)> func;
func = [](int a, int b) { return a + b; };         // Lambda
func = std::multiplies<int>();                       // 函数对象
func = [](int a, int b) -> int { return a - b; };  // 另一个 Lambda

// 为什么 std::function 能装下任意可调用对象？——类型擦除
```

**std::function 的实现原理（简化）**：

```cpp
// std::function 的核心是"类型擦除"—— 通过虚函数/函数指针桥接
template<typename Signature>
class function;

template<typename R, typename... Args>
class function<R(Args...)> {
    // 内部基类：提供统一接口
    struct callable_base {
        virtual R call(Args...) = 0;
        virtual ~callable_base() = default;
        virtual callable_base* clone() = 0;  // 用于拷贝
    };

    // 模板派生类：存储具体的可调用对象
    template<typename F>
    struct callable_impl : callable_base {
        F func;
        callable_impl(F f) : func(std::move(f)) {}
        R call(Args... args) override { return func(args...); }
        callable_base* clone() override { return new callable_impl(func); }
    };

    callable_base* impl_;  // 通过指针实现多态

public:
    template<typename F>
    function(F f) : impl_(new callable_impl<F>(std::move(f))) {}

    R operator()(Args... args) {
        return impl_->call(std::forward<Args>(args)...);
    }
};
```

**std::function 的性能注意事项**：
- 有**虚函数调用开销**和**可能的堆分配**（小对象优化 SBO 除外）
- 在性能关键路径上，优先用模板而非 `std::function`
- `std::function` 的大小通常为 32 字节（64位系统），用于 SBO

---

## 检验问题

### 基础题

**Q1：`std::move` 做了什么？`std::forward` 又做了什么？有什么区别？**

<details>
<summary>点击查看答案</summary>

- `std::move`：无条件把参数强制转换为右值引用。本质是 `static_cast<T&&>`，它不"移动"任何东西——只是赋予参数可被移动的"资格"。
- `std::forward`：有条件地转换——保持原始参数的左值/右值属性。用于万能引用参数的转发。

**区别**：`move` 是无条件的，`forward` 是有条件的。`move` 用于你确定不再需要该对象时；`forward` 用于你不知道参数是左值还是右值（模板中的万能引用）时。永远不要对 const 对象用 move（移动构造不可用，会回退到拷贝）。
</details>

**Q2：什么时候用 `shared_ptr` 而不是 `unique_ptr`？shared_ptr 的控制块里有什么？**

<details>
<summary>点击查看答案</summary>

**使用 shared_ptr**：多个对象共享所有权、观察者模式、无法在编译期确定最后一个使用者。

**控制块内容**：
1. 强引用计数（use_count）—— 决定何时 delete 对象
2. 弱引用计数（weak_count）—— 决定何时 delete 控制块
3. 删除器（Deleter）
4. 分配器（Allocator）
5. 指向被管理对象的指针（注意：可能和 shared_ptr 持有的指针不同！）
</details>

**Q3：Lambda 表达式底层是如何实现的？为什么它的体积不等于零？**

<details>
<summary>点击查看答案</summary>

Lambda 编译后生成一个**匿名的函数对象类**，捕获的变量成为该类的成员，Lambda 的函数体成为 `operator()` 的实现。因为 C++ 要求每个完整对象有唯一地址，即使没有捕获任何变量的 Lambda（无状态 Lambda），也必须至少占 1 字节。C++20 引入了无状态 Lambda 的默认构造和赋值支持，但仍然至少占 1 字节。
</details>

### 进阶题

**Q4：下面的代码有 bug 吗？**

```cpp
std::vector<std::string> v;
v.push_back("hello world");

std::thread t1([&v] { v.push_back("from t1"); });
std::thread t2([&v] { v.push_back("from t2"); });
t1.join();
t2.join();
```

<details>
<summary>点击查看答案</summary>

**有数据竞争bug**。`v` 被两个线程共享且都写入了 `push_back`，但没有同步机制。`std::vector` 的线程安全保证是"多读 or 单写"，不能多线程同时写。

修复：用互斥锁保护，或将 Lambda 改为 `[&v, &mtx]` 并在内部加锁。
</details>

**Q5：移动构造函数为什么要标记 `noexcept`？不标记会怎样？**

<details>
<summary>点击查看答案</summary>

`std::vector` 扩容时需要移动已有元素到新内存。如果移动构造是 `noexcept` 的，vector 就用移动（安全）；如果不是，就用拷贝（安全但慢）。这意味着如果你的类移动构造没标 `noexcept`，放入 `std::vector` 后就**永远不会被移动**——全是拷贝，性能大打折扣。这是 C++ 中"强异常保证"（strong exception guarantee）的体现——vector 扩容失败时能回滚到原始状态。
</details>

### 综合题

**Q6：为什么 `std::make_shared` 比 `std::shared_ptr<T>(new T)` 更好？**

<details>
<summary>点击查看答案</summary>

**两个原因**：

1. **内存分配效率**：`make_shared` 一次分配对象+控制块（连续内存，缓存友好），`new T + shared_ptr` 两次分配（对象和控制块分离，可能在不同的 cache line）。

2. **异常安全**：
```cpp
// 潜在问题：new T 成功了但 shared_ptr 构造失败 → 内存泄漏
process(std::shared_ptr<T>(new T), may_throw());

// make_shared 是单个函数调用，不存在这种竞态
process(std::make_shared<T>(), may_throw());
```

**唯一的不足**：`make_shared` 不支持自定义删除器。另外，如果使用 `make_shared` 且 weak_ptr 还活着，被管理对象的內存不会释放（因为控制块和对象在同一块内存中），直到所有 weak_ptr 也过期——如果对象非常大且 weak_ptr 长期存在，这是一个隐蔽的内存占用问题。
</details>

---

**下一步**：[Day 4：C++17/20 现代特性](day4-cpp17-20-modern-features.md)
