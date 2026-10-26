// SPDX-License-Identifier: MIT
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>

/// \file allocation_counter.hpp
/// The counting half of the allocation harness (MR-5, MR-6, MR-9).
///
/// A library claiming "this path allocates nothing" is making an assertion
/// until something counts. `HeapWatermark` answers the coarse version of that
/// question on a running image; this answers the exact one in a test, by
/// replacing the global allocation functions and tallying calls.
///
/// Safe to include anywhere, in any number of translation units. The operator
/// definitions live in `allocation_operators.ipp`, which is **not**: it goes in
/// exactly one translation unit per executable. The replaceable allocation
/// functions cannot be `inline`, so a second include is a duplicate-symbol link
/// error. Never add it to the umbrella header or either cross probe. The `.ipp`
/// extension is load-bearing rather than cosmetic — see that file.
///
/// Ships rather than staying under `tests/` because its consumers are the test
/// trees of downstream libraries that need to measure their no-allocation
/// claims.

namespace pico_util::testing {

/// Allocations counted since the active guard armed.
/// Relaxed atomics count events only; callers synchronize worker lifetimes.
///
/// A function-local static rather than a namespace variable: a replaced
/// `operator new` takes no context argument, so the state has to be reachable
/// from a free function, and this shape needs no suppression to exist.
///
/// \return the live counter, by reference.
inline std::atomic<std::size_t>& allocation_count() noexcept {
    static std::atomic<std::size_t> count{0};
    return count;
}

/// Whether allocations are currently being counted.
///
/// \return the live flag, by reference.
inline std::atomic<bool>& counting_armed() noexcept {
    static std::atomic<bool> armed{false};
    return armed;
}

/// The allocating body every replaced `operator new` is defined in terms of.
///
/// \param size bytes requested. A zero-byte request allocates one byte, so that
///        every successful allocation has a distinct address.
/// \return the allocation, or nullptr.
inline void* counted_allocate(std::size_t size) {
    if (counting_armed().load(std::memory_order_relaxed)) {
        allocation_count().fetch_add(1, std::memory_order_relaxed);
    }
    // This IS operator new: malloc is the only thing underneath it, and the
    // rule against raw allocation has nothing left to redirect it to.
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
    return std::malloc(size == 0 ? 1 : size);
}

/// The allocating body every over-aligned `operator new` is defined in terms
/// of.
///
/// \param size bytes requested.
/// \param alignment required alignment.
/// \return the allocation, or nullptr.
inline void* counted_allocate_aligned(std::size_t size,
                                      std::align_val_t alignment) {
    if (counting_armed().load(std::memory_order_relaxed)) {
        allocation_count().fetch_add(1, std::memory_order_relaxed);
    }
    const auto bytes = static_cast<std::size_t>(alignment);
    // Rounding a size within one alignment of SIZE_MAX up would wrap to a small
    // one; no allocator can satisfy it, so it is refused like any other.
    if (size > SIZE_MAX - bytes + 1U) {
        return nullptr;
    }
    // aligned_alloc requires a size that is a multiple of the alignment.
    const std::size_t rounded =
        ((size == 0 ? 1 : size) + bytes - 1) / bytes * bytes;
    // Same as counted_allocate: this is the over-aligned operator new, and
    // aligned_alloc is what it is defined in terms of. Named from the global
    // namespace: newlib's <cstdlib> declares it there but not in std.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return ::aligned_alloc(bytes, rounded);
}

/// The freeing body every replaced `operator delete` is defined in terms of.
///
/// \param memory the allocation to release; may be nullptr.
// Kept out of line: inlined into a replaced operator delete, an optimised GCC
// build pairs this free with the caller's operator new and refuses the
// mismatch under -Werror=mismatched-new-delete, though the replaced new is the
// matching malloc. The pairing is correct; only its inlining misleads.
[[gnu::noinline]] inline void counted_free(void* memory) noexcept {
    // This IS operator delete; free is its whole implementation.
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
    std::free(memory);
}

/// Counts allocations for the duration of a scope (MR-6).
///
/// Zeroes and arms the counter on construction, disarms it on destruction.
/// Scoping matters: a test framework allocates freely while reporting, so a
/// counter armed for the whole run would measure GoogleTest rather than the
/// code under test.
///
/// **One guard at a time.** There is a single counter and a single armed flag,
/// so a second guard inside a live one would zero the outer tally and, on its
/// own destruction, disarm the outer scope -- leaving the rest of that scope
/// uncounted and its assertion vacuously true. A tool whose whole job is to
/// make a no-allocation claim falsifiable must not fail in the direction of a
/// green test, so nesting is refused rather than given an accounting rule.
class AllocationGuard {
   public:
    /// Zero the counter and begin counting.
    AllocationGuard() noexcept {
        // Unconditional, not an assert. NDEBUG would compile an assert out of
        // exactly the build a consumer's Release test leg runs, and there a
        // nested guard is the vacuous green MR-6 exists to refuse. The
        // argument for an assert -- not charging a hot path for a check that
        // cannot fail -- has no purchase in a harness that only runs in tests.
        if (counting_armed().load(std::memory_order_relaxed)) {
            // The write's outcome is irrelevant: abort follows either way, and
            // there is nothing to report a failed report to.
            static_cast<void>(std::fputs(
                "AllocationGuard does not nest: a second guard would leave the "
                "enclosing scope uncounted and its assertion vacuously true\n",
                stderr));
            std::abort();
        }
        allocation_count().store(0, std::memory_order_relaxed);
        counting_armed().store(true, std::memory_order_relaxed);
    }

    /// Stop counting. The count remains readable until the next guard arms.
    ~AllocationGuard() {
        counting_armed().store(false, std::memory_order_relaxed);
    }

    AllocationGuard(const AllocationGuard&) = delete;
    AllocationGuard& operator=(const AllocationGuard&) = delete;
    AllocationGuard(AllocationGuard&&) = delete;
    AllocationGuard& operator=(AllocationGuard&&) = delete;

    /// \return allocations counted since this guard armed.
    [[nodiscard]] static std::size_t count() noexcept {
        return allocation_count().load(std::memory_order_relaxed);
    }
};

}  // namespace pico_util::testing
