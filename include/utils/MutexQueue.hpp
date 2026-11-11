#pragma once

#include <mutex>
#include <cstddef>
#include <memory>
#include <type_traits>

// Thread-safe, bounded, mutex-protected FIFO queue.
// Designed for concurrency evaluation against the lock-free SPSCQueue.
// Provides an identical non-blocking retry-based push/pop interface
// without condition variables or kernel sleep waits.
template<typename T, size_t Size>
class MutexQueue {
    static_assert(Size > 1 && (Size & (Size - 1)) == 0,
                  "Size must be a power of 2 greater than 1");
    static_assert(std::is_trivially_copyable_v<T>,
                  "T must be trivially copyable for queue operations");

public:
    MutexQueue()
        : buffer_(std::make_unique<T[]>(Size)),
          head_(0),
          tail_(0),
          count_(0) {}

    // Producer: Push an item (thread-safe, protected by mutex).
    // Returns false if queue is full.
    [[nodiscard]] inline bool push(const T& item) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if (count_ >= capacity()) [[unlikely]] {
            return false; // Queue full
        }
        buffer_[tail_] = item;
        tail_ = (tail_ + 1) & (Size - 1);
        ++count_;
        return true;
    }

    [[nodiscard]] inline bool try_push(const T& item) noexcept {
        return push(item);
    }

    // Consumer: Pop an item (thread-safe, protected by mutex).
    // Returns false if queue is empty.
    [[nodiscard]] inline bool pop(T& item) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if (count_ == 0) [[unlikely]] {
            return false; // Queue empty
        }
        item = buffer_[head_];
        head_ = (head_ + 1) & (Size - 1);
        --count_;
        return true;
    }

    [[nodiscard]] inline bool try_pop(T& item) noexcept {
        return pop(item);
    }

    // Check if queue is empty
    [[nodiscard]] inline bool empty() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_ == 0;
    }

    // Check if queue is full
    [[nodiscard]] inline bool full() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_ >= capacity();
    }

    // Get current item count
    [[nodiscard]] inline size_t size() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_;
    }

    // Maximum capacity (matches SPSCQueue's Size - 1 for exact equivalence)
    [[nodiscard]] static constexpr size_t capacity() noexcept {
        return Size - 1;
    }

private:
    mutable std::mutex mutex_;
    std::unique_ptr<T[]> buffer_;
    size_t head_;
    size_t tail_;
    size_t count_;
};
