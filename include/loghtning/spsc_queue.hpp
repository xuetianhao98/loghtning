#pragma once

#include <atomic>
#include <bit>
#include <cstddef>
#include <memory>
#include <vector>

namespace loghtning::detail {

// Bounded single-producer/single-consumer ring buffer: frontend thread writes,
// backend thread reads. It relies on that ownership model for the light
// atomics.
template <typename T>
class SpscQueue {
 public:
  explicit SpscQueue(std::size_t capacity)
      : capacity_(std::bit_ceil(capacity + 1)), mask_(capacity_ - 1) {
    buffer_.resize(capacity_);
  }

  SpscQueue(SpscQueue const&) = delete;
  auto operator=(SpscQueue const&) -> SpscQueue& = delete;

  [[nodiscard]] auto try_push(T value) noexcept -> bool {
    auto const tail = tail_.load(std::memory_order_relaxed);
    auto const next_tail = increment(tail);

    if (next_tail == head_.load(std::memory_order_acquire)) {
      return false;
    }

    buffer_[tail] = std::move(value);
    tail_.store(next_tail, std::memory_order_release);
    return true;
  }

  [[nodiscard]] auto try_pop(T& value) noexcept -> bool {
    auto const head = head_.load(std::memory_order_relaxed);

    if (head == tail_.load(std::memory_order_acquire)) {
      return false;
    }

    value = std::move(buffer_[head]);
    head_.store(increment(head), std::memory_order_release);
    return true;
  }

  [[nodiscard]] auto empty() const noexcept -> bool {
    return head_.load(std::memory_order_acquire) ==
           tail_.load(std::memory_order_acquire);
  }

  [[nodiscard]] auto usable_capacity() const noexcept -> std::size_t {
    return capacity_ - 1;
  }

 private:
  [[nodiscard]] auto increment(std::size_t value) const noexcept
      -> std::size_t {
    return (value + 1) & mask_;
  }

  // One slot is kept empty so `head == tail` can mean "empty" unambiguously.
  std::size_t capacity_{0};
  std::size_t mask_{0};
  std::vector<T> buffer_;

  // Keep reader and writer cursors on separate cache lines to avoid needless
  // cache-line bouncing between the producer and consumer threads.
  alignas(64) std::atomic<std::size_t> head_{0};
  alignas(64) std::atomic<std::size_t> tail_{0};
};

}  // namespace loghtning::detail
