#pragma once

#include <atomic>
#include <array>
#include <cstddef>
#include <new>
#include <type_traits>

// Lock-free single-producer / single-consumer ring. Capacity must be a power of two;
// usable slots = Capacity - 1. One writer thread calls tryPush, one reader thread
// calls tryPop. Element type must be trivially copyable for the slot storage to
// stay trivial.

namespace hftrec {

template <typename T, std::size_t Capacity>
class SpscRing {
    static_assert(Capacity >= 2, "Capacity must be >= 2");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of two");
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");

  public:
    static constexpr std::size_t kCapacity = Capacity;
    static constexpr std::size_t kMask     = Capacity - 1;

    SpscRing() = default;

    bool tryPush(const T& value) noexcept {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto next = (head + 1) & kMask;
        if (next == tail_.load(std::memory_order_acquire)) {
            return false;  // full
        }
        slots_[head] = value;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool tryPop(T& out) noexcept {
        const auto tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;  // empty
        }
        out = slots_[tail];
        tail_.store((tail + 1) & kMask, std::memory_order_release);
        return true;
    }

    // Approximate occupancy; producer / consumer may race. Good enough for metrics.
    std::size_t approxSize() const noexcept {
        const auto h = head_.load(std::memory_order_acquire);
        const auto t = tail_.load(std::memory_order_acquire);
        return (h - t) & kMask;
    }

  private:
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
    alignas(64) std::array<T, Capacity> slots_{};
};

}  // namespace hftrec
