#ifndef MATCHING_ENGINE_ORDER_BOOK_H
#define MATCHING_ENGINE_ORDER_BOOK_H

#include <cstddef>  // size_t
#include <utility>  // pair

#include "Types.h"
#include "Order.h"
#include "PriceLevel.h"
#include "OrderIndex.h"
#include "Pool.h"

// ============================================================================
// OrderBook — 限价订单簿（教材 6.1.4）
// ============================================================================
//
// 分层数据结构（生产级实现）：
//   OrderBook
//   ├── BuySide (双向价格链表，降序)     ← buy_head_ = best bid
//   │   ├── PriceLevel(101.00)          ← 从 Pool 分配
//   │   │   ├── Order(id=1, qty=200)
//   │   │   └── Order(id=5, qty=300)
//   │   └── PriceLevel(100.50)
//   │       └── Order(id=2, qty=500)
//   ├── SellSide (双向价格链表，升序)     ← sell_head_ = best ask
//   │   ├── PriceLevel(101.50)
//   │   │   └── Order(id=3, qty=200)
//   │   └── PriceLevel(102.00)
//   │       ├── Order(id=4, qty=300)
//   │       └── Order(id=6, qty=100)
//   └── OrderIndex (开放式寻址哈希表)
//       ├── 1 → &Order(1)
//       ├── 2 → &Order(2)
//       └── ...
//
// 与教学版（std::unordered_map + new/delete）的关键区别：
//
//   std::unordered_map         OrderIndex（本实现）
//   ├── 节点堆分配，rehash     ├── 一次 heap 预分配，零运行期分配
//   ├── 链式散列，缓存不友好    ├── 连续数组，缓存友好
//   ├── 迭代顺序不确定          ├── 遍历顺序确定（便于调试）
//   └── 哈希冲突链表指针追逐    └── 线性探测，无间接跳转
//
//   new/delete PriceLevel       Pool<PriceLevel, 4096>
//   ├── 每次 new 触发 malloc   ├── 预分配对象池，O(1) 分配/归还
//   ├── 堆碎片化风险            ├── 零碎片
//   └── 延迟不可预测            └── 延迟确定（单次指针赋值）
//
// 核心复杂度：
//   - add_order:      O(log N)* 查找/插入价格档位 + O(1) 追加 FIFO
//                     *实际多数操作在链表近端（best 附近），近似 O(1)
//   - cancel_order:   O(1) 查哈希表 + O(1) 从链表删除
//   - best_bid/ask:   O(1)
//   - 撮合取订单:     O(1) PriceLevel::front()
// ============================================================================

/// PriceLevel 对象池大小（支持最大 4096 个不同价格档位）
static constexpr size_t kMaxPriceLevels = 4096;

class OrderBook {
public:
    OrderBook();

    // ---- 核心操作 ----

    /// 将订单加入订单簿（不检查是否可撮合，仅插入）
    /// @return true 插入成功
    bool add_order(Order* order);

    /// 根据 order_id 撤单
    /// @return CancelResult::SUCCESS / NOT_FOUND / ALREADY_DONE
    CancelResult cancel_order(uint64_t order_id);

    /// 执行成交：减少被动单的已成交数量（供 MatchingEngine 调用）
    /// @return true 执行成功
    bool execute_order(uint64_t order_id, uint64_t match_qty);

    /// 将已全部成交的订单从订单簿移除
    /// @return true 移除成功
    bool remove_order(uint64_t order_id);

    /// 移除已空的 PriceLevel
    /// @return true 移除成功
    bool remove_level(PriceLevel* level);

    // ---- 查询接口（Top of Book） ----

    /// 最高买价（best bid），若无买单返回 0
    uint64_t best_bid() const;

    /// 最低卖价（best ask），若无卖单返回 0
    uint64_t best_ask() const;

    /// 买单总数量
    uint64_t bid_quantity() const;

    /// 卖单总数量
    uint64_t ask_quantity() const;

    /// 获取 best bid 价格档位的头部订单（撮合用）
    Order* get_best_bid_order() const;

    /// 获取 best ask 价格档位的头部订单（撮合用）
    Order* get_best_ask_order() const;

    /// 根据 order_id 查找订单（nullptr 表示不存在）
    Order* find_order(uint64_t order_id) const;

    /// 获取买单链表头（用于遍历 / 快照生成）
    const PriceLevel* buy_head() const { return buy_head_; }

    /// 获取卖单链表头（用于遍历 / 快照生成）
    const PriceLevel* sell_head() const { return sell_head_; }

    /// 订单簿中活跃订单数量
    size_t order_count() const { return order_index_.size(); }

    /// PriceLevel 池已用量
    size_t level_used() const { return level_pool_.used(); }

    // ---- 快照 ----

    /// 生成行情快照，填充到指定深度的 bids 和 asks 数组
    /// @param depth  最大档位数
    /// @param bids   输出：买单档位数组
    /// @param asks   输出：卖单档位数组
    /// @return (实际买单深度, 实际卖单深度)
    std::pair<size_t, size_t> snapshot(size_t depth,
                                       SnapshotLevel* bids,
                                       SnapshotLevel* asks) const;

private:
    PriceLevel* buy_head_;     // 买单价格链表头（降序 → best bid 在第一个）
    PriceLevel* sell_head_;    // 卖单价格链表头（升序 → best ask 在第一个）

    // 开放式寻址哈希表：order_id → Order*
    // 注意：不拥有 Order* 所有权，调用方负责 Order 生命周期
    OrderIndex order_index_;

    // PriceLevel 对象池：预分配，零运行期动态分配
    Pool<PriceLevel, kMaxPriceLevels> level_pool_;

    // ---- 辅助方法 ----

    /// 查找或创建价格档位
    /// @param head    所在侧的价格链表头（buy_head_ 或 sell_head_）
    /// @param price   目标价格
    /// @param is_buy  true = 买单侧（降序插入），false = 卖单侧（升序插入）
    /// @return 有效的 PriceLevel*；池满时返回 nullptr
    PriceLevel* find_or_create_level(PriceLevel*& head, uint64_t price, bool is_buy);

    /// 从价格链表中移除一个 PriceLevel（前提：其订单队列已空）
    /// @param head   所在侧的价格链表头（buy_head_ 或 sell_head_）
    /// @param level  要移除的 PriceLevel
    void detach_level(PriceLevel*& head, PriceLevel* level);
};

#endif // MATCHING_ENGINE_ORDER_BOOK_H
