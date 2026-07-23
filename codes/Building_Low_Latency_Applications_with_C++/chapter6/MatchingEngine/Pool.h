#ifndef MATCHING_ENGINE_POOL_H
#define MATCHING_ENGINE_POOL_H

#include <cstddef>
#include <new>   // placement new
#include <cstdint>

// ============================================================================
// Pool — 固定容量对象池（Free List 分配器）
// ============================================================================
// 生产环境低延迟场景的核心分配器：
//
//   - 所有内存预分配，运行期零动态分配
//   - allocate / deallocate O(1)，无锁
//   - 避免 new / delete 的堆分配延迟
//
// 用法：
//   Pool<PriceLevel, 4096> pool;
//   PriceLevel* lvl = pool.allocate(10100);  // 相当于 new PriceLevel(10100)
//   pool.deallocate(lvl);                    // 相当于 delete lvl
//
// 注意：
//   - 容量 N 在编译期固定，耗尽时 allocate() 返回 nullptr
//   - 对象在 allocate() 时 placement-new 构造，deallocate() 时析构
//   - 内部使用 struct AlignedStorage 确保对齐正确
// ============================================================================

template<typename T, size_t N>
class Pool {
    static_assert(N > 0, "Pool capacity must be > 0");
    static_assert(N <= 65536, "Pool capacity limited to 65536 for index fit in uint16_t");

    /// 每个元素的对齐存储
    struct alignas(alignof(T)) AlignedStorage {
        uint8_t data[sizeof(T)];
    };

public:
    static constexpr size_t kCapacity = N;

    Pool()
        : free_head_(0)
        , used_(0)
    {
        // 初始化空闲链表：每个槽指向下一个
        for (size_t i = 0; i < N - 1; ++i) {
            free_list_[i] = static_cast<uint16_t>(i + 1);
        }
        free_list_[N - 1] = kEndMarker;
    }

    /// 从池中分配一个对象（使用 placement new 构造）
    /// @param args  构造参数（转发给 T 的构造函数）
    /// @return      对象指针，池满时返回 nullptr
    template<typename... Args>
    T* allocate(Args&&... args) {
        if (free_head_ == kEndMarker) [[unlikely]] {
            return nullptr;
        }

        uint16_t idx = free_head_;
        free_head_ = free_list_[idx];
        ++used_;

        T* ptr = ::new (&storage_[idx]) T(std::forward<Args>(args)...);
        return ptr;
    }

    /// 归还对象到池（调用析构函数）
    void deallocate(T* ptr) {
        if (!ptr) [[unlikely]]
            return;

        ptr->~T();

        // 计算索引 = ptr - storage_ 的地址差
        size_t idx = reinterpret_cast<AlignedStorage*>(ptr) - storage_;
        free_list_[idx] = free_head_;
        free_head_ = static_cast<uint16_t>(idx);
        --used_;
    }

    /// 当前已分配数量
    size_t used() const { return used_; }

    /// 是否已满
    bool full() const { return free_head_ == kEndMarker; }

    /// 重置池（所有对象回到空闲链表）
    /// 警告：调用前需确保所有分配的对象已归还
    void reset() {
        for (size_t i = 0; i < N - 1; ++i) {
            free_list_[i] = static_cast<uint16_t>(i + 1);
        }
        free_list_[N - 1] = kEndMarker;
        free_head_ = 0;
        used_ = 0;
    }

private:
    static constexpr uint16_t kEndMarker = 0xFFFF;

    alignas(alignof(T)) AlignedStorage storage_[N];
    uint16_t free_list_[N];
    uint16_t free_head_;
    size_t   used_;
};

#endif // MATCHING_ENGINE_POOL_H
