// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <new>

#include "pico_util/testing/allocation_counter.hpp"

/// \file allocation_operators.ipp
/// Replacements for all twenty global allocation and deallocation functions
/// (MR-5). Include in exactly one translation unit per executable. Duplicates
/// fail to link. The executable that includes this file should be the one whose
/// allocations are measured.

// These are the standard's replaceable allocation functions. They need external
// linkage and these exact signatures.

/// Allocates bytes or throws std::bad_alloc on failure.
void* operator new(std::size_t size) {
    void* memory = pico_util::testing::counted_allocate(size);
    if (memory == nullptr) {
        throw std::bad_alloc();
    }
    return memory;
}

/// Allocates bytes or throws std::bad_alloc on failure.
void* operator new[](std::size_t size) { return ::operator new(size); }

/// Allocates bytes or returns nullptr on failure.
void* operator new(std::size_t size, const std::nothrow_t& /*tag*/) noexcept {
    return pico_util::testing::counted_allocate(size);
}

/// Allocates bytes or returns nullptr on failure.
void* operator new[](std::size_t size, const std::nothrow_t& /*tag*/) noexcept {
    return pico_util::testing::counted_allocate(size);
}

/// Allocates bytes or throws std::bad_alloc on failure.
void* operator new(std::size_t size, std::align_val_t alignment) {
    void* memory =
        pico_util::testing::counted_allocate_aligned(size, alignment);
    if (memory == nullptr) {
        throw std::bad_alloc();
    }
    return memory;
}

/// Allocates bytes or throws std::bad_alloc on failure.
void* operator new[](std::size_t size, std::align_val_t alignment) {
    return ::operator new(size, alignment);
}

/// Allocates bytes or returns nullptr on failure.
void* operator new(std::size_t size, std::align_val_t alignment,
                   const std::nothrow_t& /*tag*/) noexcept {
    return pico_util::testing::counted_allocate_aligned(size, alignment);
}

/// Allocates bytes or returns nullptr on failure.
void* operator new[](std::size_t size, std::align_val_t alignment,
                     const std::nothrow_t& /*tag*/) noexcept {
    return pico_util::testing::counted_allocate_aligned(size, alignment);
}

/// Releases an allocation. memory may be nullptr.
void operator delete(void* memory) noexcept {
    pico_util::testing::counted_free(memory);
}

/// Releases an allocation. memory may be nullptr.
void operator delete[](void* memory) noexcept {
    pico_util::testing::counted_free(memory);
}

/// Releases an allocation. memory may be nullptr.
void operator delete(void* memory, std::size_t /*size*/) noexcept {
    pico_util::testing::counted_free(memory);
}

/// Releases an allocation. memory may be nullptr.
void operator delete[](void* memory, std::size_t /*size*/) noexcept {
    pico_util::testing::counted_free(memory);
}

/// Releases an allocation. memory may be nullptr.
void operator delete(void* memory, const std::nothrow_t& /*tag*/) noexcept {
    pico_util::testing::counted_free(memory);
}

/// Releases an allocation. memory may be nullptr.
void operator delete[](void* memory, const std::nothrow_t& /*tag*/) noexcept {
    pico_util::testing::counted_free(memory);
}

/// Releases an allocation. memory may be nullptr.
void operator delete(void* memory, std::align_val_t /*alignment*/) noexcept {
    pico_util::testing::counted_free(memory);
}

/// Releases an allocation. memory may be nullptr.
void operator delete[](void* memory, std::align_val_t /*alignment*/) noexcept {
    pico_util::testing::counted_free(memory);
}

/// Releases an allocation. memory may be nullptr.
void operator delete(void* memory, std::size_t /*size*/,
                     std::align_val_t /*alignment*/) noexcept {
    pico_util::testing::counted_free(memory);
}

/// Releases an allocation. memory may be nullptr.
void operator delete[](void* memory, std::size_t /*size*/,
                       std::align_val_t /*alignment*/) noexcept {
    pico_util::testing::counted_free(memory);
}

/// Releases an allocation. memory may be nullptr.
void operator delete(void* memory, std::align_val_t /*alignment*/,
                     const std::nothrow_t& /*tag*/) noexcept {
    pico_util::testing::counted_free(memory);
}

/// Releases an allocation. memory may be nullptr.
void operator delete[](void* memory, std::align_val_t /*alignment*/,
                       const std::nothrow_t& /*tag*/) noexcept {
    pico_util::testing::counted_free(memory);
}
