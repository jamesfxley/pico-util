// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "pico_util/containers/ring_index.hpp"

/// \file ring_buffer.hpp
/// Fixed-capacity FIFO with explicit synchronisation (BR-5, BR-6).

namespace pico_util {

/// What a RingBuffer needs of its `Counter` parameter.
template <typename C>
concept RingCounter = requires(C counter) {
    // A second, const-qualified parameter would be read as a constant by
    // readability-identifier-naming, which would then require upper case.
    { std::as_const(counter).load() } -> std::same_as<std::size_t>;
    counter.add();
    counter.sub();
    counter.reset();
};

/// Element count for a ring one context touches. No synchronisation.
class SingleContextCounter {
   public:
    /// \return the number of elements held.
    [[nodiscard]] std::size_t load() const noexcept { return count_; }

    /// Record an element added.
    void add() noexcept { ++count_; }

    /// Record an element removed.
    void sub() noexcept { --count_; }

    /// Return to empty.
    void reset() noexcept { count_ = 0; }

   private:
    std::size_t count_ = 0;
};

/// Element count shared by one producer and one consumer.
/// Only the producer calls add() and only the consumer calls sub(). Two
/// producers would lose increments. Release stores publish slot writes and
/// completed reads to the other side's acquire loads (CR-9).
class SpscCounter {
   public:
    /// \return the element count, acquiring slot writes and completed reads.
    [[nodiscard]] std::size_t load() const noexcept {
        // Reading consumed_ first keeps the later produced_ reading from
        // lagging it and making the difference underflow. A stale consumed_ can
        // make the producer report full early. A stale produced_ can make the
        // consumer report empty early.
        const std::uint32_t consumed =
            consumed_.load(std::memory_order_acquire);
        const std::uint32_t produced =
            produced_.load(std::memory_order_acquire);
        return static_cast<std::size_t>(produced - consumed);
    }

    /// Record an element added, publishing the slot written before this call.
    /// Producer only.
    void add() noexcept {
        produced_.store(produced_.load(std::memory_order_relaxed) + 1U,
                        std::memory_order_release);
    }

    /// Record an element removed, publishing the slot as free only once its
    /// contents have been read. Consumer only.
    void sub() noexcept {
        consumed_.store(consumed_.load(std::memory_order_relaxed) + 1U,
                        std::memory_order_release);
    }

    /// Return to empty. This is unsafe against a concurrent producer or
    /// consumer.
    void reset() noexcept {
        produced_.store(0U, std::memory_order_release);
        consumed_.store(0U, std::memory_order_release);
    }

   private:
    // Each count is one aligned word on both targets and wraps the same way.
    // Family rings hold fewer than 2^32 elements.
    std::atomic<std::uint32_t> produced_ = 0;
    std::atomic<std::uint32_t> consumed_ = 0;
};

/// A FIFO of at most `N` elements with inline storage (BR-6, BR-5).
template <typename T, std::size_t N, RingCounter Counter>
class RingBuffer {
    static_assert(N > 0, "a RingBuffer needs at least one slot");

   public:
    /// Elements this ring holds when full.
    static constexpr std::size_t CAPACITY = N;

    /// \return the number of elements held.
    [[nodiscard]] std::size_t size() const noexcept { return count_.load(); }

    /// \return whether the ring holds nothing.
    [[nodiscard]] bool empty() const noexcept { return count_.load() == 0; }

    /// Whether a push would fail, including after an over-commit.
    [[nodiscard]] bool full() const noexcept { return count_.load() >= N; }

    /// \return `N`.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return N; }

    /// Append from the producer. False leaves a full ring unchanged (BR-7).
    [[nodiscard]] bool push(const T& value) {
        if (full()) {
            return false;
        }
        slots_[head_.index] = value;
        advance_head();
        return true;
    }

    /// \return the next free slot to fill in place, or nullptr if full (BR-12).
    /// Only the producer calls this. Any producer-side call invalidates it.
    [[nodiscard]] T* acquire() noexcept {
        return full() ? nullptr : &slots_[head_.index];
    }

    /// Publish the preceding acquire() slot from the producer.
    /// Without a successful acquire(), this publishes unfilled data. Pushes
    /// still fail at capacity because full() tests >= N.
    void commit() noexcept { advance_head(); }

    /// \return the oldest element or nullptr. pop() invalidates the pointer.
    /// Only the consumer calls this.
    [[nodiscard]] T* front() noexcept {
        return empty() ? nullptr : &slots_[tail_.index];
    }

    /// \return the oldest element or nullptr for the consumer to read.
    [[nodiscard]] const T* front() const noexcept {
        return empty() ? nullptr : &slots_[tail_.index];
    }

    /// Drop the oldest element. Consumer context. A no-op when empty.
    void pop() noexcept {
        if (empty()) {
            return;
        }
        tail_ = ring_index<N>(tail_, RingCount{1});
        count_.sub();
    }

    /// Discard every element, writing both cursors and the count.
    /// Call only while neither producer nor consumer is running.
    void clear() noexcept {
        head_ = RingPosition{};
        tail_ = RingPosition{};
        count_.reset();
    }

   private:
    void advance_head() noexcept {
        head_ = ring_index<N>(head_, RingCount{1});
        count_.add();
    }

    std::array<T, N> slots_{};
    // Only the producer writes head_ and only the consumer writes tail_.
    // Both touch count_.
    RingPosition head_;
    RingPosition tail_;
    Counter count_;
};

}  // namespace pico_util
