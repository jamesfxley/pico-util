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
/// Publication across two contexts (CR-1..CR-10) using 32-bit atomic words
/// (CR-5). A seqlock suits a reader that cannot preempt the writer. A slot ring
/// suits one that can. There is no default strategy (CR-1).

namespace pico_util {

namespace detail {

/// Number of 32-bit words a `T` occupies when published.
template <typename T>
inline constexpr std::size_t PUBLISHED_WORD_COUNT =
    (sizeof(T) + sizeof(std::uint32_t) - 1) / sizeof(std::uint32_t);

/// Copy `value` into `words` with relaxed stores. The strategy orders them.
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

/// Reassemble a `T` with relaxed loads. The caller's protocol validates it.
/// \param words source, whose length covers `sizeof(T)`.
template <typename T, std::size_t N>
[[nodiscard]] T read_words(
    const std::array<std::atomic<std::uint32_t>, N>& words) noexcept {
    std::array<std::uint32_t, N> raw{};
    for (std::size_t i = 0; i < N; ++i) {
        raw[i] = words[i].load(std::memory_order_relaxed);
    }
    T value{};
    // The void* cast silences GCC's -Wclass-memaccess for a trivially copyable
    // T that is not trivial, such as one with a default member initialiser
    // (CR-8).
    std::memcpy(static_cast<void*>(&value), raw.data(), sizeof(T));
    return value;
}

/// Whether the writer left the slot chosen at `before` intact (CR-10).
/// `after` is the counter re-read after the words. `slots` is the ring size.
[[nodiscard]] constexpr bool slot_read_validates(std::uint32_t before,
                                                 std::uint32_t after,
                                                 std::size_t slots) noexcept {
    // The difference is taken in 32 bits, where the counter wraps, and is
    // widened only for the comparison.
    const std::uint32_t advanced = after - before;
    return advanced < slots - 1U;
}

/// Requirements for trivial copying (CR-7) and lock-free word access.
template <typename T>
constexpr void check_publishable() noexcept {
    static_assert(std::is_trivially_copyable_v<T>,
                  "a published value is copied through memcpy");
    // There is no alignment constraint, because the value is assembled in an
    // aligned local and copied with memcpy.
#if defined(__ARM_ARCH_6M__)
    // Cortex-M0+ (RP2040). is_always_lock_free covers compare_exchange, which
    // ARMv6-M cannot do natively. Asserting it would reject this target, whose
    // word loads and stores are lock-free: GCC emits ldr then dmb for an
    // acquire load and dmb then str for a release store, with no libcall or
    // lock. The size check excludes a lock beside the value (CR-5).
    static_assert(sizeof(std::atomic<std::uint32_t>) == sizeof(std::uint32_t),
                  "a 32-bit atomic must be no wider than the value it holds");
#else
    // Cortex-M33 (RP2350) and the host, where is_always_lock_free holds.
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                  "publication is only lock-free if a 32-bit atomic is");
#endif
}

}  // namespace detail

/// A slot-ring read succeeds or reports exhaustion (CR-3, CR-4).
enum class PublishedReadError : std::uint8_t { NONE = 0, EXHAUSTED };

/// Reader and writer contexts, with no default protocol (CR-1).
enum class PublishStrategy : std::uint8_t {
    /// An interrupt handler writes and mainline reads. The reader can spin.
    /// A reader that preempts its writer would spin for ever.
    SEQLOCK_WRITER_NOT_PREEMPTIBLE,

    /// Mainline writes. A reader in any context makes bounded read attempts.
    /// The sole writer stores rarely relative to the duration of a read.
    SLOT_RING_READER_MAY_PREEMPT,
};

/// A `T` published across contexts. A strategy must be named.
template <typename T, PublishStrategy Strategy>
class Published;

/// Seqlock publication for a writer the reader cannot preempt (CR-2).
template <typename T>
class Published<T, PublishStrategy::SEQLOCK_WRITER_NOT_PREEMPTIBLE> {
   public:
    // The trivial-copyability check must not depend on which members are
    // odr-used. A reader-only consumer never calls store(), and store() is
    // where check_publishable() runs.
    static_assert(std::is_trivially_copyable_v<T>,
                  "a published value is copied through memcpy");

    /// Publish `value` only from the writer, which its reader cannot preempt.
    void store(const T& value) noexcept {
        detail::check_publishable<T>();
        sequence_.store(sequence_.load(std::memory_order_relaxed) + 1,
                        std::memory_order_relaxed);
        // Thread fences also order a reader on another core. Signal fences
        // order only the compiler and suffice only on the same core.
        std::atomic_thread_fence(std::memory_order_release);
        detail::write_words(words_, value);
        std::atomic_thread_fence(std::memory_order_release);
        sequence_.store(sequence_.load(std::memory_order_relaxed) + 1,
                        std::memory_order_relaxed);
        published_.store(true, std::memory_order_release);
    }

    /// Return the latest whole value, or `T{}` before the first store (CR-8).
    /// Spinning terminates only because this reader cannot preempt the writer.
    [[nodiscard]] T load() const noexcept {
        for (;;) {
            // The flag precedes the snapshot: a first store between a
            // snapshot and a later flag read would return zeroed words (CR-8).
            const bool stored = published_.load(std::memory_order_acquire);
            const std::uint32_t before =
                sequence_.load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            const T value = detail::read_words<T>(words_);
            std::atomic_thread_fence(std::memory_order_acquire);
            const std::uint32_t after =
                sequence_.load(std::memory_order_relaxed);
            if (before == after && (before & 1U) == 0U) {
                // Zeroed words need not equal T{}. The flag distinguishes the
                // initial state even after the counter wraps at 2^31 stores
                // (CR-8).
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

/// Slot-ring publication for a reader that can preempt the writer (CR-3).
template <typename T>
class Published<T, PublishStrategy::SLOT_RING_READER_MAY_PREEMPT> {
   public:
    // The assertion is at class scope for the reason given in the seqlock
    // specialisation.
    static_assert(std::is_trivially_copyable_v<T>,
                  "a published value is copied through memcpy");

    // SLOT_COUNT is a power of two so count % SLOT_COUNT advances to the next
    // slot even across uint32_t wrap.
    /// Number of ring slots (CR-10).
    static constexpr std::size_t SLOT_COUNT = 4;

    /// Read attempts before exhaustion is reported.
    static constexpr int MAX_READ_ATTEMPTS = 3;

    /// Publish `value` from the sole writer, in mainline only.
    void store(const T& value) noexcept {
        detail::check_publishable<T>();
        const std::uint32_t next = count_.load(std::memory_order_relaxed) + 1;
        // This release pairs with the reader's acquire fence: seeing an
        // overwritten word then exposes the previous store's count. The later
        // count release only orders preceding accesses. It gives the reader's
        // fence no release to acquire through the relaxed words. Without this
        // fence another core could validate overwritten words.
        std::atomic_thread_fence(std::memory_order_release);
        detail::write_words(slots_[next % SLOT_COUNT], value);
        count_.store(next, std::memory_order_release);
        published_.store(true, std::memory_order_release);
    }

    /// Return a validated value, or EXHAUSTED after bounded attempts (CR-3).
    /// Before the first store, any reader succeeds with `T{}` (CR-8).
    [[nodiscard]] Result<T, PublishedReadError> load() const noexcept {
        for (int attempt = 0; attempt < MAX_READ_ATTEMPTS; ++attempt) {
            // Zeroed slots need not equal T{}. The flag survives counter wrap
            // at 2^32 stores. It is stored after count_, so true exposes a
            // written slot (CR-8).
            if (!published_.load(std::memory_order_acquire)) {
                return T{};
            }
            const std::uint32_t before = count_.load(std::memory_order_acquire);
            const T value = detail::read_words<T>(slots_[before % SLOT_COUNT]);

            // The words must be read before the counter re-read. An acquire
            // load only stops later accesses moving earlier. This fence stops
            // the earlier word loads moving later.
            std::atomic_thread_fence(std::memory_order_acquire);

            // The fence supplies ordering for this relaxed load. The writer
            // reaches this slot after SLOT_COUNT - 1 stores, so equality is too
            // strict.
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
    /// The completed-store count selects the live slot as count_ % SLOT_COUNT.
    std::atomic<std::uint32_t> count_{0};
    std::atomic<bool> published_{false};
};

}  // namespace pico_util
