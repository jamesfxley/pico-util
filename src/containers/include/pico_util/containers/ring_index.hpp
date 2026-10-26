// SPDX-License-Identifier: MIT
#pragma once

#include <cassert>
#include <cstddef>

/// \file ring_index.hpp
/// Wrap arithmetic for a circular buffer, on its own.
///
/// BR-8 (a position and a count do not interconvert), BR-9 (what `ring_run`
/// reports) and BR-10 (both assert their preconditions in a build with
/// assertions enabled; NDEBUG removes the check, and an out-of-range argument
/// is then the caller's undefined behaviour). Exported: pico-http's transmit
/// ring is a consumer, so these
/// are a contract rather than an internal detail.
///
/// `RingBuffer` uses these, and so would a ring whose element is a byte and
/// whose operations are bulk span copies rather than whole-element push and pop
/// — pico-http's transmit ring is one, with three regions and two cursors, and
/// it does not fit the container. Sharing the arithmetic rather than forcing it
/// onto the container is what keeps the container from growing an interface
/// only one caller uses.
///
/// Compare-and-reset rather than a modulo or a power-of-two mask: a division is
/// expensive on a target without one, and a mask would make "a power of two" a
/// property of the container rather than of whatever sizing knob chose the
/// capacity, where it belongs.
///
/// The three quantities here are a position, a count and a capacity, and all
/// three are element counts of the same type. Written as three adjacent
/// `std::size_t` parameters, transposing any two compiled and returned a
/// plausible wrong answer (CXX-04.7). Each is now distinguished by the only
/// mechanism that makes the mistake unspellable rather than merely asserted:
/// the capacity is a template parameter, because every caller has it as a
/// compile-time constant, and the other two are distinct types.

namespace pico_util {

/// A position in a ring: the index of one element, always below the capacity.
///
/// A type with an explicit constructor rather than a bare `std::size_t` alias
/// or an aggregate: an alias converts silently, and an aggregate is built from
/// a braced `{n}` argument that names no type, so either would leave the
/// transposition it exists to prevent still spellable (BR-8).
struct RingPosition {
    /// The position at index 0.
    constexpr RingPosition() noexcept = default;

    /// \param at the element index, in `[0, Capacity)`.
    constexpr explicit RingPosition(std::size_t at) noexcept : index(at) {}

    /// The element index, in `[0, Capacity)`.
    std::size_t index = 0;
};

/// A number of ring elements — a distance to advance by, or a length wanted.
///
/// Explicitly constructed, for the reason RingPosition is (BR-8).
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
    // Checked, not merely documented (CXX-04.5). One subtraction is enough
    // only within these bounds; outside them this returns an index that is out
    // of range for the ring it was asked about, and a silent wrong answer is
    // exactly what a documented-only precondition buys.
    assert(from.index < Capacity);
    assert(offset.elements <= Capacity);
    const std::size_t advanced = from.index + offset.elements;
    return RingPosition{advanced >= Capacity ? advanced - Capacity : advanced};
}

/// Length of the contiguous run before a ring wraps.
///
/// What a caller handing a pointer and a length to a transport needs: the run
/// it may pass in one call, with the remainder following after the wrap.
///
/// \tparam Capacity ring size in elements.
/// \param start position the run begins at; must be below `Capacity`.
/// \param length elements wanted, which may straddle the wrap.
/// \return the elements available contiguously from `start`, at most `length`.
template <std::size_t Capacity>
[[nodiscard]] constexpr RingCount ring_run(RingPosition start,
                                           RingCount length) {
    static_assert(Capacity > 0, "a ring needs at least one element");
    // As ring_index. `Capacity - start` wraps for a start past the end, the
    // clamp below then never fires, and the caller sizes a transport write
    // from a length that runs off the buffer.
    assert(start.index < Capacity);
    const std::size_t to_end = Capacity - start.index;
    return RingCount{length.elements < to_end ? length.elements : to_end};
}

}  // namespace pico_util
