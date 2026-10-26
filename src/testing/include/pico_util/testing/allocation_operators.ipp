// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <new>

#include "pico_util/testing/allocation_counter.hpp"

/// \file allocation_operators.ipp
/// The global allocation functions, replaced so they can be counted (MR-5).
///
/// **Include this in exactly one translation unit per test executable.**
///
/// The replaceable allocation functions cannot be `inline` — the standard
/// requires exactly one definition of each in a program — so this header is
/// really a translation unit wearing a header's clothes. A second include in
/// the same executable is a duplicate-symbol link error, which is the loud
/// failure this arrangement wants; a quiet one would mean two counters and a
/// test that passed by accident.
///
/// It is therefore deliberately outside the `pico_util.hpp` umbrella and
/// outside the cross-compiled probe: reaching it through an umbrella include
/// would put these definitions into every translation unit that included the
/// umbrella.
///
/// The replacement is **complete** — all twenty forms, every one of them
/// reaching the same `malloc`/`aligned_alloc`/`free`. That is not
/// thoroughness for its own sake. A partial replacement leaves some forms with
/// the implementation's own allocator, so a block from one allocator can reach
/// the other's deallocation function; under a sanitizer that is an
/// `alloc-dealloc-mismatch`, and the usual workaround is to compile the harness
/// out under sanitizers and lose the coverage. Being complete is what makes
/// that unnecessary.
///
/// The replacement is program-wide, so an executable including this should be
/// the one whose allocations are being measured. Hosting it alongside tests
/// that allocate freely makes every one of them depend on the counter's armed
/// state.

// These are the standard's replaceable allocation functions: external linkage
// and these exact signatures are the whole mechanism.

/// Replaces the throwing scalar `operator new`.
///
/// \param size bytes requested.
/// \return the allocation. Throws `std::bad_alloc` rather than returning
///         nullptr, as the standard requires of this form.
void* operator new(std::size_t size) {
    void* memory = pico_util::testing::counted_allocate(size);
    if (memory == nullptr) {
        throw std::bad_alloc();
    }
    return memory;
}

/// Replaces the throwing array `operator new[]`.
///
/// \param size bytes requested.
/// \return the allocation; throws `std::bad_alloc` on failure.
void* operator new[](std::size_t size) { return ::operator new(size); }

/// Replaces the non-throwing scalar `operator new`.
///
/// \param size bytes requested.
/// \return the allocation, or nullptr on failure.
void* operator new(std::size_t size, const std::nothrow_t& /*tag*/) noexcept {
    return pico_util::testing::counted_allocate(size);
}

/// Replaces the non-throwing array `operator new[]`.
///
/// \param size bytes requested.
/// \return the allocation, or nullptr on failure.
void* operator new[](std::size_t size, const std::nothrow_t& /*tag*/) noexcept {
    return pico_util::testing::counted_allocate(size);
}

/// Replaces the throwing over-aligned scalar `operator new`.
///
/// \param size bytes requested.
/// \param alignment required alignment.
/// \return the allocation; throws `std::bad_alloc` on failure.
void* operator new(std::size_t size, std::align_val_t alignment) {
    void* memory =
        pico_util::testing::counted_allocate_aligned(size, alignment);
    if (memory == nullptr) {
        throw std::bad_alloc();
    }
    return memory;
}

/// Replaces the throwing over-aligned array `operator new[]`.
///
/// \param size bytes requested.
/// \param alignment required alignment.
/// \return the allocation; throws `std::bad_alloc` on failure.
void* operator new[](std::size_t size, std::align_val_t alignment) {
    return ::operator new(size, alignment);
}

/// Replaces the non-throwing over-aligned scalar `operator new`.
///
/// \param size bytes requested.
/// \param alignment required alignment.
/// \return the allocation, or nullptr on failure.
void* operator new(std::size_t size, std::align_val_t alignment,
                   const std::nothrow_t& /*tag*/) noexcept {
    return pico_util::testing::counted_allocate_aligned(size, alignment);
}

/// Replaces the non-throwing over-aligned array `operator new[]`.
///
/// \param size bytes requested.
/// \param alignment required alignment.
/// \return the allocation, or nullptr on failure.
void* operator new[](std::size_t size, std::align_val_t alignment,
                     const std::nothrow_t& /*tag*/) noexcept {
    return pico_util::testing::counted_allocate_aligned(size, alignment);
}

/// Replaces the scalar `operator delete`.
///
/// \param memory the allocation to release; may be nullptr.
void operator delete(void* memory) noexcept {
    pico_util::testing::counted_free(memory);
}

/// Replaces the array `operator delete[]`.
///
/// \param memory the allocation to release; may be nullptr.
void operator delete[](void* memory) noexcept {
    pico_util::testing::counted_free(memory);
}

/// Replaces the sized scalar `operator delete`.
///
/// \param memory the allocation to release; may be nullptr.
void operator delete(void* memory, std::size_t /*size*/) noexcept {
    pico_util::testing::counted_free(memory);
}

/// Replaces the sized array `operator delete[]`.
///
/// \param memory the allocation to release; may be nullptr.
void operator delete[](void* memory, std::size_t /*size*/) noexcept {
    pico_util::testing::counted_free(memory);
}

/// Replaces the non-throwing scalar `operator delete`.
///
/// \param memory the allocation to release; may be nullptr.
void operator delete(void* memory, const std::nothrow_t& /*tag*/) noexcept {
    pico_util::testing::counted_free(memory);
}

/// Replaces the non-throwing array `operator delete[]`.
///
/// \param memory the allocation to release; may be nullptr.
void operator delete[](void* memory, const std::nothrow_t& /*tag*/) noexcept {
    pico_util::testing::counted_free(memory);
}

/// Replaces the over-aligned scalar `operator delete`.
///
/// \param memory the allocation to release; may be nullptr.
void operator delete(void* memory, std::align_val_t /*alignment*/) noexcept {
    pico_util::testing::counted_free(memory);
}

/// Replaces the over-aligned array `operator delete[]`.
///
/// \param memory the allocation to release; may be nullptr.
void operator delete[](void* memory, std::align_val_t /*alignment*/) noexcept {
    pico_util::testing::counted_free(memory);
}

/// Replaces the sized over-aligned scalar `operator delete`.
///
/// \param memory the allocation to release; may be nullptr.
void operator delete(void* memory, std::size_t /*size*/,
                     std::align_val_t /*alignment*/) noexcept {
    pico_util::testing::counted_free(memory);
}

/// Replaces the sized over-aligned array `operator delete[]`.
///
/// \param memory the allocation to release; may be nullptr.
void operator delete[](void* memory, std::size_t /*size*/,
                       std::align_val_t /*alignment*/) noexcept {
    pico_util::testing::counted_free(memory);
}

/// Replaces the non-throwing over-aligned scalar `operator delete`.
///
/// \param memory the allocation to release; may be nullptr.
void operator delete(void* memory, std::align_val_t /*alignment*/,
                     const std::nothrow_t& /*tag*/) noexcept {
    pico_util::testing::counted_free(memory);
}

/// Replaces the non-throwing over-aligned array `operator delete[]`.
///
/// \param memory the allocation to release; may be nullptr.
void operator delete[](void* memory, std::align_val_t /*alignment*/,
                       const std::nothrow_t& /*tag*/) noexcept {
    pico_util::testing::counted_free(memory);
}
