// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "pico_util/result/result.hpp"

/// \file published.hpp
/// Publishing one value across two contexts (CR-1..CR-10).
///
/// A value wider than the machine word cannot be stored or loaded atomically on
/// a 32-bit target, so a reader can see half of one value spliced onto half of
/// another. The strategy depends on which side can preempt which.
///
/// - An interrupt timestamp latch is written by the interrupt
///   handler and read from mainline. The reader cannot preempt the
///   writer, so a reader that spins until the writer finishes always
///   terminates: the handler runs to completion. A seqlock is correct.
/// - A clock offset is written from mainline and read from the
///   interrupt handler. Here the reader *can* preempt its own writer,
///   and a spinning reader would wait forever for a writer that cannot
///   resume. A slot ring, whose reader validates and retries a bounded
///   number of times, is correct.
///
/// Pick the wrong one and the result is a hang in an interrupt handler, or a
/// torn read. Neither shows up in a single-threaded host test, and both
/// strategies compile for any `T`. So there is no default (CR-1): the primary
/// template is declared and not defined, and naming the strategy is what forces
/// the question to be answered where the answer is known.
///
/// Both carry the value as 32-bit atomic words (CR-5). This keeps publication
/// lock-free: a `std::atomic<std::int64_t>` would call
/// `__atomic_load_8`, which the Pico SDK implements as a global spinlock with
/// interrupts disabled — the opposite of what either strategy is for.

namespace pico_util {

namespace detail {

/// Number of 32-bit words a `T` occupies when published.
template <typename T>
inline constexpr std::size_t PUBLISHED_WORD_COUNT =
    (sizeof(T) + sizeof(std::uint32_t) - 1) / sizeof(std::uint32_t);

/// Copy `value` into `words`, one relaxed store per word.
///
/// Relaxed because the ordering is the strategy's job — a seqlock's fences or a
/// slot ring's release on its counter — and paying for it per word as well
/// would be ordering the same thing twice.
///
/// \param words destination, whose length covers `sizeof(T)`.
/// \param value the value to publish.
template <typename T, std::size_t N>
void write_words(std::array<std::atomic<std::uint32_t>, N>& words,
                 const T& value) noexcept {
    std::array<std::uint32_t, N> raw{};
    std::memcpy(raw.data(), &value, sizeof(T));
    for (std::size_t i = 0; i < N; ++i) {
        words[i].store(raw[i], std::memory_order_relaxed);
    }
}

/// Reassemble a `T` from `words`, one relaxed load per word.
///
/// \param words source, whose length covers `sizeof(T)`.
/// \return the reassembled value. Whether it is a value that was ever published
///         whole is the caller's protocol to establish, not this function's.
template <typename T, std::size_t N>
[[nodiscard]] T read_words(
    const std::array<std::atomic<std::uint32_t>, N>& words) noexcept {
    std::array<std::uint32_t, N> raw{};
    for (std::size_t i = 0; i < N; ++i) {
        raw[i] = words[i].load(std::memory_order_relaxed);
    }
    T value{};
    // Through void*: T is trivially copyable (check_publishable asserts it), so
    // copying bytes into it is defined, but GCC's -Wclass-memaccess fires for
    // any T that is not also trivial -- one with a default member initialiser,
    // say -- and under -Werror that refused exactly the T CR-8 is about. The
    // cast is how GCC documents stating that the copy is intended.
    std::memcpy(static_cast<void*>(&value), raw.data(), sizeof(T));
    return value;
}

/// Whether a slot-ring read that saw the store counter at `before`, then
/// read a slot, then saw it at `after`, read words the writer had not begun
/// overwriting (CR-10).
///
/// The writer fills the slot *after* the one it last published, so it takes
/// `slots - 1` stores to reach the slot a reader chose at `before`; fewer than
/// that leaves those words the single value published at `before`, whatever
/// has been published since. Unsigned, so the counter's wrap costs nothing.
/// A function of its own so the arithmetic can be pinned at its boundaries by
/// a test that cannot depend on thread timing.
///
/// \param before the counter the reader chose its slot by.
/// \param after the counter re-read once the words were read.
/// \param slots the ring's slot count.
/// \return whether the words read are a whole published value.
[[nodiscard]] constexpr bool slot_read_validates(std::uint32_t before,
                                                 std::uint32_t after,
                                                 std::size_t slots) noexcept {
    // The difference is taken in 32 bits, where the counter wraps, and only
    // then widened for the comparison.
    const std::uint32_t advanced = after - before;
    return advanced < slots - 1U;
}

/// What both strategies require of the published type.
///
/// CR-7: `T` is trivially copyable, asserted at class scope in both
/// specialisations rather than on the store path.
///
/// Neither strategy performs a read-modify-write: every access is a plain
/// atomic load or store. That is what decides which assertion is the right
/// one, because the two supported cores answer `is_always_lock_free`
/// differently and only one of them answers it usefully.
template <typename T>
constexpr void check_publishable() noexcept {
    static_assert(std::is_trivially_copyable_v<T>,
                  "a published value is copied through memcpy");
    // No alignment constraint: the value is assembled in a properly aligned
    // local and memcpy'd, so the word array's own alignment never reaches it.
#if defined(__ARM_ARCH_6M__)
    // Cortex-M0+ (RP2040). ARMv6-M has no exclusive-access instructions, so
    // is_always_lock_free is false for every type -- but it reports on
    // compare_exchange too, and neither strategy performs one. GCC compiles a
    // 32-bit atomic load to `ldr` + `dmb` and a store to `dmb` + `str`: no
    // libcall, no lock, which is the whole of what CR-5 asks for. Asserting
    // is_always_lock_free here would refuse a target the design is correct on.
    // What is asserted instead is the property whose absence would break it --
    // an atomic carrying a lock alongside its value.
    static_assert(sizeof(std::atomic<std::uint32_t>) == sizeof(std::uint32_t),
                  "a 32-bit atomic must be no wider than the value it holds");
#else
    // Cortex-M33 (RP2350) and the host, where the guarantee is available
    // outright.
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                  "publication is only lock-free if a 32-bit atomic is");
#endif
}

}  // namespace detail

/// Why a slot-ring read could not return a value it validated (CR-3).
///
/// `NONE` is the success state `Result` compares against. `EXHAUSTED` means the
/// reader used all `MAX_READ_ATTEMPTS` without one validating, which happens
/// only when the writer breaks CR-4's contract -- so it is the caller being
/// told of a writer bug rather than an event to retry around.
enum class PublishedReadError : std::uint8_t { NONE = 0, EXHAUSTED };

/// Which publication protocol, and therefore which reader and writer contexts.
///
/// There is no default (CR-1). Both strategies compile for any `T` and both
/// read correctly under a single-threaded host test, so a strategy chosen by
/// omission would be wrong only on the target and only under preemption.
enum class PublishStrategy : std::uint8_t {
    /// The writer cannot be preempted by the reader: an interrupt handler
    /// writes and mainline reads.
    ///
    /// The reader spins until it sees a stable, even sequence number.
    /// That terminates because the writer always runs to completion.
    /// Safe **only** in this direction: a reader that can preempt its
    /// own writer spins forever.
    SEQLOCK_WRITER_NOT_PREEMPTIBLE,

    /// The reader can preempt the writer: mainline writes and an interrupt
    /// handler reads.
    ///
    /// The reader never spins. It validates its read against a store
    /// counter and retries a bounded number of times; if it exhausts them it
    /// reports `PublishedReadError::EXHAUSTED` rather than returning a value
    /// that may be torn. Writer contract: one writer, mainline only, and stores
    /// rare
    /// relative to the handful of instructions a read occupies -- the
    /// contract is what keeps exhaustion unreachable, not the retry count.
    /// An interrupt-context writer breaks it, and is what the seqlock
    /// strategy is for.
    SLOT_RING_READER_MAY_PREEMPT,
};

/// Publishes one value of type `T` across two contexts.
///
/// Declared and not defined: naming a strategy is mandatory. See
/// PublishStrategy for which to name.
template <typename T, PublishStrategy Strategy>
class Published;

/// Seqlock publication, for a writer the reader cannot preempt (CR-2).
///
/// The writer brackets its store with an odd/even counter; the reader retries
/// while the count is odd or has changed, meaning a write was in progress
/// across its read.
template <typename T>
class Published<T, PublishStrategy::SEQLOCK_WRITER_NOT_PREEMPTIBLE> {
   public:
    // At class scope, not only on the store() path: check_publishable<T>() is
    // called from store(), so a reader-only consumer -- a facade holding a
    // reference to a latch something else writes -- reaches read_words()'
    // memcpy into a non-trivially-copyable T with no diagnostic anywhere. A
    // requirement the whole design rests on must not depend on which members a
    // translation unit happens to odr-use (CXX-09.1).
    static_assert(std::is_trivially_copyable_v<T>,
                  "a published value is copied through memcpy");

    /// Publish `value`. Writer context only — the one that cannot be preempted
    /// by a reader.
    ///
    /// \param value the value to publish.
    void store(const T& value) noexcept {
        detail::check_publishable<T>();
        sequence_.store(sequence_.load(std::memory_order_relaxed) + 1,
                        std::memory_order_relaxed);
        // Full thread fences, not signal fences: a signal fence orders the
        // compiler alone, which suffices only while writer and reader share a
        // core. A DMB costs nothing measurable here and means moving the reader
        // to the second core cannot silently reintroduce a torn read.
        std::atomic_thread_fence(std::memory_order_release);
        detail::write_words(words_, value);
        std::atomic_thread_fence(std::memory_order_release);
        sequence_.store(sequence_.load(std::memory_order_relaxed) + 1,
                        std::memory_order_relaxed);
        published_.store(true, std::memory_order_release);
    }

    /// Read the published value. Reader context.
    ///
    /// May spin while a write is in progress. That terminates only because the
    /// writer cannot be preempted by this reader; see PublishStrategy.
    ///
    /// \return the most recent value published whole, and a value-initialised
    ///         `T` before the first store() (CR-8).
    [[nodiscard]] T load() const noexcept {
        for (;;) {
            // CR-8, read first. A store that completes after this read makes
            // the snapshot below its value, and T{} was the published state
            // when the flag was read. Read after the snapshot, a first store
            // landing in between would return the zeroed words as its value.
            const bool stored = published_.load(std::memory_order_acquire);
            const std::uint32_t before =
                sequence_.load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            const T value = detail::read_words<T>(words_);
            std::atomic_thread_fence(std::memory_order_acquire);
            const std::uint32_t after =
                sequence_.load(std::memory_order_relaxed);
            if (before == after && (before & 1U) == 0U) {
                // CR-8. The words start zeroed, and all-bits-zero is not T{}
                // for a T with a default member initialiser, so a read before
                // the first store returns the value-initialised T rather than
                // the bytes it would be assembled from. The flag, not the
                // counter: the counter is zero again after 2^31 stores.
                return stored ? value : T{};
            }
        }
    }

    /// \return whether a value has been published. Never becomes false again
    ///         once true (CR-6).
    [[nodiscard]] bool published() const noexcept {
        return published_.load(std::memory_order_acquire);
    }

   private:
    static constexpr std::size_t WORDS = detail::PUBLISHED_WORD_COUNT<T>;

    std::array<std::atomic<std::uint32_t>, WORDS> words_{};
    /// Odd while store() is writing, even between writes.
    std::atomic<std::uint32_t> sequence_{0};
    std::atomic<bool> published_{false};
};

/// Slot-ring publication, for a reader that can preempt the writer (CR-3).
///
/// The writer fills the next slot and then publishes a store count; the reader
/// takes the count, reads the slot it names, and re-reads the count. A counter
/// rather than an alternating index is what makes the re-read conclusive: an
/// index that alternates can return to the value the reader first saw with a
/// write to that very slot in between, so the check would pass over a torn
/// read. A counter never repeats, so observing it unchanged means no store
/// landed.
///
/// The ring is what makes a validated read safe rather than merely likely. A
/// reader's slot is disturbed only when the writer comes all the way back round
/// to it, which takes SLOT_COUNT stores, and the writer is already inside the
/// next slot one store before it publishes — so a read tolerates
/// SLOT_COUNT - 2 stores landing underneath it. Widening the ring buys
/// tolerance at one slot each.
template <typename T>
class Published<T, PublishStrategy::SLOT_RING_READER_MAY_PREEMPT> {
   public:
    // At class scope, not only on the store() path: check_publishable<T>() is
    // called from store(), so a reader-only consumer -- a facade holding a
    // reference to a latch something else writes -- reaches read_words()'
    // memcpy into a non-trivially-copyable T with no diagnostic anywhere. A
    // requirement the whole design rests on must not depend on which members a
    // translation unit happens to odr-use (CXX-09.1).
    static_assert(std::is_trivially_copyable_v<T>,
                  "a published value is copied through memcpy");

    /// Slots the ring holds. A power of two, so the index is a mask. This
    /// value, MAX_READ_ATTEMPTS and the tolerance load() validates against are
    /// CR-10.
    static constexpr std::size_t SLOT_COUNT = 4;

    /// Read attempts before the reader gives up and reports exhaustion.
    ///
    /// Reaching that point takes SLOT_COUNT - 1 stores inside every one of
    /// these reads, against a writer contract of stores that are rare next to a
    /// read's duration.
    static constexpr int MAX_READ_ATTEMPTS = 3;

    /// Publish `value`. Writer context: one writer, mainline only.
    ///
    /// \param value the value to publish.
    void store(const T& value) noexcept {
        detail::check_publishable<T>();
        const std::uint32_t next = count_.load(std::memory_order_relaxed) + 1;
        // The reader's validation assumes that if it read any word of a slot
        // this store is overwriting, its re-read of the counter sees the store
        // before this one. The counter's release store below orders only what
        // precedes it, and the words are relaxed stores, so nothing gave the
        // reader's acquire fence a release to synchronise with. This fence is
        // that release, for the reason the seqlock writer has one: without it a
        // reader on the other core can take words from a slot being overwritten
        // and still see a counter that validates them (CXX-08.2).
        std::atomic_thread_fence(std::memory_order_release);
        detail::write_words(slots_[next % SLOT_COUNT], value);
        count_.store(next, std::memory_order_release);
        published_.store(true, std::memory_order_release);
    }

    /// Read the published value. Safe from any context; never blocks.
    ///
    /// \return the most recent value the reader could validate, or
    ///         `PublishedReadError::EXHAUSTED` when no attempt validated
    ///         (CR-3).
    ///
    /// Never spins, which is the guarantee that matters in an interrupt
    /// handler: it makes at most MAX_READ_ATTEMPTS attempts and then returns.
    /// Exhaustion is unreachable under the **writer contract** -- stores rare
    /// relative to a read's duration -- and a writer storing in a tight loop
    /// can lap the ring inside every attempt. The value assembled on the last
    /// attempt may then be torn, so it is withheld rather than returned: a
    /// caller that must not use a torn value is told, which a plain `T` return
    /// could not do.
    ///
    /// CR-8: a value-initialised `T`, as a success, before the first store().
    [[nodiscard]] Result<T, PublishedReadError> load() const noexcept {
        for (int attempt = 0; attempt < MAX_READ_ATTEMPTS; ++attempt) {
            // CR-8: nothing published yet. The slots start zeroed, and
            // all-bits-zero is not T{} for a T with a default member
            // initialiser. The flag, not the counter: the counter is zero
            // again after 2^32 stores. published_ is stored after count_, so
            // once it reads true the count below names a written slot.
            if (!published_.load(std::memory_order_acquire)) {
                return T{};
            }
            const std::uint32_t before = count_.load(std::memory_order_acquire);
            const T value = detail::read_words<T>(slots_[before % SLOT_COUNT]);

            // The words must be read before the counter is re-read, and an
            // acquire on that re-read does not achieve it: acquire stops later
            // accesses moving earlier, not earlier loads moving later, which is
            // the direction this needs. An acquire fence is the barrier that
            // holds by the memory model rather than by what a particular
            // architecture happens to emit (CXX-08.2).
            std::atomic_thread_fence(std::memory_order_acquire);

            // Relaxed because the fence above supplies the ordering. Equality
            // would be the wrong test and needlessly strict: the writer has to
            // advance SLOT_COUNT - 1 stores before it reaches this slot.
            const std::uint32_t after = count_.load(std::memory_order_relaxed);
            if (detail::slot_read_validates(before, after, SLOT_COUNT)) {
                return value;
            }
        }
        return PublishedReadError::EXHAUSTED;
    }

    /// \return whether a value has been published. Never becomes false again
    ///         once true (CR-6).
    [[nodiscard]] bool published() const noexcept {
        return published_.load(std::memory_order_acquire);
    }

   private:
    static constexpr std::size_t WORDS = detail::PUBLISHED_WORD_COUNT<T>;

    std::array<std::array<std::atomic<std::uint32_t>, WORDS>, SLOT_COUNT>
        slots_{};
    /// Count of stores, not a slot index: the live slot is count_ % SLOT_COUNT.
    std::atomic<std::uint32_t> count_{0};
    std::atomic<bool> published_{false};
};

}  // namespace pico_util
