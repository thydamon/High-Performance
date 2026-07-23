#ifndef MATCHING_ENGINE_ORDER_H
#define MATCHING_ENGINE_ORDER_H

#include <cstdint>
#include "Types.h"

// ============================================================================
// Order — 单个订单节点（教材 6.1.3）
// ============================================================================
// 采用侵入式双向链表节点设计：
//   - prev / next 用于同一 PriceLevel 内的 FIFO 队列
//   - parent_level 指向所属 PriceLevel，撤单时 O(1) 定位价格档位
//
// 侵入式设计 vs 分离式设计：
//   优点：减少一次间接寻址，缓存更友好
//   缺点：Order 不能同时属于多个容器
//   在订单簿中每个订单只属于一个 PriceLevel，选侵入式更合适
// ============================================================================

struct PriceLevel;  // 前向声明

struct Order {
    // ---- 订单核心字段 ----
    uint64_t    order_id;
    OrderSide   side;
    OrderType   type;
    uint64_t    price;       // 最小价格单位（如 1/10000 元）
    uint64_t    quantity;    // 原始数量
    uint64_t    filled;      // 已成交数量
    uint64_t    timestamp;   // 纳秒级时间戳
    OrderStatus status;

    // ---- 侵入式链表指针 ----
    // 同一 PriceLevel 内按到达时间排序（FIFO）
    Order*      prev;        // 前驱订单
    Order*      next;        // 后继订单

    // 所属 PriceLevel 指针（撤单时快速定位价格档位）
    PriceLevel* parent_level;

    // ---- 构造 & 辅助 ----
    Order()
        : order_id(0)
        , side(OrderSide::BUY)
        , type(OrderType::LIMIT)
        , price(0)
        , quantity(0)
        , filled(0)
        , timestamp(0)
        , status(OrderStatus::NEW)
        , prev(nullptr)
        , next(nullptr)
        , parent_level(nullptr)
    {}

    Order(uint64_t id, OrderSide s, OrderType t, uint64_t p, uint64_t q, uint64_t ts)
        : order_id(id)
        , side(s)
        , type(t)
        , price(p)
        , quantity(q)
        , filled(0)
        , timestamp(ts)
        , status(OrderStatus::NEW)
        , prev(nullptr)
        , next(nullptr)
        , parent_level(nullptr)
    {}

    /// 剩余未成交数量
    uint64_t remaining() const {
        return quantity - filled;
    }

    /// 是否已全部成交
    bool is_filled() const {
        return filled >= quantity;
    }

    /// 是否可参与撮合（新单或部分成交）
    bool is_active() const {
        return status == OrderStatus::NEW ||
               status == OrderStatus::PARTIALLY_FILLED;
    }
};

// 确保 Order 体积紧凑（不含虚函数、不含 padding 浪费）
// 在 x86-64 上预期 sizeof(Order) ≈ 72 字节
static_assert(sizeof(Order) <= 80, "Order size unexpectedly large");

#endif // MATCHING_ENGINE_ORDER_H
