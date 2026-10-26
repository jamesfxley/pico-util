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
/// A fixed-capacity FIFO ring, with the synchronisation it assumes as a policy
/// (BR-5, BR-6).
///
/// The Counter policy selects plain or single-producer/single-consumer
/// synchronization. Counting distinguishes full from empty without a reserved
/// slot, and wrap arithmetic does not require a power-of-two capacity.
///
/// The counter is what makes that work. `head_` is touched only by the producer
/// and `tail_` only by the consumer, so the counter is the one object both
/// contexts touch — which makes it the only thing that has to carry the
/// publication, and leaves the rest of the container ordinary.
///
/// Counting uses every slot and permits arbitrary capacity; a sizing policy
/// may impose its own constraints without putting them in the container.

namespace pico_util {

/// What a RingBuffer needs of its `Counter` parameter.
template <typename C>
concept RingCounter = requires(C counter) {
    // as_const rather than a second, const-qualified parameter: a `const C`
    // parameter reads as a constant to readability-identifier-naming, which
    // then wants it spelled in upper case.
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

/// Element count for a ring with one producer and one consumer in different
/// contexts — an interrupt handler and the main loop, or the two cores.
///
/// Two counts, each written by one side only: the producer advances `produced_`
/// and the consumer advances `consumed_`, and the element count is their
/// difference. A single shared count needs a read-modify-write from both sides,
/// and ARMv6-M has no exclusive-access instructions to do one with: on RP2040 a
/// `fetch_add` compiles to a call into the SDK's atomic runtime, which takes a
/// spinlock with interrupts disabled, so an interrupt-context producer would
/// take a lock on every push. With one writer per count, each side advances its
/// own with a plain load and store, which both architectures do natively.
///
/// That rests on the contract the name states: `add()` from the producer only,
/// `sub()` from the consumer only. Two producers would lose increments.
///
/// CR-9. The release on each store publishes what that side did to the slot --
/// the producer's write, the consumer's read -- and the acquire in `load()` is
/// what the other side observes it through. Without it a consumer can see the
/// count of an element before the bytes it refers to, or a producer can reuse a
/// slot the consumer is still reading.
class SpscCounter {
   public:
    /// \return the number of elements held, acquiring the elements themselves.
    ///
    /// `consumed_` is read first. Each count only grows and the consumed one
    /// never passes the produced one, so a produced count read afterwards
    /// cannot be behind it, and the difference cannot wrap below zero for any
    /// observer. For the two parties themselves the result is conservative in
    /// the safe direction: the producer may see a stale consumed count and
    /// refuse a push that would have fitted, the consumer a stale produced
    /// count and find the ring empty a moment early.
    [[nodiscard]] std::size_t load() const noexcept {
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

    /// Return to empty. Not safe against a concurrent producer or consumer;
    /// see RingBuffer::clear().
    void reset() noexcept {
        produced_.store(0U, std::memory_order_release);
        consumed_.store(0U, std::memory_order_release);
    }

   private:
    // 32 bits on every target, so a count is one aligned word on both
    // architectures and the difference wraps the same way everywhere. A ring
    // holding 2^32 elements is not a configuration this family has.
    std::atomic<std::uint32_t> produced_ = 0;
    std::atomic<std::uint32_t> consumed_ = 0;
};

/// A fixed-capacity FIFO holding at most `N` elements of type `T`.
///
/// Refuses at capacity rather than evicting: the oldest element is never
/// overwritten by a push, so a caller that must not lose data can retry, and
/// one that may drop it does so knowingly. Storage is a `std::array` member, so
/// the container allocates nothing (BR-6).
///
/// `Counter` has no default deliberately (BR-5). Both policies compile for any
/// `T`, and a single-context ring used across two contexts passes every
/// single-threaded host test it is given — the defect appears only on the
/// target and only under preemption. Naming the policy at the declaration is
/// what makes the assumption reviewable.
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

    /// \return whether a further push would be refused.
    ///
    /// `>=` rather than `==`: an over-committed ring (see commit()) would
    /// otherwise never test full again, and every later push would overwrite a
    /// live element -- turning the refusing container this is into the
    /// silently-evicting one it exists not to be.
    [[nodiscard]] bool full() const noexcept { return count_.load() >= N; }

    /// \return `N`.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return N; }

    /// Append `value`. Producer context.
    ///
    /// \param value copied into the next free slot.
    /// \return false when the ring is full, leaving it unchanged. Never
    ///         overwrites the oldest element. `[[nodiscard]]` because a
    ///         dropped refusal is a silently dropped element (BR-7,
    ///         CXX-04.3).
    [[nodiscard]] bool push(const T& value) {
        if (full()) {
            return false;
        }
        slots_[head_.index] = value;
        advance_head();
        return true;
    }

    /// The next free slot, to fill in place. Producer context.
    ///
    /// Pairs with commit(): the slot is not visible to the consumer until then.
    /// Filling in place is what lets a caller copy a payload straight into the
    /// ring rather than through a temporary.
    ///
    /// \return the slot, or nullptr when the ring is full. Invalidated by the
    ///         matching commit(), and by any other producer-side call.
    ///
    /// BR-12: fill in place, so no whole-element temporary sits on the
    /// producer's stack.
    [[nodiscard]] T* acquire() noexcept {
        return full() ? nullptr : &slots_[head_.index];
    }

    /// Publish the slot returned by the preceding acquire(). Producer context.
    ///
    /// Calling this without a successful acquire() publishes a slot that was
    /// never filled, which is a caller bug rather than a state this container
    /// can detect. The damage stops at that slot: full() tests `>= N`, so the
    /// ring keeps refusing rather than beginning to overwrite live elements.
    void commit() noexcept { advance_head(); }

    /// The oldest element. Consumer context.
    ///
    /// \return the element, or nullptr when the ring is empty. Invalidated by
    ///         the matching pop().
    [[nodiscard]] T* front() noexcept {
        return empty() ? nullptr : &slots_[tail_.index];
    }

    /// The oldest element, for reading. Consumer context.
    ///
    /// \return the element, or nullptr when the ring is empty.
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

    /// Discard every element.
    ///
    /// Single-context use only: it touches both cursors and the count, so under
    /// `SpscCounter` it races a concurrent producer or consumer. Reset a shared
    /// ring while neither side is running, not while both are.
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
    // head_ is written only by the producer and tail_ only by the consumer, so
    // neither is shared. count_ is the one value both touch, which is why the
    // policy sits on it and nowhere else.
    RingPosition head_;
    RingPosition tail_;
    Counter count_;
};

}  // namespace pico_util
