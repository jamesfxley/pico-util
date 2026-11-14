// SPDX-License-Identifier: MIT
#pragma once

#include <cassert>
#include <cstddef>

/// \file ring_index.hpp
/// Public ring arithmetic with distinct position and count types (BR-8).
/// ring_run() bounds a contiguous run (BR-9). Preconditions are asserted,
/// with violations undefined under NDEBUG (BR-10).

namespace pico_util {

/// An element index below capacity, distinct from a count (BR-8).
struct RingPosition {
    /// The position at index 0.
    constexpr RingPosition() noexcept = default;

    /// \param at the element index, in `[0, Capacity)`.
    constexpr explicit RingPosition(std::size_t at) noexcept : index(at) {}

    /// The element index, in `[0, Capacity)`.
    std::size_t index = 0;
};

/// An element distance or length, distinct from a position (BR-8).
struct RingCount {
    /// No elements.
    constexpr RingCount() noexcept = default;

    /// \param count how many elements.
    constexpr explicit RingCount(std::size_t count) noexcept
        : elements(count) {}

    /// How many elements.
    std::size_t elements = 0;
};

/// Advance a position around a ring.
///
/// \tparam Capacity ring size in elements.
/// \param from current position; must be below `Capacity`.
/// \param offset how far to advance; must not exceed `Capacity`.
/// \return the position `offset` past `from`, wrapped at `Capacity` (BR-11).
template <std::size_t Capacity>
[[nodiscard]] constexpr RingPosition ring_index(RingPosition from,
                                                RingCount offset) {
    static_assert(Capacity > 0, "a ring needs at least one element");
    // One subtraction wraps correctly only within these bounds.
    assert(from.index < Capacity);
    assert(offset.elements <= Capacity);
    const std::size_t advanced = from.index + offset.elements;
    return RingPosition{advanced >= Capacity ? advanced - Capacity : advanced};
}

/// Length of the contiguous run before a ring wraps.
///
/// \tparam Capacity ring size in elements.
/// \param start position the run begins at; must be below `Capacity`.
/// \param length elements wanted. The range can straddle the wrap.
/// \return the elements available contiguously from `start`, at most `length`.
template <std::size_t Capacity>
[[nodiscard]] constexpr RingCount ring_run(RingPosition start,
                                           RingCount length) {
    static_assert(Capacity > 0, "a ring needs at least one element");
    // A start past the end wraps the subtraction and bypasses the clamp.
    assert(start.index < Capacity);
    const std::size_t to_end = Capacity - start.index;
    return RingCount{length.elements < to_end ? length.elements : to_end};
}

}  // namespace pico_util
