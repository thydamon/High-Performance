#include "OrderBook.h"
#include <algorithm>
#include <cassert>

// ============================================================================
// OrderBook 实现（教材 6.1.4 — 生产级订单簿框架）
// ============================================================================

OrderBook::OrderBook()
    : buy_head_(nullptr)
    , sell_head_(nullptr)
{
}

// ============================================================================
// add_order — 将订单插入订单簿（教材 6.2.2）
// ============================================================================
// 流程：
//   1. 插入订单哈希表
//   2. 查找或创建价格档位（买单降序 / 卖单升序）
//   3. 追加到 PriceLevel 的 FIFO 队列尾部
//
// 注意：此方法不检查价格是否可撮合，
//       调用方（MatchingEngine::match）应先撮合再插入剩余部分。
//
// 内存安全：
//   - OrderIndex 预分配，此处不触发动态分配
//   - PriceLevel 从 Pool 分配，池满时返回 false
// ============================================================================

bool OrderBook::add_order(Order* order) {
    if (!order) return false;

    // 1. 插入订单哈希表
    if (!order_index_.insert(order->order_id, order)) {
        return false;  // key 冲突或表满（理论上后者极难触发）
    }

    // 2. 查找或创建价格档位
    bool is_buy = (order->side == OrderSide::BUY);
    PriceLevel*& head = is_buy ? buy_head_ : sell_head_;

    PriceLevel* level = find_or_create_level(head, order->price, is_buy);
    if (!level) {
        // Pool 已满 — 回滚哈希表插入
        order_index_.erase(order->order_id);
        return false;
    }

    // 3. 在 PriceLevel 的 FIFO 队列尾部插入
    level->append_order(order);

    return true;
}

// ============================================================================
// cancel_order — 撤单（教材 6.3.2）
// ============================================================================
// 流程：
//   1. 在 OrderIndex 中查找 order_id
//   2. 检查订单状态（已成交或已取消 → 拒绝）
//   3. 从 PriceLevel 的 FIFO 链表中移除
//   4. 减少 PriceLevel::total_quantity
//   5. 如果价格档位为空 → 从价格链表中移除（归还到 Pool）
//   6. 从 OrderIndex 中移除（墓碑标记）
//
// 边界情况：
//   - 重复撤单 → ALREADY_DONE（订单在 Index 中但状态已 FILLED/CANCELED）
//   - 撤不存在的单 → NOT_FOUND
//   - 撤已成交的单 → ALREADY_DONE
//   - 撤正在撮合的单 → 单线程模型下不存在竞态
// ============================================================================

CancelResult OrderBook::cancel_order(uint64_t order_id) {
    // 1. 查找
    Order* order = order_index_.find(order_id);
    if (!order) {
        return CancelResult::NOT_FOUND;
    }

    // 2. 检查状态
    if (order->status == OrderStatus::FILLED ||
        order->status == OrderStatus::CANCELED) {
        return CancelResult::ALREADY_DONE;
    }

    // 3. 从 PriceLevel 的 FIFO 链表中移除
    PriceLevel* level = order->parent_level;
    if (!level) {
        // 理论上不应用发生：有 Index 条目却无 parent_level
        order_index_.erase(order_id);
        return CancelResult::SUCCESS;
    }

    level->remove_order(order);

    // 4. 更新订单状态
    order->status = OrderStatus::CANCELED;

    // 5. 从 OrderIndex 移除
    order_index_.erase(order_id);

    // 6. 如果 PriceLevel 为空，从价格链表中移除并归还到 Pool
    if (level->empty()) {
        PriceLevel*& head = (order->side == OrderSide::BUY) ? buy_head_ : sell_head_;
        detach_level(head, level);
    }

    return CancelResult::SUCCESS;
}

// ============================================================================
// execute_order — 执行成交（减少被动单的剩余数量）
// ============================================================================
// 由 MatchingEngine::match 调用，撮合时更新被动单的 filled 数量
// 以及 PriceLevel 的总量。
//
// 注意：此方法不自动移除已成交订单，
//       调用方应在判断 resting->remaining() == 0 后调用 remove_order。
// ============================================================================

bool OrderBook::execute_order(uint64_t order_id, uint64_t match_qty) {
    Order* order = order_index_.find(order_id);
    if (!order) {
        return false;
    }

    PriceLevel* level = order->parent_level;
    if (!level) return false;

    // 不能超过订单剩余数量
    uint64_t actual = std::min(match_qty, order->remaining());
    order->filled += actual;

    // 更新 PriceLevel 总量（撮合时减少的是被动单所属档位的总量）
    level->total_quantity -= actual;

    // 更新订单状态
    if (order->is_filled()) {
        order->status = OrderStatus::FILLED;
    } else {
        order->status = OrderStatus::PARTIALLY_FILLED;
    }

    return true;
}

// ============================================================================
// remove_order — 从订单簿移除已全部成交的订单
// ============================================================================
// 与 cancel_order 不同：此方法不检查状态、不更新状态，
// 直接清理订单簿中的索引（用于撮合完成后移除已填充的被动单）。
// ============================================================================

bool OrderBook::remove_order(uint64_t order_id) {
    Order* order = order_index_.find(order_id);
    if (!order) {
        return false;
    }

    PriceLevel* level = order->parent_level;

    if (level) {
        level->remove_order(order);
        // 如果 PriceLevel 空了，从链表中移除并归还到 Pool
        if (level->empty()) {
            PriceLevel*& head = (order->side == OrderSide::BUY) ? buy_head_ : sell_head_;
            detach_level(head, level);
        }
    }

    order_index_.erase(order_id);
    return true;
}

// ============================================================================
// remove_level — 移除已空的 PriceLevel（供外部调用）
// ============================================================================

bool OrderBook::remove_level(PriceLevel* level) {
    if (!level || !level->empty()) {
        return false;
    }

    // 通过遍历确定 level 属于哪一侧
    PriceLevel* cur = buy_head_;
    while (cur) {
        if (cur == level) {
            detach_level(buy_head_, level);
            return true;
        }
        cur = cur->next;
    }
    cur = sell_head_;
    while (cur) {
        if (cur == level) {
            detach_level(sell_head_, level);
            return true;
        }
        cur = cur->next;
    }
    return false;
}

// ============================================================================
// 查询接口（Top of Book）
// ============================================================================

uint64_t OrderBook::best_bid() const {
    return buy_head_ ? buy_head_->price : 0;
}

uint64_t OrderBook::best_ask() const {
    return sell_head_ ? sell_head_->price : 0;
}

uint64_t OrderBook::bid_quantity() const {
    uint64_t total = 0;
    PriceLevel* cur = buy_head_;
    while (cur) {
        total += cur->total_quantity;
        cur = cur->next;
    }
    return total;
}

uint64_t OrderBook::ask_quantity() const {
    uint64_t total = 0;
    PriceLevel* cur = sell_head_;
    while (cur) {
        total += cur->total_quantity;
        cur = cur->next;
    }
    return total;
}

Order* OrderBook::get_best_bid_order() const {
    if (!buy_head_) return nullptr;
    return buy_head_->front();
}

Order* OrderBook::get_best_ask_order() const {
    if (!sell_head_) return nullptr;
    return sell_head_->front();
}

Order* OrderBook::find_order(uint64_t order_id) const {
    return order_index_.find(order_id);
}

// ============================================================================
// snapshot — 生成行情快照
// ============================================================================

std::pair<size_t, size_t> OrderBook::snapshot(size_t depth,
                                              SnapshotLevel* bids,
                                              SnapshotLevel* asks) const {
    size_t bid_count = 0;
    size_t ask_count = 0;

    const PriceLevel* cur = buy_head_;
    while (cur && bid_count < depth) {
        bids[bid_count].price    = cur->price;
        bids[bid_count].quantity = cur->total_quantity;
        ++bid_count;
        cur = cur->next;
    }

    cur = sell_head_;
    while (cur && ask_count < depth) {
        asks[ask_count].price    = cur->price;
        asks[ask_count].quantity = cur->total_quantity;
        ++ask_count;
        cur = cur->next;
    }

    return {bid_count, ask_count};
}

// ============================================================================
// 私有辅助方法
// ============================================================================

// --------------------------------------------------------------------------
// find_or_create_level — 查找或创建价格档位
// --------------------------------------------------------------------------
// 在有序价格链表中查找目标价格：
//   - 找到 → 返回已有 PriceLevel
//   - 未找到 → 从 Pool 分配新 PriceLevel 并插入到正确位置
//
// 插入位置：
//   买单链表（降序）：
//     101.00 → 100.50 → 100.00
//     插入 100.75 → 位于 101.00 之后、100.50 之前
//
//   卖单链表（升序）：
//     101.50 → 102.00 → 102.50
//     插入 101.75 → 位于 101.50 之后、102.00 之前
//
// 返回值：有效的 PriceLevel*；池满时返回 nullptr
// --------------------------------------------------------------------------

PriceLevel* OrderBook::find_or_create_level(PriceLevel*& head, uint64_t price, bool is_buy) {
    // 情况 1：链表为空 → 从 Pool 分配新节点作为头
    if (!head) {
        head = level_pool_.allocate(price);
        return head;
    }

    // 情况 2：比头节点更优（买单价格更高 / 卖单价格更低）→ 分配新头
    if ((is_buy && price > head->price) || (!is_buy && price < head->price)) {
        PriceLevel* new_level = level_pool_.allocate(price);
        if (!new_level) return nullptr;
        new_level->next = head;
        head->prev = new_level;
        head = new_level;
        return new_level;
    }

    // 情况 3：正好是头节点
    if (head->price == price) {
        return head;
    }

    // 情况 4：遍历链表查找插入位置
    PriceLevel* cur = head;
    while (cur->next) {
        if (cur->next->price == price) {
            return cur->next;  // 找到已存在的档位
        }

        // 判断是否应插入在 cur 和 cur->next 之间
        if (is_buy) {
            // 买单降序：cur.price > price > cur.next.price
            if (cur->price > price && price > cur->next->price) {
                break;
            }
        } else {
            // 卖单升序：cur.price < price < cur.next.price
            if (cur->price < price && price < cur->next->price) {
                break;
            }
        }
        cur = cur->next;
    }

    // 在 cur 之后插入新档位（从 Pool 分配）
    PriceLevel* new_level = level_pool_.allocate(price);
    if (!new_level) return nullptr;

    new_level->prev = cur;
    new_level->next = cur->next;
    if (cur->next) {
        cur->next->prev = new_level;
    }
    cur->next = new_level;

    return new_level;
}

// --------------------------------------------------------------------------
// detach_level — 从价格链表中移除一个 PriceLevel 并归还到 Pool
// --------------------------------------------------------------------------
// 前提：level 的订单队列已为空（empty() == true）
// 负责：更新前后节点的指针 + 将 level 归还到 Pool
// --------------------------------------------------------------------------

void OrderBook::detach_level(PriceLevel*& head, PriceLevel* level) {
    if (!level) return;

    // 更新前驱
    if (level->prev) {
        level->prev->next = level->next;
    } else {
        head = level->next;  // 移除的是头节点
    }

    // 更新后继
    if (level->next) {
        level->next->prev = level->prev;
    }

    // 归还到 Pool（触发析构，但不释放堆内存）
    level_pool_.deallocate(level);
}
