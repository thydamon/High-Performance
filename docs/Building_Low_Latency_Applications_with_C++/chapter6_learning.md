# 第六章：撮合引擎实现

> 教材：《Building Low Latency Applications with C++》  
> 阶段：第三阶段（第 5–7 章）  
> 目标：基于第五章的架构设计，从零实现交易所核心——撮合引擎（Matching Engine），包括限价订单簿、订单处理、撮合逻辑以及与网关/行情队列的交互。

---

## 一、本章学习目标

学完本章后，你应该能够回答：

1. 限价订单簿（Limit Order Book）的核心数据结构是什么？如何实现高效的 add/cancel/match？
2. 市价单（Market Order）和限价单（Limit Order）的处理流程有什么区别？
3. 撮合逻辑的核心规则是什么？FIFO（先到先得）如何在代码中体现？
4. 撤单（Cancel）和改单（Modify）需要处理哪些边界情况？
5. 撮合引擎如何通过无锁队列与订单网关和行情发布器交互？
6. 如何设计增量行情更新（Incremental Update）和快照行情（Snapshot）的生成逻辑？
7. 什么是价格-时间优先级？如何在订单簿中维护？

---

## 二、为什么要学这一章？

第五章从架构层面描述了交易所各组件如何配合。  
第六章进入**核心实现**——撮合引擎是整个交易系统的**心脏**：

- 撮合引擎的延迟直接决定系统性能上限，每一笔订单都在这里处理；
- 订单簿数据结构的设计影响插入、删除、匹配的效率；
- 撮合逻辑的正确性决定系统能否真实运行——一个小 bug 会导致"虚假成交"；
- 与网关和行情发布器的无锁队列交互是实现端到端低延迟的关键。

> **核心思想**：撮合引擎不是"处理订单的算法"，而是一个在极限延迟约束下管理复杂状态机的系统。每一行代码都要考虑：这个操作会不会引入锁？会不会触发动态内存分配？会不会产生不可预测的延迟抖动？

---

## 三、学习路线（分 5 个小节）

### 第 1 小节：订单簿数据结构

#### 6.1.1 订单簿基础概念

限价订单簿（Limit Order Book, LOB）是撮合引擎的核心数据结构，它维护**买单（Bids）和卖单（Asks）**的集合。

**基本术语**

| 术语 | 含义 |
|---|---|
| **Bid** | 买单（Buy Order），表示买方愿意以某价格买入 |
| **Ask/Offer** | 卖单（Sell Order），表示卖方愿意以某价格卖出 |
| **Buy Side / Sell Side** | 买单列表和卖单列表 |
| **Price Level** | 同价格的所有订单聚合成一个价格档位 |
| **Order** | 单个订单：价格、数量、时间戳、订单 ID、方向 |
| **Top of Book** | 最高买价（Best Bid）和最低卖价（Best Ask），两者之差为价差（Spread） |
| **Order Book Depth** | 各价格档位的挂单总量，反映市场深度 |

**价格-时间优先级（Price-Time Priority）**

撮合引擎最常用的优先级规则：

1. **价格优先**：买单价格高的优先，卖单价格低的优先；
2. **时间优先**：同一价格下，先到达的订单优先成交。

```
买单（Bids）             卖单（Asks）
价格高 → 优先成交        价格低 → 优先成交
时间早 → 优先成交        时间早 → 优先成交

Bid Level:               Ask Level:
  101.00  500 lot         101.50  200 lot
  100.50  300 lot         102.00  400 lot
  100.00  1000 lot        102.50  300 lot
  ───────                 ───────
  Best Bid = 101.00       Best Ask = 101.50  → 价差 = 0.50
```

#### 6.1.2 订单簿数据结构设计

订单簿的核心需求：

| 操作 | 频率 | 延迟要求 |
|---|---|---|
| 插入新订单 | 极高 | O(log n) 或更好 |
| 删除/撤单 | 高 | O(1) 或 O(log n) |
| 查询 best bid/ask（Top of Book） | 极高 | O(1) |
| 查询某一价格档位的总量 | 高 | O(1) |
| 遍历价格档位（按价格顺序） | 中（快照/检查） | 高效遍历 |

**经典设计：价格链表 + 订单哈希表**

书中采用**分层设计**：

```
OrderBook
├── BuySide (价格降序排列)        ← Best Bid 在第一个元素
│   ├── PriceLevel(101.00)       ← 同一价格的所有订单
│   │   ├── Order(id=1, qty=200, time=t1)
│   │   └── Order(id=5, qty=300, time=t3)
│   └── PriceLevel(100.50)
│       └── Order(id=2, qty=500, time=t2)
├── SellSide (价格升序排列)       ← Best Ask 在第一个元素
│   ├── PriceLevel(101.50)
│   │   └── Order(id=3, qty=200, time=t4)
│   └── PriceLevel(102.00)
│       ├── Order(id=4, qty=300, time=t5)
│       └── Order(id=6, qty=100, time=t6)
└── OrderMap (哈希表，order_id → Order*)
    ├── 1 → &Order(1)
    ├── 2 → &Order(2)
    └── ...
```

**为什么用这种设计？**

| 组件 | 选择原因 |
|---|---|
| **价格链表（std::map 或自定义 skiplist）** | 需要按价格排序遍历；买单降序、卖单升序 |
| **PriceLevel 内用 FIFO 队列** | 同一价格按到达时间优先（时间优先） |
| **OrderMap（哈希表）** | 撤单时需要按 ID 快速找到订单；O(1) 查找 |
| **价格档位聚合数量** | Top of Book 查询时快速知道各档位总量 |

**为什么不直接用 `std::map`？**

虽然 `std::map` 是有序容器，但低延迟场景下：

| 问题 | 说明 |
|---|---|
| 动态内存分配 | `std::map` 的节点插入会触发堆分配 |
| 内存布局散乱 | 红黑树节点分散，缓存命中率低 |
| 迭代器稳定性 | 增删节点后迭代器行为复杂 |

书中倾向**自定义价格链表**（基于数组或预分配节点池），但教学上从 `std::map` 开始理解，再逐步优化。

#### 6.1.3 Order 和 PriceLevel 数据结构

```cpp
#include <cstdint>
#include <functional>

enum class OrderSide : uint8_t { BUY = 0, SELL = 1 };
enum class OrderType : uint8_t { LIMIT = 0, MARKET = 1 };
enum class OrderStatus : uint8_t {
    NEW = 0,
    PARTIALLY_FILLED = 1,
    FILLED = 2,
    CANCELED = 3
};

struct Order {
    uint64_t order_id;
    OrderSide side;
    OrderType type;
    uint64_t price;      // 以最小价格单位表示（如 1/10000 元）
    uint64_t quantity;   // 原始数量
    uint64_t filled;     // 已成交数量
    uint64_t timestamp;  // 纳秒级时间戳
    OrderStatus status;

    // PriceLevel 链表指针（同一价格下的双向链表）
    Order* prev;
    Order* next;

    // 所属 PriceLevel 指针（撤单/改单时快速定位价格档位）
    class PriceLevel* parent_level;
};

struct PriceLevel {
    uint64_t price;
    uint64_t total_quantity;  // 该价格所有订单的总量
    Order* head;              // FIFO 队列头
    Order* tail;              // FIFO 队列尾
    PriceLevel* prev;         // 链表前驱（上一价格档位）
    PriceLevel* next;         // 链表后继（下一价格档位）
};
```

**为什么用双向链表管理同一价格的订单？**

- 新订单加到链表尾部（O(1)）；
- 撤单从链表中删除（O(1)）；
- 撮合时从链表头部取订单（O(1)）；
- 完美支持 FIFO 时间优先规则。

#### 6.1.4 完整的订单簿框架

```cpp
#include <unordered_map>
#include <cstdint>

class OrderBook {
public:
    OrderBook() : buy_head_(nullptr), sell_head_(nullptr) {}

    // 核心操作
    bool add_order(Order* order);
    bool cancel_order(uint64_t order_id);
    bool execute_order(uint64_t order_id, uint64_t quantity);

    // 查询
    uint64_t best_bid() const;   // 最高买价
    uint64_t best_ask() const;   // 最低卖价
    uint64_t bid_quantity() const;
    uint64_t ask_quantity() const;

private:
    // 价格链表：买单按价格降序，卖单按价格升序
    PriceLevel* buy_head_;   // 最高买价
    PriceLevel* sell_head_;  // 最低卖价

    // 订单哈希表：order_id → Order*
    std::unordered_map<uint64_t, Order*> order_map_;

    // 辅助方法
    PriceLevel* find_or_create_level(PriceLevel*& head, uint64_t price, bool is_buy);
    bool remove_level(PriceLevel*& head, PriceLevel* level);
};
```

> **思考点**：
> 1. 为什么买单链表用降序、卖单链表用升序？
> 2. `std::unordered_map` 在低延迟场景下有什么潜在问题？如何替代？
> 3. 价格档位的 `total_quantity` 为什么在处理部分成交时需要实时更新？

<details>
<summary><b>思考点答案</b></summary>

1. **买单降序、卖单升序**
   - Best Bid 是最高买入价，放在链表头部可以 O(1) 获取；
   - Best Ask 是最低卖出价，放在链表头部可以 O(1) 获取；
   - 撮合时从两端向中间扫描，自然形成由外到内的匹配顺序。

2. **unordered_map 的问题**
   - 哈希冲突时性能退化；
   - 内存分配不可控（rehash 触发大量重新分配）；
   - **迭代顺序不确定，难以调试**。`std::unordered_map` 是哈希表（链地址法），迭代器遍历的是桶（bucket）数组 + 每个桶内的链表，迭代顺序由**哈希函数、桶数量、插入历史（rehash）** 共同决定，与插入顺序完全无关：

     ```
     内部结构示意：
     ┌─────┬─────┬─────┬─────┬─────┐
     │bucket0│bucket1│bucket2│...│
     ├─────┼─────┼─────┼─────┼─────┤
     │      │  5→8  │  2→7  │      │
     └─────┴─────┴─────┴─────┴─────┘
     迭代输出：bucket1 中 5→8，bucket2 中 2→7 → {5}{8}{2}{7}
     ```

     这意味着**同样的数据、同样的插入顺序**，在不同编译器、不同 STL 实现、甚至同程序不同次运行（因之前的 rehash 历史不同）下输出的顺序都不同。这对调试造成直接影响：
     
     - **Bug 难以复现**：依赖遍历顺序的 Bug 只在特定桶排列下触发，下次运行可能不再出现；
     - **调试信息不可靠**：断点观察到的元素排列每次都可能不同，无法稳定跟踪某个订单；
     - **日志对比困难**：同样的业务逻辑产生不同的日志输出，让人误判"行为变了"；
     - **rehash 导致顺序突变**：运行中插入触发 rehash 后，所有老元素的迭代顺序完全打乱，可能在快照生成或批量遍历途中造成意外行为。
     
     而 `std::map` 的迭代顺序永远是 key 升序（红黑树中序遍历），在调试和日志分析上具有完全的确定性。这一点在订单簿这样需要频繁遍历和快照的场景中尤为重要。
   - **替代方案**：基于开放寻址的自定义哈希表、或直接使用大的数组（如果订单 ID 是递增的，可直接用 order_id 索引数组），或使用 `absl::flat_hash_map`。

3. **total_quantity 实时更新**
   - 撮合时 engine 需要快速判断某一价格档位是否有足够流动性；
   - 如果只在扫描到档位时才累加总量，每次 O(n) 遍历会拖慢撮合；
   - 在 add/cancel/execute 时维护 `total_quantity` 的增量更新，撮合时只需 O(1) 档位级别决策。

</details>

---

### 第 2 小节：限价单与市价单处理

#### 6.2.1 限价单（Limit Order）

限价单指定了**价格和数量**，只有达到指定价格或更优时才能成交。

**处理流程**

```
限价买单 100.50, 500 lot
  ↓
检查能否与卖单撮合：
  Best Ask = 101.00 > 100.50  → 不能成交
  ↓
把订单插入买单价格链表，price = 100.50
  ↓
行情更新：订单簿深度变化，发布增量行情
```

**限价单是否能立即成交取决于价格：**

| 条件 | 行为 |
|---|---|
| 买单价格 ≥ Best Ask | 立即撮合，可能部分成交或全部成交 |
| 买单价格 < Best Ask | 加入订单簿，等待后续撮合 |
| 卖单价格 ≤ Best Bid | 立即撮合，可能部分成交或全部成交 |
| 卖单价格 > Best Bid | 加入订单簿，等待后续撮合 |

#### 6.2.2 限价单插入逻辑

```cpp
bool OrderBook::add_order(Order* order) {
    // 1. 插入订单哈希表
    order_map_[order->order_id] = order;

    // 2. 查找或创建价格档位
    PriceLevel* level = find_or_create_level(
        order->side == OrderSide::BUY ? buy_head_ : sell_head_,
        order->price,
        order->side == OrderSide::BUY
    );

    // 3. 在 PriceLevel 的 FIFO 队列尾部插入
    order->parent_level = level;
    order->prev = level->tail;
    order->next = nullptr;
    if (level->tail) {
        level->tail->next = order;
    } else {
        level->head = order;
    }
    level->tail = order;
    level->total_quantity += order->quantity;

    return true;
}
```

#### 6.2.3 市价单（Market Order）

市价单不指定价格，以当前市场最优价格立即成交。

**处理流程**

```
市价买单 500 lot
  ↓
检查 Best Ask 是否存在（卖单是否为空）：
  如果没有卖单 → 拒绝（或等待）
  ↓
从 Best Ask 价格档位开始：
  取队列头部订单，按 FIFO 撮合
  档位总量不足 → 整个档位消耗完，移动到下一档位
  档位总量足够 → 部分成交该档位
  ↓
市价单永不进入订单簿
  全部成交 → 成交回报
  部分成交 → 剩余数量取消（或继续等待，视市场规则而定）
```

**关键区别：**

| 特性 | 限价单 | 市价单 |
|---|---|---|
| 指定价格 | ✅ 是 | ❌ 否 |
| 进入订单簿 | ✅ 未成交部分加入 | ❌ 永不加入 |
| 成交价格 | 指定价或更优 | 当前市场最优价 |
| 确定性 | 可能不成交 | 保证成交（只要对面有单） |

> 市价单在低流动性市场风险较大——可能以很差的价格吃掉多个档位。

#### 6.2.4 简单 add_order 逻辑示意

```cpp
enum class AddOrderResult {
    ACCEPTED,        // 订单加入订单簿
    FILLED,          // 全部成交
    PARTIALLY_FILLED // 部分成交，剩余加入订单簿
};

// 撮合引擎的 add_order 入口
AddOrderResult MatchingEngine::add_order(Order* order) {
    if (order->type == OrderType::MARKET) {
        return handle_market_order(order);
    }
    return handle_limit_order(order);
}

AddOrderResult MatchingEngine::handle_limit_order(Order* order) {
    // 买单：检查是否与卖单可成交
    if (order->side == OrderSide::BUY) {
        while (order->quantity > order->filled) {
            uint64_t best_ask = order_book_.best_ask();
            if (best_ask == 0 || order->price < best_ask) {
                break;  // 无法继续撮合
            }
            match_order(order);  // 与最优卖单撮合
        }
    } else {
        // 卖单：对称逻辑
        while (order->quantity > order->filled) {
            uint64_t best_bid = order_book_.best_bid();
            if (best_bid == 0 || order->price > best_bid) {
                break;
            }
            match_order(order);
        }
    }

    if (order->filled == order->quantity) {
        return AddOrderResult::FILLED;
    }

    // 剩余部分加入订单簿
    order_book_.add_order(order);
    return order->filled > 0 ?
        AddOrderResult::PARTIALLY_FILLED :
        AddOrderResult::ACCEPTED;
}
```

> **思考点**：
> 1. 限价单到达时，为什么不先检查价格能否成交就直接插入订单簿？
> 2. 市价单在全部成交前，如果对面流动性突然消失（撤单），应该怎么处理？
> 3. 一个"冰山订单"Iceberg Order（只显示部分数量）对订单簿设计有什么挑战？

<details>
<summary><b>思考点答案</b></summary>

1. **为什么先检查成交再插入？**
   - 限价单到达时可能与现有订单反向匹配（如 100.50 的买单到达时，Best Ask 是 100.40）。
   - 如果直接插入再撮合，多了一步不必要的插入/删除。
   - 正确的顺序是：先尝试撮合，剩余部分再进入订单簿。

2. **市价单流动性消失**
   - 典型的处理方案：市价单执行到流动性耗尽为止，剩余部分**取消**（不是全部取消，而是已成交部分正常结算）。
   - 有些市场规则支持"市价转限价"（Market-to-Limit, MTL），剩余部分以最后成交价转为限价单。
   - 极低延迟系统通常直接取消剩余部分，把决策交给策略层。

3. **冰山订单的挑战**
   - 订单簿上只显示冰山订单的"可见部分"。
   - 可见部分全部成交后，再从"隐藏部分"补充到可见部分。
   - 实现时需要区分 peak（峰值可见量）和 total（总隐藏量），并在 PriceLevel 中标记冰山订单。
   - 取消冰山订单时必须取消全部（可见 + 隐藏）。

</details>

---

### 第 3 小节：撤单与改单

#### 6.3.1 撤单（Cancel）

撤单是从订单簿中移除一个尚未成交（或部分成交）的订单。

**处理流程**

```
Cancel Order(id=5)
  ↓
在 OrderMap 中查找 order_id=5：
  找不到 → 拒绝（订单不存在或已成交）
  ↓
从订单中获取 parent_level 指针
  ↓
从 PriceLevel 的 FIFO 链表中移除：
  level->head == order → 更新 head
  level->tail == order → 更新 tail
  prev->next = order->next
  next->prev = order->prev
  ↓
减少 level->total_quantity：
  如果 level->total_quantity == 0 → 移除该 PriceLevel
  ↓
从 OrderMap 中移除该订单
  ↓
生成成交/撤单回报
```

#### 6.3.2 撤单实现

```cpp
CancelResult OrderBook::cancel_order(uint64_t order_id) {
    auto it = order_map_.find(order_id);
    if (it == order_map_.end()) {
        return CancelResult::NOT_FOUND;
    }

    Order* order = it->second;

    // 检查订单状态
    if (order->status == OrderStatus::FILLED ||
        order->status == OrderStatus::CANCELED) {
        return CancelResult::ALREADY_DONE;
    }

    // 从 PriceLevel 的双向链表中移除
    PriceLevel* level = order->parent_level;
    if (order->prev) {
        order->prev->next = order->next;
    } else {
        level->head = order->next;  // 是头部
    }
    if (order->next) {
        order->next->prev = order->prev;
    } else {
        level->tail = order->prev;  // 是尾部
    }

    level->total_quantity -= order->quantity;

    // 如果 PriceLevel 为空，从价格链表中移除
    if (level->total_quantity == 0) {
        remove_level(
            order->side == OrderSide::BUY ? buy_head_ : sell_head_,
            level
        );
    }

    order->status = OrderStatus::CANCELED;
    order_map_.erase(it);

    return CancelResult::SUCCESS;
}
```

#### 6.3.3 撤单的边界情况

| 场景 | 处理 |
|---|---|
| 撤单已成交订单 | 拒绝（订单已完成） |
| 撤单已在撮合中的订单 | 取决于阶段：已锁定但尚未正式成交 → 可撤销；已成交 → 不可撤销 |
| 撤单不存在的订单 | 返回 NOT_FOUND |
| 对方撤单后，正在撮合的订单 | 撮合过程中需检查对面订单是否已被撤单 |
| 重复撤单 | 幂等处理，第二次返回已取消状态 |

#### 6.3.4 改单（Modify/Replace）

改单本质是**撤单 + 重新下单**（Cancel-Replace）。

**处理原则：**

```
Modify Order(id=5, new_price=101.00, new_qty=600)
  ↓
记录原订单的价格/数量/时间
  ↓
撤单（保留原始时间）
  ↓
如果新价格与原价相同 → 更新数量（可能立即撮合）
如果新价格与原价不同 → 重新插入订单簿，使用新的（或原始）时间
  ↓
防止"价格发现"（Quote Matching）：
  如果减少数量 → 直接修改
  如果增加数量 → 新增部分视为新订单，时间优先级为当前时间
```

> ⚠️ **关键设计考虑**：改单时**时间戳如何处理**是两个常见做法：
> - **保留原始时间**：防止策略通过反复改单"插队"；
> - **重置为当前时间**：实现简单，但可被滥用。
>
> 大多数交易所保留原始时间戳（改价除外——价格变化意味着位置变化）。

> **思考点**：
> 1. 撤单一个正在被撮合的订单（match 函数已读取但尚未提交）会有什么问题？
> 2. 改单增加数量时，新增部分应该用什么时间戳？为什么？
> 3. 冰山订单的撤单和正常订单撤单有什么区别？

<details>
<summary><b>思考点答案</b></summary>

1. **正在撮合时撤单**
   - 这是典型的**竞态条件**。撮合线程从 order_map 获取了订单指针，但撤单线程同时从 order_map 删除并释放了该订单。
   - 解决方案：使用引用计数或延迟回收机制（如 hazard pointer、epoch-based reclamation），确保在有线程引用时订单不会被释放。
   - 单线程撮合引擎不存在这个问题——所有操作在同一个线程顺序执行。

2. **改单增量的时间戳**
   - 新增部分通常使用**当前时间**（新到达时间），因为这部分数量之前并不存在于订单簿中。
   - 如果保留原始时间，策略可以通过"改单加 1 手"的方式不断插队，破坏时间优先原则。
   - 有些交易所规则是：改单增加数量 → 整个订单时间重置为当前时间（可视为新订单）。

3. **冰山订单撤单**
   - 冰山订单有"可见部分"和"隐藏部分"，撤单时必须把两部分全部取消。
   - 可见部分可能在多个价格档位的多次撮合中被不断补充，需额外维护 iceberg state。
   - 处理完后还需要检查：可见部分目前已展示多少、隐藏部分还剩多少，确保完全从系统中移除。

</details>

---

### 第 4 小节：撮合逻辑与成交回报

#### 6.4.1 撮合匹配规则

撮合引擎的核心是**以价格-时间优先级**持续匹配买单和卖单。

**撮合循环逻辑（以限价买单为例）：**

```
限价买单 price = 100.50, qty = 1000
  ↓
while (order.filled < order.quantity) {
  best_ask = order_book.best_ask()
  if (best_ask == 0 || order.price < best_ask) break  // 无法继续

  // 从 Best Ask 价格档位取头部订单
  resting_order = order_book.get_best_ask_order()
  if (!resting_order) break

  match_qty = min(order.remaining(), resting_order.remaining())
  match_price = resting_order.price    // 以被动单价格成交

  // 生成成交记录（Trade）
  Trade trade = {
    .buy_order_id   = (order.side == BUY)  ? order.order_id : resting_order.order_id,
    .sell_order_id  = (order.side == SELL) ? order.order_id : resting_order.order_id,
    .price          = match_price,
    .quantity       = match_qty,
    .timestamp      = now()
  }

  // 更新双方订单
  order.filled += match_qty
  resting_order.filled += match_qty

  // 更新 PriceLevel 总数量
  resting_order.parent_level->total_quantity -= match_qty

  // 如果被动单已全部成交，将其从订单簿移除
  if (resting_order.remaining() == 0) {
    order_book_.remove_order(resting_order.order_id)
  }

  // 如果 PriceLevel 空了，移除该价格档位
  if (resting_order.parent_level->total_quantity == 0) {
    order_book_.remove_level(resting_order.parent_level)
  }

  // 发出成交回报通知
  on_trade(trade)
}
```

#### 6.4.2 完整的撮合匹配函数

```cpp
#include <vector>

struct Trade {
    uint64_t buy_order_id;
    uint64_t sell_order_id;
    uint64_t price;
    uint64_t quantity;
    uint64_t timestamp;
};

class MatchingEngine {
public:
    // 核心入口
    void process_new_order(Order* order);
    void process_cancel(uint64_t order_id);
    void process_modify(uint64_t order_id, uint64_t new_price, uint64_t new_qty);

    // 设置回调（用于发送成交回报和行情更新）
    using TradeCallback = std::function<void(const Trade&)>;
    using OrderUpdateCallback = std::function<void(const Order&)>;

    void set_trade_callback(TradeCallback cb) { trade_cb_ = std::move(cb); }
    void set_order_update_callback(OrderUpdateCallback cb) { order_update_cb_ = std::move(cb); }

private:
    OrderBook order_book_;
    TradeCallback trade_cb_;
    OrderUpdateCallback order_update_cb_;

    // 撮合一个活跃订单和挂单簿
    void match(Order* aggressive_order) {
        while (aggressive_order->remaining() > 0) {
            uint64_t best_price;
            if (aggressive_order->side == OrderSide::BUY) {
                best_price = order_book_.best_ask();
                if (best_price == 0 || aggressive_order->price < best_price) {
                    break;  // 无法撮合
                }
            } else {
                best_price = order_book_.best_bid();
                if (best_price == 0 || aggressive_order->price > best_price) {
                    break;
                }
            }

            // 从对面的 Best Level 取头部订单
            Order* resting = (aggressive_order->side == OrderSide::BUY)
                ? order_book_.get_best_ask_order()
                : order_book_.get_best_bid_order();

            if (!resting) break;

            // 计算成交数量
            uint64_t match_qty = std::min(
                aggressive_order->remaining(),
                resting->remaining()
            );

            // 更新双方
            aggressive_order->filled += match_qty;
            resting->filled += match_qty;
            resting->parent_level->total_quantity -= match_qty;

            // 生成成交回报
            Trade trade;
            if (aggressive_order->side == OrderSide::BUY) {
                trade = {aggressive_order->order_id, resting->order_id,
                         resting->price, match_qty, current_timestamp_ns()};
            } else {
                trade = {resting->order_id, aggressive_order->order_id,
                         resting->price, match_qty, current_timestamp_ns()};
            }

            if (trade_cb_) trade_cb_(trade);

            // 移除已全部成交的被动单
            if (resting->remaining() == 0) {
                order_book_.remove_order(resting->order_id);
            }

            // 移除已空的 PriceLevel
            if (resting->parent_level->total_quantity == 0) {
                order_book_.remove_level(resting->parent_level);
            }
        }

        // 如果主动单还有剩余，加入订单簿
        if (aggressive_order->remaining() > 0 &&
            aggressive_order->type == OrderType::LIMIT) {
            order_book_.add_order(aggressive_order);
            if (order_update_cb_) order_update_cb_(*aggressive_order);
        }
    }
};
```

#### 6.4.3 成交回报（Trade Report）

撮合产生的结果需要通知两类接收者：

| 回报类型 | 接收方 | 传输方式 |
|---|---|---|
| **成交回报（Execution Report）** | 订单网关 → 客户端 | TCP（可靠） |
| **行情更新（Market Data Incremental）** | 行情发布器 → 所有客户端 | UDP 组播（低延迟） |

**成交回报的数据结构：**

```cpp
struct ExecutionReport {
    uint64_t order_id;         // 订单 ID
    uint64_t trade_id;         // 成交 ID
    uint64_t price;            // 成交价格
    uint64_t quantity;         // 成交数量
    uint64_t remaining;        // 订单剩余数量
    OrderSide side;            // 买卖方向
    OrderStatus status;        // 新状态（FILLED / PARTIALLY_FILLED / CANCELED）
    uint64_t timestamp;        // 成交时间
};
```

**行情更新（Incremental Market Data）：**

```cpp
struct MarketDataIncremental {
    enum class Action : uint8_t {
        ADD = 0,      // 新订单加入
        CANCEL = 1,   // 订单取消
        EXECUTE = 2,  // 订单成交（数量减少）
    };

    Action action;
    OrderSide side;
    uint64_t price;
    uint64_t quantity;  // 变化数量
    uint64_t timestamp;
};
```

> **关键设计思想**：成交回报和行情更新从**同一个撮合事件**产生，但走不同的通道。成交回报只发给相关客户端，行情更新广播给所有市场参与者。

#### 6.4.4 处理部分成交

订单撮合时很少是"一个订单全部吃掉对面整个档位"。更多情况是：

**场景**：买单 1000 lot vs 最佳卖价档位有 3 个订单，总共 650 lot

```
被动卖单队列 (Best Ask = 101.50):
  ┌──────────────────────────────────────────────┐
  │ Order A: 200 lot  → 全部成交                  │
  │ Order B: 300 lot  → 全部成交                  │
  │ Order C: 150 lot  → 部分成交（成交 100 lot）   │
  │                                               │
  │ 主动买单剩下 350 lot → 继续扫下一 Ask 档位      │
  └──────────────────────────────────────────────┘
```

**部分成交的逻辑要点：**

1. 如果主动单数量大于被动单队列总量 → 整个档位被清空，主动单继续扫下一档位；
2. 如果被动单队列中前几个订单全部成交，最后一个订单部分成交 → 更新该订单数量；
3. 每次成交都要立即更新 PriceLevel 的 `total_quantity`；
4. 每次成交都要生成成交回报，发送给主动单和被部分成交的被动单。

> **思考点**：
> 1. 撮合时，为什么成交价格用被动单的价格而不是主动单的价格？
> 2. 如果一张市价买单进入时，对面恰好没有任何卖单，应该怎么处理？
> 3. 订单薄中 deep price level 的快速访问（例如深度 10 档的快照）对数据结构有什么要求？

<details>
<summary><b>思考点答案</b></summary>

1. **为什么用被动单价格成交？**
   - 主动单（Aggressive Order）是主动"吃单"的一方，按照挂单的价格成交。
   - 如果限价买单价格为 100.50，对面 Best Ask 是 100.40，则成交价为 100.40（被动单的价格）。
   - 这保证了挂单方（流动性提供者）获得预期的价格。

2. **市价单对面无流动性**
   - 有几种处理方式：
     - **拒绝**：市价单无法执行，直接返回拒绝消息。
     - **等待**：部分系统允许市价单等待（但低延迟系统很少这样做）。
     - **转限价**：按最后市场价转为限价单（部分市场规则）。
   - 书中交易所采用最保守的做法：**无法撮合则拒绝**。

3. **深度行情快速访问**
   - 需要价格链表支持双向遍历（从 Best 开始前/后移动 N 档）。
   - 如果 PriceLevel 用数组索引而非链表，可以通过下标 O(1) 访问任意档位。
   - 但数组方案最大的问题是：价格区间未知，且档位插入/删除涉及元素移动。
   - 折中方案：**skiplist**（跳跃表），支持 O(log n) 查找 + O(1) 顺序遍历。

</details>

---

### 第 5 小节：撮合引擎的集成与线程交互

#### 6.5.1 单线程撮合模型

**最重要的设计决策**：撮合引擎本身是**单线程**的。

```
┌─────────────────────────────────────────────────────┐
│                  撮合线程（Matching Thread）          │
│                                                      │
│  输入队列                   输出队列                    │
│  ┌─────────┐              ┌──────────┐              │
│  │ 新订单   │──→ OrderBook →│ 成交回报  │              │
│  │ 撤单     │──→ Matching  │ 行情更新  │              │
│  │ 改单     │──→ Logic     │          │              │
│  └─────────┘              └──────────┘              │
│                                                      │
│  所有操作在同一线程顺序执行                            │
│  → 不需要锁！                                         │
└─────────────────────────────────────────────────────┘
```

**为什么撮合引擎必须是单线程？**

| 原因 | 说明 |
|---|---|
| **免锁** | 单线程处理无竞争，完全不需要互斥锁 |
| **确定性强** | 没有线程切换、没有锁等待，延迟可预测 |
| **缓存友好** | OrderBook 数据保持在同一个核心的 L1/L2 缓存中 |
| **逻辑简单** | 无需考虑并发修改订单簿的竞态条件 |
| **性能足够** | 单线程已能处理每秒数百万笔订单（只做内存操作） |

**单线程不等于慢**，撮合引擎的核心操作（比较价格、更新数量、链表操作）都是 O(1) 或 O(log n)，单核足够。

#### 6.5.2 线程间的无锁通信

撮合引擎虽然单线程，但需要与外部通信：

```
┌────────────┐     SPSC      ┌──────────────┐     SPSC      ┌────────────┐
│ 订单网关    │───队列───→    │  撮合引擎     │───队列───→    │  行情发布器  │
│ (TCP线程)   │              │  (撮合线程)   │              │  (UDP线程)  │
└────────────┘              │              │              └────────────┘
                             │   ┌────────┐│              ┌────────────┐
┌────────────┐     SPSC      │   │OrderBook││    SPSC      │  订单网关   │
│ 其他输入   │───队列───→    │   └────────┘│───队列───→    │  (TCP线程)  │
└────────────┘              └──────────────┘              └────────────┘
```

**四个无锁队列：**

| 队列 | 生产者 | 消费者 | 内容 |
|---|---|---|---|
| 输入队列 1 | 订单网关线程 | 撮合线程 | 新订单、撤单、改单请求 |
| 输入队列 2 | 其他来源 | 撮合线程 | 管理员命令、系统控制 |
| 输出队列 1 | 撮合线程 | 行情发布器线程 | 增量行情（Market Data Incremental） |
| 输出队列 2 | 撮合线程 | 订单网关线程 | 成交回报（Execution Report） |

#### 6.5.3 撮合引擎的 SPSC 队列集成

```cpp
#include <thread>
#include <atomic>

class Exchange {
public:
    Exchange()
        : running_(false)
        , order_input_queue_(1024 * 1024)    // 输入队列：1M 容量
        , md_output_queue_(1024 * 1024)      // 行情输出队列
        , exec_output_queue_(1024 * 1024)    // 成交回报输出队列
    {}

    void start() {
        running_ = true;

        // 启动撮合线程
        matching_thread_ = std::thread([this]() {
            pin_to_core(2);  // 绑定到核心 2
            matching_loop();
        });

        // 启动行情发布器线程
        md_publisher_thread_ = std::thread([this]() {
            pin_to_core(3);
            md_publish_loop();
        });

        // 启动订单网关线程
        gateway_thread_ = std::thread([this]() {
            pin_to_core(4);
            gateway_loop();
        });
    }

    void stop() {
        running_ = false;
        matching_thread_.join();
        md_publisher_thread_.join();
        gateway_thread_.join();
    }

    // 订单网关调用：向撮合引擎提交新订单
    bool submit_order(Order* order) {
        return order_input_queue_.push(reinterpret_cast<uint64_t>(order));
    }

private:
    // 撮合引擎主循环
    void matching_loop() {
        // 设置撮合引擎回调
        matching_engine_.set_trade_callback([this](const Trade& trade) {
            // 成交回报 → 输出到网关队列
            ExecutionReport report = make_exec_report(trade);
            exec_output_queue_.push(report);

            // 行情更新 → 输出到行情队列
            MarketDataIncremental md = make_md_update(trade);
            md_output_queue_.push(md);
        });

        // 主循环：从输入队列取消息并处理
        while (running_) {
            uint64_t raw;
            if (order_input_queue_.pop(raw)) {
                Order* order = reinterpret_cast<Order*>(raw);
                matching_engine_.process_new_order(order);
            }
            // 也可以处理其他类型的消息（撤单、改单等）
        }
    }

    void md_publish_loop() {
        while (running_) {
            MarketDataIncremental md;
            if (md_output_queue_.pop(md)) {
                // 编码并发送 UDP 行情
                encode_and_send_md(md);
            }
        }
    }

    void gateway_loop() {
        while (running_) {
            ExecutionReport report;
            if (exec_output_queue_.pop(report)) {
                // 查找对应客户端连接并发送
                send_to_client(report);
            }
        }
    }

    // 线程绑定辅助
    void pin_to_core(int core_id) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    }

    // 各类 SPSC 队列
    SPSCQueue<uint64_t, 1024 * 1024> order_input_queue_;
    SPSCQueue<MarketDataIncremental, 1024 * 1024> md_output_queue_;
    SPSCQueue<ExecutionReport, 1024 * 1024> exec_output_queue_;

    MatchingEngine matching_engine_;
    std::thread matching_thread_;
    std::thread md_publisher_thread_;
    std::thread gateway_thread_;
    std::atomic<bool> running_;
};
```

#### 6.5.4 行情生成与快照

**增量行情（Incremental Update）**：每次订单簿发生变化时发布，包含变化的内容。

| 事件 | 增量行情内容 |
|---|---|
| 新限价单加入 | ADD, side, price, quantity |
| 撤单 | CANCEL, side, price, quantity（消失的数量） |
| 成交 | EXECUTE, side, price, quantity（减少的数量） |

**快照（Snapshot）**：定期发布完整的订单簿状态，供新连接的客户端"从头"重建。

```
快照周期：
  增量1 → 增量2 → 增量3 → ... → 快照 → 增量 → 增量 → ...
                                 ↑
                         新客户端从快照开始，
                         然后应用后续增量
```

**快照生成逻辑：**

```cpp
void Exchange::generate_snapshot() {
    // 遍历买单链表（降序）
    std::vector<MarketDataSnapshot::Level> bids;
    PriceLevel* bid_level = order_book_.buy_head();
    while (bid_level && bids.size() < kSnapshotDepth) {
        bids.push_back({bid_level->price, bid_level->total_quantity});
        bid_level = bid_level->next;
    }

    // 遍历卖单链表（升序）
    std::vector<MarketDataSnapshot::Level> asks;
    PriceLevel* ask_level = order_book_.sell_head();
    while (ask_level && asks.size() < kSnapshotDepth) {
        asks.push_back({ask_level->price, ask_level->total_quantity});
        ask_level = ask_level->next;
    }

    // 发送快照
    MarketDataSnapshot snapshot = {
        .timestamp = current_timestamp_ns(),
        .sequence = ++snapshot_sequence_,
        .bids = std::move(bids),
        .asks = std::move(asks),
    };
    send_snapshot(snapshot);
}
```

**快照与增量的配合：**

- 增量行情每条带一个 **sequence number**（递增序号）；
- 快照也带一个 **sequence number**（等于生成该快照时的最新增量序号）；
- 客户端收到快照后，从 sequence + 1 开始接收增量，保证不丢不重。

#### 6.5.5 整体运行流程

```
1. 订单网关收到客户端 TCP 消息
   ↓ 消息解码
2. 构造 Order 对象（从内存池分配）
   ↓ push 到 SPSC 输入队列
3. 撮合线程从输入队列 pop 出 Order
   ↓
4. 调用 MatchingEngine::process_new_order(order)
   ↓
5. 检查价格 → 是否立即撮合
   ↓
6. 需要撮合 → 调用 match()
   ↓
   发生成交 → 更新 OrderBook → 生成 Trade
                          ↓
               回调：生成 ExecutionReport → push 到输出队列
                    生成 MarketDataIncremental → push 到行情队列
   ↓
7. 未成交部分加入 OrderBook（限价单）
   ↓
8. 行情发布器线程 pop 行情队列 → 编码 → UDP 组播发送
   ↓
9. 订单网关返回线程 pop 成交回报队列 → 编码 → TCP 发送给客户端
```

**整个路径中没有任何锁，没有任何系统调用（除了收发包）。**

#### 6.5.6 尾延迟（Tail Latency）的保证

```cpp
// 撮合主循环加入批次处理，提高吞吐
void matching_loop_batched() {
    while (running_) {
        // 一次取多个订单批量处理
        constexpr size_t kBatchSize = 64;
        size_t processed = 0;

        uint64_t batch[kBatchSize];
        while (processed < kBatchSize) {
            if (!order_input_queue_.pop(batch[processed])) {
                break;  // 队列空，退出批处理
            }
            ++processed;
        }

        if (processed == 0) {
            _mm_pause();  // 无任务时降低功耗
            continue;
        }

        // 批量处理（不需要锁，都在同一线程）
        for (size_t i = 0; i < processed; ++i) {
            Order* order = reinterpret_cast<Order*>(batch[i]);
            matching_engine_.process_new_order(order);
        }

        // 批量处理行情输出
        md_publisher_.flush();
        exec_publisher_.flush();
    }
}
```

**批处理的作用**：

- 减少主循环的循环次数，提高指令缓存效率；
- 提高吞吐（一次处理多个，摊薄循环开销）；
- 但增加**单笔订单延迟**（需要等批次攒满或超时）。

> **权衡**：批处理大小 = 吞吐 vs 延迟的平衡
> - 批次小 → 延迟低，吞吐略低；
> - 批次大 → 吞吐高，但最坏延迟增加。

> **思考点**：
> 1. 撮合引擎为什么坚持单线程？多线程撮合有什么场景会需要吗？
> 2. 增量行情和快照的 sequence number 如何保证客户端能正确重建订单簿？
> 3. 批处理大小如何选择？是固定大小还是自适应？

<details>
<summary><b>思考点答案</b></summary>

1. **单线程 vs 多线程撮合**
   - 单线程：无锁、确定性高、延迟低。绝大多数交易所核心都是单线程。
   - 多线程需要的场景：
     - 按交易品种分片（sharding）：不同品种在不同线程撮合；
     - 市场数据量大到单核无法处理（极少数超大市场）。
   - 即使多线程，也是"每个品种单线程"的简单扩展，不存在多个线程共享一个订单簿。

2. **Sequence Number 的保证**
   - 增量行情每条都带递增 seq_no，严格有序；
   - 快照生成时记录当时的 seq_no（快照 seq）；
   - 客户端：收到快照后丢弃之前所有状态，从快照 seq + 1 开始接收增量；
   - 如果发现增量 seq_no 不连续，说明丢失了增量，请求重新传输或等待下一张快照。
   - 有些系统使用 gap fill 机制：客户端检测到 seq 跳跃时，向交易所请求丢失的增量数据。

3. **批处理大小的选择**
   - 固定大小：简单，但流量低时延迟不必要地高。
   - 自适应：根据当前队列深度动态调整。队列浅时小批次（低延迟），队列深时大批次（高吞吐）。
   - 常见做法：固定大小 + 超时机制——等待最多 N 个或最多 T 微秒，哪个先到就执行。
   - 批处理的核心思想：不要让批处理成为延迟瓶颈，而是让它在低负载时接近无批处理。

</details>

---

## 四、第六章实践任务清单

完成以下任务，才算真正掌握第六章：

### 必做

1. **限价订单簿实现**
   - 实现 `Order`、`PriceLevel`、`OrderBook` 数据结构；
   - 支持买单（降序）、卖单（升序）价格链表；
   - 支持 add_order（插入订单到指定价格档位）；
   - 支持 cancel_order（从哈希表 + 价格链表中移除）；
   - 支持 top of book 查询（best_bid / best_ask）；
   - 实现 PriceLevel FIFO 队列（双向链表管理同价订单）。

2. **撮合逻辑实现**
   - 实现限价单处理：先判断是否能与反向订单撮合，剩余部分加入订单簿；
   - 实现市价单处理：按 Best Ask/Bid 顺序吃单，直至全部成交或对面无单；
   - 实现 match() 核心逻辑：从对面 Best Level 取头部订单 → 计算成交数量 → 更新双方状态；
   - 处理部分成交：一个主动单扫多个档位、多个被动单的场景。

3. **撤单与改单**
   - 实现 cancel_order 的完整流程（查 → 删 → 更新 quantity → 可能的 PriceLevel 清理）；
   - 实现 modify_order（cancel-replace 模式）；
   - 处理边界情况：重复撤单、撤不存在的订单、正在撮合的订单。

4. **成交回报与行情生成**
   - 撮合时生成成交回报（ExecutionReport）；
   - 撮合时生成增量行情（MarketDataIncremental）；
   - 实现行情快照（Snapshot）生成功能。

5. **撮合引擎集成**
   - 实现 SPSC 队列连接订单网关（输入）和行情发布器（输出）；
   - 实现单线程撮合主循环；
   - 实现线程绑定（撮合线程 → 核心 2，行情线程 → 核心 3，网关线程 → 核心 4）。

### 选做（加深理解）

6. **订单簿 benchmark**
   - 生成随机订单流（含限价单、市价单、撤单）；
   - 测量单线程撮合引擎能处理多少订单/秒；
   - 观察 add / cancel / match 的延迟分布。

7. **批处理优化**
   - 实现批量撮合（一次从队列取多个订单处理）；
   - 对比固定批次 vs 自适应批次 vs 无边界的延迟和吞吐；
   - 找到最优的批处理大小。

8. **冰山订单（Iceberg Order）实现**
   - 扩展 Order 结构支持隐藏数量；
   - 实现可见部分 + 隐藏部分的自动补充逻辑；
   - 注意冰山订单在行情中的显示策略（只显示可见部分总量）。

9. **行情快照 vs 增量一致性验证**
   - 设计一个测试：持续生成订单操作 → 记录所有增量 → 定期快照；
   - 从快照 + 增量重建订单簿 → 验证与撮合引擎中的订单簿完全一致。

10. **思考题**
    - 价格-时间优先级与 Pro-Rata（按比例分配）有什么区别？各有什么优缺点？
    - 如果撮合引擎要在多核上扩展，有哪些分片策略？
    - 行情快照生成时需要暂停撮合吗？如果不暂停，怎么保证快照的一致性？
    - 如何设计一个协议，让丢失了若干增量行情的客户端能够请求补发（gap fill）？

---

## 五、常见误区与注意事项

| 误区 | 正确认识 |
|---|---|
| ❌ "撮合引擎多线程更快" | ✅ 订单簿是高度有状态的数据结构，多线程引入锁或事务，反而降低性能。单线程已可处理数百万订单/秒 |
| ❌ "std::map 足够高效" | ✅ std::map 的节点分配在堆上，有动态内存和缓存问题。极致场景需要自定义预分配数据结构 |
| ❌ "限价单一定进订单簿" | ✅ 限价单到达时如果价格可以与对面撮合，立即成交，只有剩余部分才进订单簿 |
| ❌ "成交价用主动单的价格" | ✅ 成交价以被动单（挂单）的价格为准，主动单接受被动单的报价 |
| ❌ "撤单看到订单就删掉" | ✅ 正在被撮合引用的订单不能直接删除，需确保没有其他线程持有引用 |
| ❌ "行情快照可以和增量同时发" | ✅ 快照和增量需要配合 sequence number，确保客户端能正确重建 |
| ❌ "市价单一定全成交" | ✅ 对面流动性不足时，市价单可能部分成交或完全无法成交 |

---

## 六、下章预告

第七章会进入**与市场参与者通信**，包括：

- 行情协议（Market Data Protocol）：如何设计高效的二进制编码；
- 订单协议（Order Protocol）：客户端 → 交易所的订单编码/解码；
- 行情发布器（Market Data Publisher）：UDP 组播实现；
- 订单网关服务器（Order Gateway Server）：TCP 多客户端支持；
- FIFO Sequencer：保证多数据源的消息顺序。

第六章实现的撮合引擎是交易所的心脏，第七章则为这颗心脏接上血管（网络通信），让交易所真正可运行。

---

## 七、推荐学习节奏

| 时间 | 内容 |
|---|---|
| 第 1–2 天 | 第 1 小节：订单簿数据结构设计，理解 price-time priority |
| 第 3–4 天 | 第 2 小节：限价单与市价单处理流程 + 编码实现 |
| 第 5–6 天 | 第 3 小节：撤单与改单逻辑，边界情况处理 |
| 第 7–9 天 | 第 4 小节：撮合逻辑核心 + 成交回报与行情生成（重点） |
| 第 10–12 天 | 第 5 小节：撮合引擎集成、线程交互、批处理优化 |
| 第 13–14 天 | 综合实践：完整运行撮合引擎，测量延迟 |

> ⚠️ 第六章是全书最核心的代码章。建议逐行理解、逐行实现，配合测试确保正确性。

---

## 八、本章核心金句

> "撮合引擎的核心竞争力不在算法复杂度，而在延迟的确定性和可预测性。"

> "单线程不是技术限制，而是设计选择——免锁是低延迟的第一原则。"

> "价格-时间优先是公平的基石：不是谁的价格好谁赢，而是谁先来谁先成交。"

> "市价单是主动吃单，限价单是挂单等待——两者角色的设计决定了撮合引擎的行为模式。"

> "增量行情告诉市场变了什么，快照告诉市场现在是什么——两者缺一不可。"
