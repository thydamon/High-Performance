#ifndef MATCHING_ENGINE_PRICE_LEVEL_H
#define MATCHING_ENGINE_PRICE_LEVEL_H

#include <cstdint>
#include "Order.h"

// ============================================================================
// PriceLevel — 价格档位（教材 6.1.3）
// ============================================================================
// 同一价格的所有订单按 FIFO 顺序组成双向链表：
//   - 插入：加到 tail（O(1)）
//   - 删除：从链表移除（O(1)）
//   - 撮合：从 head 取单（O(1)）
//
// price levels 自身也是双向链表节点（prev / next 指向上下价格档位），
//   - 买单链表：价格降序（head = best bid）
//   - 卖单链表：价格升序（head = best ask）
// ============================================================================

struct PriceLevel {
    uint64_t    price;            // 价格档位
    uint64_t    total_quantity;   // 该价格所有订单的数量总和

    Order*      head;             // FIFO 队列头（最早到达）
    Order*      tail;             // FIFO 队列尾（最新到达）

    PriceLevel* prev;             // 价格链表前驱
    PriceLevel* next;             // 价格链表后继

    PriceLevel()
        : price(0)
        , total_quantity(0)
        , head(nullptr)
        , tail(nullptr)
        , prev(nullptr)
        , next(nullptr)
    {}

    explicit PriceLevel(uint64_t p)
        : price(p)
        , total_quantity(0)
        , head(nullptr)
        , tail(nullptr)
        , prev(nullptr)
        , next(nullptr)
    {}

    // ---- FIFO 队列操作 ----

    /// 向队列尾部追加一个订单
    void append_order(Order* order) {
        order->parent_level = this;
        order->prev = tail;
        order->next = nullptr;

        if (tail) {
            tail->next = order;
        } else {
            head = order;
        }
        tail = order;

        total_quantity += order->quantity;
    }

    /// 从队列中移除一个订单（不释放内存）
    void remove_order(Order* order) {
        if (order->prev) {
            order->prev->next = order->next;
        } else {
            head = order->next;     // 移除的是头部
        }

        if (order->next) {
            order->next->prev = order->prev;
        } else {
            tail = order->prev;     // 移除的是尾部
        }

        total_quantity -= order->quantity;

        order->prev = nullptr;
        order->next = nullptr;
        order->parent_level = nullptr;
    }

    /// 队列是否为空
    bool empty() const {
        return head == nullptr;
    }

    /// 头部订单（最早到达的挂单）
    Order* front() const {
        return head;
    }
};

#endif // MATCHING_ENGINE_PRICE_LEVEL_H
