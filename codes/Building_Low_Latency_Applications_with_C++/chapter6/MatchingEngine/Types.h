#ifndef MATCHING_ENGINE_TYPES_H
#define MATCHING_ENGINE_TYPES_H

#include <cstdint>

// ============================================================================
// Matching Engine — 基础类型定义
// ============================================================================
//   - 枚举以 uint8_t 紧凑存储
//   - 价格以最小价格单位表示（整数，避免浮点误差）
// ============================================================================

/// 订单方向
enum class OrderSide : uint8_t {
    BUY  = 0,
    SELL = 1
};

/// 订单类型
enum class OrderType : uint8_t {
    LIMIT  = 0,
    MARKET = 1
};

/// 订单状态
enum class OrderStatus : uint8_t {
    NEW               = 0,
    PARTIALLY_FILLED  = 1,
    FILLED            = 2,
    CANCELED          = 3
};

/// 添加订单结果
enum class AddOrderResult : uint8_t {
    ACCEPTED,         // 订单加入订单簿（未成交）
    FILLED,           // 全部成交
    PARTIALLY_FILLED, // 部分成交，剩余加入订单簿
    REJECTED          // 被拒绝（如市价单对面无流动性）
};

/// 撤单结果
enum class CancelResult : uint8_t {
    SUCCESS,
    NOT_FOUND,
    ALREADY_DONE
};

/// 成交结构（撮合产生的一条成交记录）
struct Trade {
    uint64_t buy_order_id;
    uint64_t sell_order_id;
    uint64_t price;         // 成交价（以被动单价格为准）
    uint64_t quantity;      // 成交数量
    uint64_t timestamp;     // 纳秒时间戳
};

/// 成交回报（发送给订单网关 → 客户端）
struct ExecutionReport {
    uint64_t   order_id;
    uint64_t   trade_id;
    uint64_t   price;
    uint64_t   quantity;
    uint64_t   remaining;
    OrderSide  side;
    OrderStatus status;
    uint64_t   timestamp;
};

/// 增量行情（发送给行情发布器 → UDP 组播）
struct MarketDataIncremental {
    enum class Action : uint8_t {
        ADD     = 0,
        CANCEL  = 1,
        EXECUTE = 2
    };

    Action    action;
    OrderSide side;
    uint64_t  price;
    uint64_t  quantity;    // 变化数量
    uint64_t  timestamp;
};

/// 行情快照的一个价格档位
struct SnapshotLevel {
    uint64_t price;
    uint64_t quantity;
};

/// 行情快照
struct MarketDataSnapshot {
    uint64_t           timestamp;
    uint64_t           sequence;
    uint64_t           bid_depth;   // 实际买单深度
    uint64_t           ask_depth;   // 实际卖单深度
    SnapshotLevel      bids[10];    // 最多 10 档，0 = best bid
    SnapshotLevel      asks[10];    // 最多 10 档，0 = best ask
};

#endif // MATCHING_ENGINE_TYPES_H
