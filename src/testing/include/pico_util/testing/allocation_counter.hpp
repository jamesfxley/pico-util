// SPDX-License-Identifier: MIT
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>

/// \file allocation_counter.hpp
/// Allocation counts under a scoped guard (MR-5, MR-6, MR-9).
/// The operator definitions in allocation_operators.ipp belong in one
/// translation unit per executable. Do not add that file to the umbrella or
/// either cross probe.

namespace pico_util::testing {

/// The count of allocations since the active guard armed.
/// Callers synchronise worker lifetimes. Relaxed atomics count events only.
inline std::atomic<std::size_t>& allocation_count() noexcept {
    static std::atomic<std::size_t> count{0};
    return count;
}

/// The live flag that enables counting.
inline std::atomic<bool>& counting_armed() noexcept {
    static std::atomic<bool> armed{false};
    return armed;
}

// A zero-byte request gets one byte so each live allocation has a distinct
// address.
/// Allocates size bytes, or returns nullptr.
inline void* counted_allocate(std::size_t size) {
    if (counting_armed().load(std::memory_order_relaxed)) {
        allocation_count().fetch_add(1, std::memory_order_relaxed);
    }
    // This function implements operator new, and malloc is the only allocator
    // beneath it, so the check against raw allocation has nothing to redirect
    // to.
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
    return std::malloc(size == 0 ? 1 : size);
}

/// Allocates size bytes at alignment, or returns nullptr.
inline void* counted_allocate_aligned(std::size_t size,
                                      std::align_val_t alignment) {
    if (counting_armed().load(std::memory_order_relaxed)) {
        allocation_count().fetch_add(1, std::memory_order_relaxed);
    }
    const auto bytes = static_cast<std::size_t>(alignment);
    // Rounding a size within one alignment of SIZE_MAX would wrap.
    if (size > SIZE_MAX - bytes + 1U) {
        return nullptr;
    }
    // aligned_alloc requires a size that is a multiple of the alignment.
    const std::size_t rounded =
        ((size == 0 ? 1 : size) + bytes - 1) / bytes * bytes;
    // As for counted_allocate, this function implements the over-aligned
    // operator new on top of aligned_alloc. The call uses the global namespace
    // because newlib's <cstdlib> declares aligned_alloc there and not in std.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return ::aligned_alloc(bytes, rounded);
}

/// Releases an allocation. memory may be nullptr.
// Inlining pairs this free with the caller's new in optimised GCC and triggers
// -Werror=mismatched-new-delete even though the replaced new uses malloc.
[[gnu::noinline]] inline void counted_free(void* memory) noexcept {
    // This function implements operator delete, and free is its whole body.
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
    std::free(memory);
}

/// Counts allocations within one scope (MR-6).
/// Nested guards abort. The harness must not give a vacuous pass.
class AllocationGuard {
   public:
    /// Zero the counter and begin counting.
    AllocationGuard() noexcept {
        // An assert would let NDEBUG remove the nested-guard check, leaving
        // the enclosing scope uncounted and its no-allocation check passing
        // (MR-6).
        if (counting_armed().load(std::memory_order_relaxed)) {
            // Abort follows even if the diagnostic write fails.
            static_cast<void>(std::fputs(
                "AllocationGuard does not nest: a second guard would leave the "
                "enclosing scope uncounted and its assertion vacuously true\n",
                stderr));
            std::abort();
        }
        allocation_count().store(0, std::memory_order_relaxed);
        counting_armed().store(true, std::memory_order_relaxed);
    }

    /// Stop counting. The count stays readable until the next guard arms.
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
