# Day 2：面向对象核心机制深入

> **学习目标**：透彻理解虚函数表的底层布局、多重继承与虚继承的内存模型、RAII 与 Rule of Five/Zero。
> **预计耗时**：3-4 小时

---

## 第一部分：虚函数表（vtable）原理——从入门到内存布局

### 1.1 没有虚函数时：静态绑定

```cpp
class Base {
public:
    void foo() { printf("Base::foo\n"); }
};

class Derived : public Base {
public:
    void foo() { printf("Derived::foo\n"); }  // 隐藏（hide），非覆盖（override）
};

Base* p = new Derived();
p->foo();  // 打印 "Base::foo" —— 静态绑定！根据指针类型决定
```

**关键区别**：
| 场景 | 调用机制 | 决定时机 |
|------|---------|----------|
| 非虚函数 | 静态绑定（根据指针/引用的**声明类型**） | 编译期 |
| 虚函数 | 动态绑定（根据指针/引用指向的**实际对象类型**） | 运行时 |

### 1.2 虚函数表（vtable）机制 ⭐ 核心原理

每当你写一个含有虚函数的类，编译器在幕后做了三件事：

```
1. 为这个类生成一张虚函数表（vtable）—— 函数指针数组
2. 在每个对象中插入一个隐藏指针 vptr（指向该类对应的 vtable）
3. 每次调用虚函数时，通过 vptr → vtable[index] 间接调用
```

```cpp
class Animal {
public:
    virtual void speak() { printf("...\n"); }
    virtual ~Animal() {}
    int m_age;
};

class Dog : public Animal {
public:
    void speak() override { printf("Woof!\n"); }
    int m_legs;
};

// 编译器生成的对象布局（简化示意）：
//
// Animal 对象:
// ┌──────────┐
// │  vptr    │ ──→ Animal vtable
// ├──────────┤    ┌──────────────────┐
// │  m_age   │    │ type_info*       │ ← RTTI 信息
// └──────────┘    │ ~Animal()        │
//                  │ Animal::speak()  │
//                  └──────────────────┘
//
// Dog 对象:
// ┌──────────┐
// │  vptr    │ ──→ Dog vtable
// ├──────────┤    ┌──────────────────┐
// │  m_age   │    │ type_info*       │ ← 指向 "class Dog"
// ├──────────┤    │ ~Dog()           │
// │  m_legs  │    │ Dog::speak()     │
// └──────────┘    └──────────────────┘
```

**虚函数调用的实际指令（x86-64 简化）**：
```asm
; p->speak();  其中 p 指向 Dog 对象

mov  rax, [p]           ; 取 vptr（对象第一项）
mov  rax, [rax]         ; 取 vtable 第一条（注意可能还有 RTTI 偏移）
call [rax + offset]     ; 通过函数指针间接调用
; 总共 3 次内存访问：取 vptr → 取函数指针 → 调用
```

> **性能开销**：虚函数调用比普通函数调用多 2-3 次内存解引用，且无法内联（除非编译器能做去虚拟化优化）。这就是为什么高频交易系统中应谨慎使用虚函数。

### 1.3 单继承下的 vtable 布局 ⭐ 面试必考

```cpp
class Base {
public:
    virtual void f() {}
    virtual void g() {}
    virtual void h() {}
};

class Derived : public Base {
public:
    void g() override {}      // 覆盖 Base::g
    virtual void i() {}       // 新增虚函数
};
```

**vtable 布局**：

```
Base vtable:              Derived vtable:
┌──────────────────┐     ┌──────────────────┐
│ type_info*       │     │ type_info*       │
├──────────────────┤     ├──────────────────┤
│ &Base::f()       │     │ &Base::f()       │ ← 未覆盖，直接复制
├──────────────────┤     ├──────────────────┤
│ &Base::g()       │     │ &Derived::g()    │ ← 覆盖，替换为新地址
├──────────────────┤     ├──────────────────┤
│ &Base::h()       │     │ &Base::h()       │ ← 未覆盖，直接复制
├──────────────────┤     ├──────────────────┤
│ (结束)           │     │ &Derived::i()    │ ← 新增虚函数，追加到末尾
└──────────────────┘     └──────────────────┘
```

**核心规律**：
1. 派生类的 vtable **拷贝父类 vtable 布局**，然后两个变化：
   - 被覆盖的虚函数：替换函数指针
   - 新增的虚函数：追加到 vtable 末尾
2. vtable 布局是**类型兼容性**的基础——`Base*` 和 `Derived*` 看到的前半段布局完全一致

### 1.4 多重继承下的 vtable 布局 ⭐ 面试难点

```cpp
class Base1 {
public:
    virtual void f1() {}
    int b1_data;
};

class Base2 {
public:
    virtual void f2() {}
    int b2_data;
};

class Derived : public Base1, public Base2 {
public:
    void f1() override {}       // 覆盖 Base1::f1
    void f2() override {}       // 覆盖 Base2::f2
    virtual void f3() {}        // 新增
    int d_data;
};
```

**多重继承下的对象布局**：

```
Derived 对象内存布局:
┌───────────────────┐  ← this 指针（作为 Base1* 时）
│ vptr_to_Base1     │ ──→ Derived-in-Base1 vtable
├───────────────────┤      ┌─────────────────────┐
│ b1_data           │      │ &Derived::f1()      │ ← 覆盖
├───────────────────┤  ← this + 16（作为 Base2* 时）  ├─────────────────────┤
│ vptr_to_Base2     │ ──→ Derived-in-Base2 vtable   │ &Derived::f3()      │ ← 新增
├───────────────────┤      └─────────────────────┘
│ b2_data           │
├───────────────────┤      Derived-in-Base2 vtable:
│ d_data            │      ┌─────────────────────┐
└───────────────────┘      │ &Derived::f2()      │ ← 覆盖
                           │ (top_offset: -16)   │ ← this 调整偏移量！
                           └─────────────────────┘
```

**关键点：this 指针调整（This Pointer Adjustment / Thunk）**

当你这样写代码时：
```cpp
Base2* p = new Derived();
p->f2();   // p 指向 Derived 对象内部的 Base2 子对象！
```

此时 `p` 实际上指向 `Derived` 对象**内部**偏移 16 字节（假设）的位置。调用 `f2()` 时，通过 **thunk** 自动调整 `this` 指针：

```asm
; thunk 的伪汇编（调整 this 指针）
sub  rdi, 16        ; 将 this 从 Base2* 调整为 Derived*
jmp  Derived::f2    ; 跳转到真实实现
```

### 1.5 虚继承（Virtual Inheritance）—— 菱形继承的解决方案 ⭐⭐ 进阶

```cpp
class GrandBase {
public:
    int data;
    virtual void f() {}
};

class Base1 : virtual public GrandBase { /* ... */ };
class Base2 : virtual public GrandBase { /* ... */ };

class Derived : public Base1, public Base2 {
    // GrandBase 只有一份拷贝！
};
```

**问题**：没有虚继承时，菱形继承会导致两个问题：

1. **数据有两份**——`Derived` 中通过 Base1 和 Base2 各继承了一份 `GrandBase`（含两份 `data`），访问 `d.data` 编译报错（ambiguous），必须写成 `d.Base1::data` 或 `d.Base2::data`。更致命的是这两份数据各改各的、互不同步。
2. **指针转换歧义**——`GrandBase* g = &d` 同样报错，必须 `static_cast<Base1*>(&d)` 指定走哪条路径。

虚继承将 `GrandBase` 提升为**共享基类**，Derived 中只保留一份，解决了上述问题。

**虚继承的对象布局**（以 g++ Itanium ABI 为例）：

```
Derived 对象布局:
┌─────────────────────────┐  ← Derived* / Base1*
│ vptr_to_Base1           │ ──→ 含 virtual base offset
├─────────────────────────┤
│ Base1 自己的成员         │
├─────────────────────────┤
│ vptr_to_Base2           │ ──→ 含 virtual base offset
├─────────────────────────┤
│ Base2 自己的成员         │
├─────────────────────────┤
│ Derived 自己的成员       │
├─────────────────────────┤
│ GrandBase (共享的)       │  ← 虚基类放在对象末尾
│   data (只有一份!)       │
│   GrandBase自己的vptr    │
└─────────────────────────┘
```

**关键设计**：
- 虚基类子对象**放在派生类对象的末尾**（而不是开头）
- 每个"虚拟继承"路径的 vtable 中记录到虚基类的偏移量（virtual base offset）
- 访问虚基类成员时需要**两次间接访问**：vptr → offset → GrandBase 成员

```cpp
Derived d;
GrandBase* g = &d;   // 编译器不知道 GrandBase 在 d 中的位置！
// 必须通过 vtable 中的 virtual base offset 来定位
```

---

## 第二部分：dynamic_cast 与 RTTI

### 2.1 dynamic_cast 的工作原理

`dynamic_cast` 是 C++ 中唯一**依赖 RTTI** 的转型操作符，用于安全的向下转型。

```cpp
Base* p = new Derived();
Derived* dp = dynamic_cast<Derived*>(p);   // 成功 → dp 非空
Derived* dp2 = dynamic_cast<Derived*>(p2); // 失败 → dp2 == nullptr

// 对引用失败时抛 std::bad_cast
Derived& dr = dynamic_cast<Derived&>(*p);  // 成功
Derived& dr2 = dynamic_cast<Derived&>(*p2); // 抛异常！
```

**实现原理**：通过 vtable 第一项的 `type_info*` 指针，进行运行时类型比较。

```
vtable 结构（简化）:
┌───────────────────┐
│ type_info*        │ ← RTTI 信息：指向类型的 std::type_info 对象
│ ----------------  │
│ &Base::f()        │
│ &Base::g()        │
│ ...               │
└───────────────────┘
```

**性能影响**：`dynamic_cast` 可能很慢——它需要遍历继承树进行字符串比较（比较 `type_info::name()`），复杂度 O(继承深度)。

**与非虚 dynamic_cast 的区别**：
- `static_cast`：编译期转换，不做运行时检查，**更快但更危险**
- `dynamic_cast`：运行时检查，**安全但有开销**

### 2.2 RTTI 与 typeid

```cpp
#include <typeinfo>

Base* p = new Derived();
std::cout << typeid(*p).name() << std::endl;  // 打印 "class Derived"（因为是多态类型）
std::cout << typeid(p).name() << std::endl;   // 打印 "class Base *"（指针本身）

// 类型比较
if (typeid(*p) == typeid(Derived)) { /* ... */ }
```

**关键点**：
- `typeid` 对于**多态类型**（有虚函数的类），通过 vptr 获取 `type_info`，得到实际类型
- 对于**非多态类型**，`typeid` 直接根据编译期类型返回结果
- 可以用 `-fno-rtti` 禁用 RTTI（减小二进制体积），但 `dynamic_cast` 和 `typeid` 无法正常工作

---

## 第三部分：构造与析构、RAII 与 Rule of Five/Zero

### 3.1 构造函数中调用虚函数 —— 经典陷阱

```cpp
class Base {
public:
    Base() { init(); }                   // 构造函数中调用虚函数！
    virtual void init() { printf("Base::init\n"); }
};

class Derived : public Base {
public:
    Derived() : Base() {}                // 先调用 Base()
    void init() override { printf("Derived::init\n"); }
};

Derived d;  // 输出：Base::init（不是 Derived::init！）
```

**原因**：构造顺序是**先基类后派生类**。在 `Base()` 执行时，`Derived` 部分还没构造完成，vptr 此时指向 `Base` 的 vtable。编译器故意这样做，防止在派生类未初始化时访问其成员。

### 3.2 虚析构函数 —— 为什么需要

```cpp
class Base {
public:
    ~Base() { printf("~Base\n"); }   // 非虚析构！
};

class Derived : public Base {
    int* data;
public:
    Derived() : data(new int[100]) {}
    ~Derived() { delete[] data; printf("~Derived\n"); }
};

Base* p = new Derived();
delete p;  // 输出只有 "~Base" —— Derived::~Derived 未被调用！内存泄漏！
```

**规则**：如果一个类有可能作为基类被多态地删除（即通过基类指针 `delete`），**析构函数必须是虚函数**。

```cpp
// 正确做法
class Base {
public:
    virtual ~Base() = default;  // 虚析构
};
```

### 3.3 RAII（Resource Acquisition Is Initialization）

RAII 是 C++ 最重要的资源管理惯用法，核心思想：

> **在构造函数中获取资源，在析构函数中释放资源。让资源的生命周期与对象的生命周期绑定。**

```cpp
// 不用 RAII（容易泄漏——早期 return、异常、忘记 delete...）
void bad_function() {
    FILE* f = fopen("data.txt", "r");
    char* buf = new char[1024];
    // ... 使用 f 和 buf ...
    if (some_error) return;     // 泄漏！
    fclose(f);
    delete[] buf;
}

// 使用 RAII（零泄漏保证）
void good_function() {
    std::ifstream f("data.txt");       // 构造时打开，析构时自动关闭
    std::vector<char> buf(1024);        // 构造时分配，析构时自动释放
    if (some_error) return;             // 安全！栈展开自动调用析构
}
```

**RAII 四大应用场景**：

| 场景 | RAII 包装 |
|------|----------|
| 动态内存 | `std::unique_ptr` / `std::shared_ptr` / `std::vector` |
| 文件 | `std::ifstream` / `std::ofstream` |
| 互斥锁 | `std::lock_guard` / `std::unique_lock` / `std::scoped_lock` |
| 数据库连接 / Socket | 自定义 RAII 类 |

### 3.4 Rule of Three / Five / Zero ⭐

#### Rule of Three（C++98/03）

如果一个类需要**自定义**析构函数、拷贝构造函数或拷贝赋值运算符中的任意一个，**几乎肯定需要自定义全部三个**。

```cpp
class Array {
    int* data;
    size_t size;
public:
    // 构造函数
    Array(size_t n) : data(new int[n]), size(n) {}

    // 析构函数：释放资源
    ~Array() { delete[] data; }

    // 拷贝构造：深拷贝
    Array(const Array& other) : data(new int[other.size]), size(other.size) {
        std::copy(other.data, other.data + size, data);
    }

    // 拷贝赋值：处理自赋值 + 深拷贝
    Array& operator=(const Array& other) {
        if (this != &other) {                // 防止自赋值
            int* tmp = new int[other.size];   // 先分配新内存
            std::copy(other.data, other.data + other.size, tmp);
            delete[] data;                    // 再释放旧内存
            data = tmp;
            size = other.size;
        }
        return *this;
    }
};
```

#### Rule of Five（C++11）

C++11 增加了移动语义，在 Rule of Three 基础上增加**移动构造函数**和**移动赋值运算符**。

```cpp
class Array {
    int* data;
    size_t size;
public:
    // ... 拷贝构造 & 拷贝赋值 & 析构 ...

    // 移动构造：窃取资源
    Array(Array&& other) noexcept
        : data(std::exchange(other.data, nullptr))
        , size(std::exchange(other.size, 0)) {}

    // 移动赋值：
    Array& operator=(Array&& other) noexcept {
        if (this != &other) {
            delete[] data;
            data = std::exchange(other.data, nullptr);
            size = std::exchange(other.size, 0);
        }
        return *this;
    }
};
```

> **`std::exchange(obj, val)`**：将 `obj` 的值替换为 `val`，返回 `obj` 的旧值。移动语义中极其有用。

#### Rule of Zero（现代 C++ 推荐）

**如果你的类不需要直接管理资源（所有成员都是 RAII 类型），那么不要自定义任何析构/拷贝/移动函数。**

```cpp
// Rule of Zero —— 编译器自动生成的都是最优的
class Widget {
    std::vector<int> data;      // 自动管理内存
    std::string name;           // 自动管理内存
    std::unique_ptr<Impl> pImpl; // 自动管理资源
    // 不需要写析构、拷贝、移动 —— 编译器自动生成的都是正确的
};
```

---

## 检验问题

### 基础题

**Q1：虚函数调用和普通函数调用的性能差异有多大？为什么会差这么多？**

<details>
<summary>点击查看答案</summary>

虚函数调用比普通函数调用多 2-3 次内存访问（取 vptr → 取 vtable 中的函数指针 → 间接调用），且**无法内联**（编译器不确定具体调用哪个函数）。普通函数调用在现代 CPU 上可能 1-2 个周期，虚函数调用可能 5-10 个周期甚至更多（取决于 cache 命中情况）。这就是为什么高频交易系统的热路径要避免虚函数——纳秒级延迟不允许这种开销。
</details>

**Q2：为什么在构造函数中调用虚函数不会发生多态？**

<details>
<summary>点击查看答案</summary>

因为构造顺序：先构造基类，再构造派生类。在基类构造函数执行期间，派生类部分**尚未初始化**，vptr 此时指向当前正在构造的类的 vtable（即基类的 vtable）。编译器这样做是**故意的**，防止在派生类成员还未初始化时访问它们。析构函数同理——先析构派生类，此时基类析构函数中 vptr 已回退指向基类 vtable。
</details>

### 进阶题

**Q3：下面的对象布局是怎样的？sizeof 是多少（64位系统）？**

```cpp
class A {
public:
    virtual void f() {}
    int x;
};

class B : public A {
public:
    virtual void g() {}
    int y;
};
```

<details>
<summary>点击查看答案</summary>

**A 对象**（64位）：vptr(8) + int x(4) + padding(4) = **16 bytes**
**B 对象**（64位）：vptr(8) + A::x(4) + padding(4) + B::y(4) + padding(4) = **24 bytes**（注意尾部 padding 是为了数组对齐）

B 的 vtable：`[type_info, &A::f(), &B::g()]`
（A::f 未被覆盖，g 是 B 新增的追加到末尾）
</details>

**Q4：多重继承中进行 `dynamic_cast` 时，`this` 指针需要调整吗？举个例子说明。**

<details>
<summary>点击查看答案</summary>

**需要调整**。

```cpp
class Base1 { virtual void f() {} int a; };
class Base2 { virtual void g() {} int b; };
class Derived : public Base1, public Base2 {};

Derived d;
Base2* b2 = &d;           // b2 指向 d 内部偏移 sizeof(Base1) 的位置
Derived* dp = dynamic_cast<Derived*>(b2);
// 编译器生成的代码自动调整 b2 指针回退到 d 的起始地址
```

编译器通过 vtable 中存的 **top_offset** 字段知道需要回退多少字节。这个 thunk 是编译器自动生成的。
</details>

### 综合题

**Q5：下面这个类有问题吗？如果有，如何修复？**

```cpp
class FileWrapper {
    FILE* f;
public:
    FileWrapper(const char* path) { f = fopen(path, "r"); }
    ~FileWrapper() { if (f) fclose(f); }
    // ... 其他方法
};
```

<details>
<summary>点击查看答案</summary>

**有问题**：违反了 Rule of Three。编译器自动生成的拷贝构造函数和拷贝赋值运算符是**浅拷贝**（只拷贝 `FILE*` 指针），导致：

```cpp
FileWrapper f1("a.txt");
FileWrapper f2 = f1;  // 浅拷贝！f1.f 和 f2.f 指向同一个 FILE
// f2 析构 → fclose(f) → f1.f 变成悬空指针
// f1 析构 → fclose(悬空指针) → 未定义行为！
```

**修复方法**：
1. 使用 `std::unique_ptr<FILE, decltype(&fclose)>`（Rule of Zero）
2. 或者显式禁止拷贝：`FileWrapper(const FileWrapper&) = delete;`
3. 或者实现深拷贝（但 FILE* 不太适合深拷贝）
</details>

---

**下一步**：[Day 3：移动语义、智能指针、Lambda](day3-cpp11-14-17-move-smartptr-lambda.md)
