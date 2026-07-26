# 第 1 周：C++ 深度系统化 — 学习目录

> **对应主计划**：[Interview_Prep_Plan_Senior_C++_Engineer.md](../../Interview_Prep_Plan_Senior_C++_Engineer.md) 第 1 周
> **学习目标**：建立 C++ 面试知识体系框架，从编译原理到现代特性全覆盖，融会贯通而非死记硬背。

---

## 学习路线图

```
Day 1 ──→ Day 2 ──→ Day 3 ──→ Day 4 ──→ Day 5 ──→ Day 6
编译链接    面向对象    C++11      C++17/20   模板+STL   综合复习
内存布局    vtable      14/17      现代特性    源码剖析   模拟面试
│          │          │          │          │          │
└─ 基础 ──→┴─ 核心 ──→┴─ 现代 ──→┴─ 前沿 ──→┴─ 深入 ──→┴─ 检验
```

## 文件导航

| Day | 文件 | 主题 | 预计耗时 |
|-----|------|------|---------|
| 1 | [day1-compilation-memory-layout.md](day1-compilation-memory-layout.md) | 编译链接全流程与内存布局 | 3-4h |
| 2 | [day2-oop-vtable-inheritance.md](day2-oop-vtable-inheritance.md) | 面向对象核心机制深入 | 3-4h |
| 3 | [day3-cpp11-14-17-move-smartptr-lambda.md](day3-cpp11-14-17-move-smartptr-lambda.md) | 移动语义、智能指针、Lambda | 3-4h |
| 4 | [day4-cpp17-20-modern-features.md](day4-cpp17-20-modern-features.md) | C++17/20 现代特性 | 3-4h |
| 5 | [day5-template-metaprogramming-stl.md](day5-template-metaprogramming-stl.md) | 模板元编程 + STL 源码剖析 | 3-4h |
| 6 | [day6-weekly-review-qa.md](day6-weekly-review-qa.md) | 周综合复习与模拟面试 | 3-4h |

## 学习建议

1. **顺序阅读，不要跳**：每天的内容有前置依赖，后面的章节会引用前面的知识点
2. **动手验证**：每个代码段都建议在 [Compiler Explorer (godbolt.org)](https://godbolt.org) 上实际编译运行
3. **先思考再对答案**：每个检验问题先自己组织语言回答，再对照参考答案
4. **做笔记**：准备一个"面试笔记本"，整理每周的核心知识点（1-2 页），面试前快速翻阅

---

## 本周末检验目标

完成 6 天学习后，你应该能自信地回答：

1. 虚函数表在多继承/虚继承下的内存布局是怎样的？
2. `std::shared_ptr` 的控制块结构以及线程安全性如何？
3. C++20 Concepts 相比 SFINAE 有什么优势？
4. `std::vector` 扩容为什么通常用 2 倍或 1.5 倍，而不是 10 倍？
5. 移动语义如何实现的？什么时候该用 `std::forward` 而不是 `std::move`？
