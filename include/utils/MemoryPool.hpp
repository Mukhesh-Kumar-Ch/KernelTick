#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <atomic>
#include <memory>
#include <new>
#include "../order_matching/Order.hpp"

#ifdef __linux__
#include "../utils/LinuxOptimizations.hpp"
#endif

// Lock-free recycling memory pool for Order objects.
// Pre-allocates a fixed-size contiguous buffer on the heap to avoid
// dynamic memory allocations and deallocations in the critical path.
template<size_t PoolSize = 1024 * 1024> // 1M orders by default
class OrderMemoryPool {
public:
    static_assert(PoolSize > 0, "PoolSize must be greater than 0");
    static_assert(PoolSize <= 0x7FFFFFFFULL, "PoolSize must fit within 31-bit index range");

    explicit OrderMemoryPool(int numa_node = -1) {
        const size_t bytes = sizeof(Order) * PoolSize;
#if defined(__linux__) && defined(HFT_NUMA_AVAILABLE)
        if (numa_node >= 0) {
            void* numa_ptr = hft::allocateOnNumaNode(bytes, numa_node);
            if (numa_ptr) {
                pool_ = static_cast<Order*>(numa_ptr);
                is_numa_allocated_ = true;
            }
        }
#else
        (void)numa_node;
#endif
        if (!pool_) {
            void* ptr = nullptr;
            if (posix_memalign(&ptr, 64, bytes) == 0 && ptr != nullptr) {
                pool_ = static_cast<Order*>(ptr);
            } else {
                pool_ = static_cast<Order*>(std::malloc(bytes));
            }
        }

        next_index_ = std::make_unique<uint32_t[]>(PoolSize);
        reset();
    }

    ~OrderMemoryPool() {
        if (pool_) {
#if defined(__linux__) && defined(HFT_NUMA_AVAILABLE)
            if (is_numa_allocated_) {
                numa_free(pool_, sizeof(Order) * PoolSize);
                pool_ = nullptr;
                return;
            }
#endif
            std::free(pool_);
            pool_ = nullptr;
        }
    }

    OrderMemoryPool(const OrderMemoryPool&) = delete;
    OrderMemoryPool& operator=(const OrderMemoryPool&) = delete;
    OrderMemoryPool(OrderMemoryPool&&) = delete;
    OrderMemoryPool& operator=(OrderMemoryPool&&) = delete;

    // Allocate an Order from the preallocated pool (lock-free Treiber stack).
    // Uses placement new to construct the object in-place.
    // If the pool is exhausted, falls back to dynamic heap allocation.
    template <typename... Args>
    [[nodiscard]] inline Order* allocate(Args&&... args) noexcept {
        uint64_t current = head_.load(std::memory_order_acquire);
        while (true) {
            uint32_t idx = unpackIndex(current);
            if (idx == EMPTY_INDEX) [[unlikely]] {
                // Pool exhausted - fallback to dynamic heap allocation
                fallback_allocations_.fetch_add(1, std::memory_order_relaxed);
                return new (std::nothrow) Order(std::forward<Args>(args)...);
            }

            uint32_t next_idx = next_index_[idx];
            uint32_t tag = unpackTag(current);
            uint64_t next_head = packHead(tag + 1, next_idx);

            if (head_.compare_exchange_weak(current, next_head,
                                           std::memory_order_acq_rel,
                                           std::memory_order_acquire)) {
                allocated_count_.fetch_add(1, std::memory_order_relaxed);
                Order* order = &pool_[idx];
                return new (order) Order(std::forward<Args>(args)...);
            }
        }
    }

    // Deallocate an Order.
    // If the Order originated from the pool, its destructor is called and its
    // slot is recycled back into the lock-free free stack.
    // If it was allocated from the heap fallback, standard delete is called.
    inline void deallocate(Order* order) noexcept {
        if (!order) [[unlikely]] {
            return;
        }

        if (isFromPool(order)) [[likely]] {
            order->~Order();
            auto idx = static_cast<uint32_t>(order - pool_);

            uint64_t current = head_.load(std::memory_order_acquire);
            while (true) {
                uint32_t top_idx = unpackIndex(current);
                uint32_t tag = unpackTag(current);
                next_index_[idx] = top_idx;
                uint64_t next_head = packHead(tag + 1, idx);

                if (head_.compare_exchange_weak(current, next_head,
                                               std::memory_order_release,
                                               std::memory_order_acquire)) {
                    allocated_count_.fetch_sub(1, std::memory_order_relaxed);
                    return;
                }
            }
        } else [[unlikely]] {
            delete order;
        }
    }

    // Check if an Order pointer belongs to the preallocated pool storage
    [[nodiscard]] inline bool isFromPool(const Order* order) const noexcept {
        if (!order || !pool_) return false;
        const auto ptr_val = reinterpret_cast<uintptr_t>(order);
        const auto start_val = reinterpret_cast<uintptr_t>(pool_);
        const auto end_val = start_val + sizeof(Order) * PoolSize;
        return (ptr_val >= start_val) && (ptr_val < end_val) &&
               (((ptr_val - start_val) % sizeof(Order)) == 0);
    }

    // Reset pool state and recycle all slots (useful for testing and benchmarking)
    void reset() noexcept {
        for (size_t i = 0; i < PoolSize; ++i) {
            next_index_[i] = static_cast<uint32_t>(i + 1);
        }
        if constexpr (PoolSize > 0) {
            next_index_[PoolSize - 1] = EMPTY_INDEX;
        }
        head_.store(packHead(0, 0), std::memory_order_release);
        allocated_count_.store(0, std::memory_order_release);
        fallback_allocations_.store(0, std::memory_order_release);
    }

    [[nodiscard]] constexpr size_t capacity() const noexcept {
        return PoolSize;
    }

    [[nodiscard]] size_t allocatedCount() const noexcept {
        return allocated_count_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] size_t availableCount() const noexcept {
        const size_t alloc = allocatedCount();
        return (alloc >= PoolSize) ? 0 : (PoolSize - alloc);
    }

    [[nodiscard]] size_t getFallbackCount() const noexcept {
        return fallback_allocations_.load(std::memory_order_relaxed);
    }

private:
    static constexpr uint32_t EMPTY_INDEX = 0xFFFFFFFFU;

    [[nodiscard]] static constexpr uint64_t packHead(uint32_t tag, uint32_t idx) noexcept {
        return (static_cast<uint64_t>(tag) << 32) | static_cast<uint64_t>(idx);
    }

    [[nodiscard]] static constexpr uint32_t unpackTag(uint64_t head) noexcept {
        return static_cast<uint32_t>(head >> 32);
    }

    [[nodiscard]] static constexpr uint32_t unpackIndex(uint64_t head) noexcept {
        return static_cast<uint32_t>(head & 0xFFFFFFFFULL);
    }

    Order* pool_{nullptr};
    std::unique_ptr<uint32_t[]> next_index_{nullptr};
    bool is_numa_allocated_{false};

    alignas(64) std::atomic<uint64_t> head_{0};
    alignas(64) std::atomic<size_t> allocated_count_{0};
    alignas(64) std::atomic<size_t> fallback_allocations_{0};
};

// Global pool instance (used in main engine and demonstrations)
inline OrderMemoryPool<> g_order_pool;
