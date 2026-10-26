// SPDX-License-Identifier: MIT
// Wrap arithmetic (BR-5's foundation). Tested directly rather than only through
// RingBuffer, so that a caller using it for a ring the container does not serve
// has the contract pinned.
#include "pico_util/containers/ring_index.hpp"

#include <gtest/gtest.h>

#include <concepts>
#include <cstddef>

using pico_util::ring_index;
using pico_util::ring_run;
using pico_util::RingCount;
using pico_util::RingPosition;

namespace {

// BR-11: advancing wraps at the capacity.
TEST(RingIndexTest, AdvancesAndWraps) {
    static_assert(ring_index<4>(RingPosition{0}, RingCount{1}).index == 1);
    static_assert(ring_index<4>(RingPosition{3}, RingCount{1}).index == 0);
    static_assert(ring_index<4>(RingPosition{2}, RingCount{2}).index == 0);
    static_assert(ring_index<4>(RingPosition{2}, RingCount{3}).index == 1);
    // A capacity of one wraps on every step.
    static_assert(ring_index<1>(RingPosition{0}, RingCount{1}).index == 0);
    // Advancing by zero is the identity, including at the last position.
    static_assert(ring_index<4>(RingPosition{3}, RingCount{0}).index == 3);
    SUCCEED();
}

// BR-11. The top of the offset's range, which is where one subtraction stops
// being enough and the documented bound starts earning its keep. RingBuffer
// subscripts slots_[] with nothing but this function's return value in between,
// so a range pinned only from below leaves that subscript unguarded in the
// direction that runs off the array.
TEST(RingIndexTest, AdvancingByTheWholeCapacityReturnsTheSamePosition) {
    static_assert(ring_index<4>(RingPosition{0}, RingCount{4}).index == 0);
    static_assert(ring_index<4>(RingPosition{3}, RingCount{4}).index == 3);
    static_assert(ring_index<1>(RingPosition{0}, RingCount{1}).index == 0);
    // The largest legal pair: last position, whole capacity.
    static_assert(ring_index<8>(RingPosition{7}, RingCount{8}).index == 7);
    SUCCEED();
}

// BR-10. One past each bound is refused rather than answered. A single
// subtraction would return 4 for a four-slot ring -- in range for nothing, and
// a subscript off the end of RingBuffer's storage.
TEST(RingIndexTest, AnArgumentPastItsBoundIsRefused) {
#ifdef NDEBUG
    GTEST_SKIP() << "the bounds are asserts, which NDEBUG removes";
#else
    EXPECT_DEATH(
        {
            const RingPosition advanced =
                ring_index<4>(RingPosition{3}, RingCount{5});
            static_cast<void>(advanced.index);
        },
        "offset.elements <= Capacity");
    EXPECT_DEATH(
        {
            const RingPosition advanced =
                ring_index<4>(RingPosition{4}, RingCount{0});
            static_cast<void>(advanced.index);
        },
        "from.index < Capacity");
#endif
}

// BR-9: the run is the smaller of the length and the distance to the end.
TEST(RingIndexTest, ReportsTheContiguousRun) {
    // Fits before the wrap.
    static_assert(ring_run<8>(RingPosition{0}, RingCount{3}).elements == 3);
    // Straddles it: only the part before the end is contiguous.
    static_assert(ring_run<8>(RingPosition{6}, RingCount{4}).elements == 2);
    // Ends exactly at the wrap.
    static_assert(ring_run<8>(RingPosition{4}, RingCount{4}).elements == 4);
    // Nothing wanted, nothing available.
    static_assert(ring_run<8>(RingPosition{4}, RingCount{0}).elements == 0);
    // The last position: one element before the wrap, whatever is asked for.
    static_assert(ring_run<8>(RingPosition{7}, RingCount{8}).elements == 1);
    SUCCEED();
}

// BR-10. ring_run's own bound, in the direction its comment names: `Capacity -
// start` wraps for a start past the end, the clamp then never fires, and the
// caller sizes a transport write from a length that runs off the buffer.
TEST(RingIndexTest, ARunFromAStartPastTheEndIsRefused) {
#ifdef NDEBUG
    GTEST_SKIP() << "the bound is an assert, which NDEBUG removes";
#else
    EXPECT_DEATH(
        {
            const RingCount run = ring_run<8>(RingPosition{8}, RingCount{1});
            static_cast<void>(run.elements);
        },
        "start.index < Capacity");
#endif
}

template <std::size_t Capacity>
constexpr bool BRACED_SIZES_ACCEPTED =
    requires(std::size_t first, std::size_t second) {
        pico_util::ring_index<Capacity>({first}, {second});
    };

// BR-8. The reason the parameters are typed at all: a position and a count are
// both element counts of the same width, so with two bare std::size_t
// parameters a transposed call compiled and returned a plausible wrong answer.
// These assertions fail to compile if either type ever gains a conversion.
TEST(RingIndexTest, APositionAndACountDoNotConvert) {
    static_assert(!std::convertible_to<RingCount, RingPosition>);
    static_assert(!std::convertible_to<RingPosition, RingCount>);
    static_assert(!std::convertible_to<std::size_t, RingPosition>);
    static_assert(!std::convertible_to<std::size_t, RingCount>);
    static_assert(!std::convertible_to<RingPosition, std::size_t>);
    static_assert(!std::convertible_to<RingCount, std::size_t>);
    // A braced argument names no type, so it would build either one from a
    // bare std::size_t: the transposition the types exist to refuse.
    static_assert(!BRACED_SIZES_ACCEPTED<8>);
    SUCCEED();
}

}  // namespace
