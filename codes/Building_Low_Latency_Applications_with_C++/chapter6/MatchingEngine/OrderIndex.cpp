#include "OrderIndex.h"
#include <cstring>   // memset

// ============================================================================
// OrderIndex 实现 — 开放式寻址哈希表
// ============================================================================

OrderIndex::OrderIndex()
    : size_(0)
{
    // 一次性 heap 分配所有槽位内存（约 17 MiB）
    keys_   = new uint64_t[kCapacity]();
    values_ = new Order*[kCapacity]();
    states_ = new uint8_t[kCapacity]();
    // new[]() 的 () 已将 keys_ 初始化为 0，states_ 为 kEmpty
    // values_ 我们用 memset 确保安全
    std::memset(values_, 0, kCapacity * sizeof(Order*));
    std::memset(states_, kEmpty, kCapacity);
}

OrderIndex::~OrderIndex() {
    delete[] keys_;
    delete[] values_;
    delete[] states_;
}

// 移动构造函数
OrderIndex::OrderIndex(OrderIndex&& other) noexcept
    : keys_(other.keys_)
    , values_(other.values_)
    , states_(other.states_)
    , size_(other.size_)
{
    other.keys_   = nullptr;
    other.values_ = nullptr;
    other.states_ = nullptr;
    other.size_   = 0;
}

// 移动赋值
OrderIndex& OrderIndex::operator=(OrderIndex&& other) noexcept {
    if (this != &other) {
        delete[] keys_;
        delete[] values_;
        delete[] states_;

        keys_   = other.keys_;
        values_ = other.values_;
        states_ = other.states_;
        size_   = other.size_;

        other.keys_   = nullptr;
        other.values_ = nullptr;
        other.states_ = nullptr;
        other.size_   = 0;
    }
    return *this;
}

// ============================================================================
// insert — 插入或更新键值对
// ============================================================================
// 线性探测策略：
//   1. 计算初始槽位 h = hash(key)
//   2. 从 h 开始向后探测（环形）
//   3. 遇到 kEmpty → 在此插入
//   4. 遇到 key 匹配 → 更新值为新 ptr（不允许重复 key）
//   5. 遇到 kDeleted → 记录第一个墓碑位置作为备选插入点
//
// 不允许重复 key：如果 key 已存在，返回 false（调用方先 erase 再 insert）
// ============================================================================

bool OrderIndex::insert(uint64_t order_id, Order* ptr) {
    // 满容量检查
    if (size_ >= kMaxSize) [[unlikely]] {
        return false;
    }

    size_t idx = hash(order_id);
    size_t first_deleted = kCapacity;  // kCapacity 表示"未找到"

    while (states_[idx] != kEmpty) {
        if (states_[idx] == kOccupied && keys_[idx] == order_id) {
            // key 已存在 → 不允许重复
            return false;
        }
        if (states_[idx] == kDeleted && first_deleted == kCapacity) {
            first_deleted = idx;  // 记住第一个墓碑
        }
        idx = (idx + 1) & kMask;
    }

    // 使用墓碑位置（如果找到的话），否则使用当前空槽
    if (first_deleted != kCapacity) {
        idx = first_deleted;
    }

    keys_[idx]   = order_id;
    values_[idx] = ptr;
    states_[idx] = kOccupied;
    ++size_;

    return true;
}

// ============================================================================
// find — 查找键
// ============================================================================
// 线性探测：
//   1. 计算初始槽位 h = hash(key)
//   2. 从 h 开始向后探测（环形）
//   3. 遇到 kEmpty → 不在表中，返回 nullptr
//   4. 遇到 kOccupied 且 key 匹配 → 找到，返回 value
//   5. kDeleted → 继续探测
// ============================================================================

Order* OrderIndex::find(uint64_t order_id) const {
    size_t idx = hash(order_id);

    while (states_[idx] != kEmpty) {
        if (states_[idx] == kOccupied && keys_[idx] == order_id) {
            return values_[idx];
        }
        idx = (idx + 1) & kMask;
    }

    return nullptr;
}

// ============================================================================
// erase — 删除键
// ============================================================================
// 线性探测找到 key 后：
//   1. 将槽位标记为 kDeleted（墓碑）
//   2. 保留 key 和 value 不变（仅用于调试时可见）
//
// 墓碑的必要性：
//   插入和查找依赖"遇到 kEmpty 就停止"的规则。
//   直接清空会将探测链打断，导致链上后续元素不可达。
//   墓碑标记允许探测链继续穿越被删除的槽位。
//
// 注意：墓碑槽不会被 insert 直接复用（除非线性探测恰好扫到它），
//   但 insert 会优先使用第一个遇到的墓碑替代空槽。
// ============================================================================

bool OrderIndex::erase(uint64_t order_id) {
    size_t idx = hash(order_id);

    while (states_[idx] != kEmpty) {
        if (states_[idx] == kOccupied && keys_[idx] == order_id) {
            states_[idx] = kDeleted;
            --size_;
            return true;
        }
        idx = (idx + 1) & kMask;
    }

    return false;
}

// ============================================================================
// clear — 清空所有条目
// ============================================================================
// 直接重置全部状态为 kEmpty，不析构 value（OrderBook 负责 Order 生命周期）
// ============================================================================

void OrderIndex::clear() {
    std::memset(keys_,   0,        kCapacity * sizeof(uint64_t));
    std::memset(values_, 0,        kCapacity * sizeof(Order*));
    std::memset(states_, kEmpty,   kCapacity);
    size_ = 0;
}

// ============================================================================
// for_each — 确定性遍历
// ============================================================================
// 按槽位索引顺序遍历，每次运行顺序一致（区别于 unordered_map）。
// 这对快照生成和调试至关重要：同样的数据 → 同样的遍历输出。
// ============================================================================

void OrderIndex::for_each(std::function<void(uint64_t, Order*)> fn) const {
    for (size_t i = 0; i < kCapacity; ++i) {
        if (states_[i] == kOccupied) {
            fn(keys_[i], values_[i]);
        }
    }
}
