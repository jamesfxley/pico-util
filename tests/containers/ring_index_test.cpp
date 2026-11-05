// SPDX-License-Identifier: MIT
// Direct arithmetic tests cover rings beyond RingBuffer (BR-5).
#include "pico_util/containers/ring_index.hpp"

#include <gtest/gtest.h>

#include <concepts>
#include <cstddef>

using pico_util::ring_index;
using pico_util::ring_run;
using pico_util::RingCount;
using pico_util::RingPosition;

namespace {

// Advancing wraps at the capacity (BR-11).
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

// The maximum legal offset still wraps to a valid subscript (BR-11).
TEST(RingIndexTest, AdvancingByTheWholeCapacityReturnsTheSamePosition) {
    static_assert(ring_index<4>(RingPosition{0}, RingCount{4}).index == 0);
    static_assert(ring_index<4>(RingPosition{3}, RingCount{4}).index == 3);
    static_assert(ring_index<1>(RingPosition{0}, RingCount{1}).index == 0);
    // The largest legal pair: last position, whole capacity.
    static_assert(ring_index<8>(RingPosition{7}, RingCount{8}).index == 7);
    SUCCEED();
}

// An out-of-bounds argument asserts instead of yielding an invalid subscript
// (BR-10).
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

// The run is the smaller of the length and the distance to the end (BR-9).
TEST(RingIndexTest, ReportsTheContiguousRun) {
    // Fits before the wrap.
    static_assert(ring_run<8>(RingPosition{0}, RingCount{3}).elements == 3);
    // It straddles the wrap, and only the part before the end is contiguous.
    static_assert(ring_run<8>(RingPosition{6}, RingCount{4}).elements == 2);
    // Ends exactly at the wrap.
    static_assert(ring_run<8>(RingPosition{4}, RingCount{4}).elements == 4);
    // Nothing wanted, nothing available.
    static_assert(ring_run<8>(RingPosition{4}, RingCount{0}).elements == 0);
    // The last position: one element before the wrap, whatever is asked for.
    static_assert(ring_run<8>(RingPosition{7}, RingCount{8}).elements == 1);
    SUCCEED();
}

// An invalid start could wrap the subtraction and bypass the clamp (BR-10).
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

// Distinct types reject transposed calls and implicit conversions (BR-8).
TEST(RingIndexTest, APositionAndACountDoNotConvert) {
    static_assert(!std::convertible_to<RingCount, RingPosition>);
    static_assert(!std::convertible_to<RingPosition, RingCount>);
    static_assert(!std::convertible_to<std::size_t, RingPosition>);
    static_assert(!std::convertible_to<std::size_t, RingCount>);
    static_assert(!std::convertible_to<RingPosition, std::size_t>);
    static_assert(!std::convertible_to<RingCount, std::size_t>);
    // A braced argument names no type, so it would build either type from a
    // bare std::size_t. That is the transposition the types exist to prevent.
    static_assert(!BRACED_SIZES_ACCEPTED<8>);
    SUCCEED();
}

}  // namespace
