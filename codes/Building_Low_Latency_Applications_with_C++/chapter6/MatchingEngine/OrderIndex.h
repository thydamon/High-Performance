#ifndef MATCHING_ENGINE_ORDER_INDEX_H
#define MATCHING_ENGINE_ORDER_INDEX_H

#include <cstddef>
#include <cstdint>
#include <functional>  // std::function for for_each

#include "Order.h"

// ============================================================================
// OrderIndex — 订单哈希表（开放式寻址，线性探测）
// ============================================================================
// 替代 std::unordered_map 的生产级实现：
//
//   特性                    std::unordered_map        OrderIndex
//   ─────────────────────────────────────────────────────────────────
//   内存分配                节点级堆分配              一次预分配
//   运行期动态分配            频繁（插入/ rehash）      零分配
//   内存布局                链式散列（指针追逐）       连续数组（缓存友好）
//   迭代顺序                不确定（桶排列决定）       槽位顺序（确定）
//   删除                    直接清理                 墓碑标记
//
// 设计要点：
//   - 容量固定（2^20 = 1,048,576 槽位），运行期不扩容
//   - 最大负载因子 0.7，超出时 insert 返回 false
//   - Structure-of-Arrays 布局：key / value / state 分离
//   - 线性探测：无额外指针跳跃，相邻元素共享 cache line
//   - 墓碑（DELETED）标记取代直接清空，保证探测链连续性
//
// 内存占用：
//   keys_   = 8 MiB
//   values_ = 8 MiB
//   states_ = 1 MiB
//   总计 ≈ 17 MiB（构造函数一次 heap 分配）
// ============================================================================

class OrderIndex {
public:
    /// 槽位容量（2^20 = 1,048,576）
    static constexpr size_t kCapacity = 1 << 20;

    /// 掩码（用于快速取模，capacity 必须是 2 的幂）
    static constexpr size_t kMask = kCapacity - 1;

    /// 最大负载因子（x/100）：0.7 → 最多 734,003 个元素
    static constexpr size_t kMaxLoadNumerator = 7;
    static constexpr size_t kMaxLoadDenominator = 10;
    static constexpr size_t kMaxSize = kCapacity * kMaxLoadNumerator / kMaxLoadDenominator;

    /// 墓碑标记值（用于 states_ 数组）
    static constexpr uint8_t kEmpty   = 0;
    static constexpr uint8_t kOccupied = 1;
    static constexpr uint8_t kDeleted  = 2;

    OrderIndex();
    ~OrderIndex();

    // 禁止拷贝
    OrderIndex(const OrderIndex&) = delete;
    OrderIndex& operator=(const OrderIndex&) = delete;

    // 允许移动（转移所有权）
    OrderIndex(OrderIndex&& other) noexcept;
    OrderIndex& operator=(OrderIndex&& other) noexcept;

    /// 插入 (order_id → Order*) 映射
    /// @return true 插入成功；false 键已存在或哈希表满
    bool insert(uint64_t order_id, Order* ptr);

    /// 查找 order_id 对应的 Order*
    /// @return Order* 指针；未找到返回 nullptr
    Order* find(uint64_t order_id) const;

    /// 删除一个映射
    /// @return true 删除成功；false 键不存在
    bool erase(uint64_t order_id);

    /// 清空所有映射
    void clear();

    /// 当前元素数量
    size_t size() const { return size_; }

    /// 是否为空
    bool empty() const { return size_ == 0; }

    /// 确定性地遍历所有活跃条目（按槽位顺序，稳定可预测）
    void for_each(std::function<void(uint64_t order_id, Order* ptr)> fn) const;

private:
    /// 计算 key 的哈希值并 mask 到容量范围
    static size_t hash(uint64_t key) {
        // MurmurHash3 64-bit finalizer — 对连续 ID 分布好
        key ^= key >> 33;
        key *= 0xff51afd7ed558ccdULL;
        key ^= key >> 33;
        key *= 0xc4ceb9fe1a85ec53ULL;
        key ^= key >> 33;
        return static_cast<size_t>(key & kMask);
    }

    uint64_t* keys_;      // [kCapacity] — 0 = 空槽
    Order**   values_;    // [kCapacity]
    uint8_t*  states_;    // [kCapacity] — kEmpty / kOccupied / kDeleted
    size_t    size_;      // 活跃元素数
};

#endif // MATCHING_ENGINE_ORDER_INDEX_H
