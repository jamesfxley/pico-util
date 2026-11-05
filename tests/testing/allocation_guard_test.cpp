// SPDX-License-Identifier: MIT
// Guard tests without replaced operators (MR-6).
// Replacing new here makes the analyser report a leak in GoogleTest's
// death-test matchers, outside code this library owns.
#include <gtest/gtest.h>

#include "pico_util/testing/allocation_counter.hpp"

using pico_util::testing::AllocationGuard;

namespace {

// A nested guard would leave the outer scope uncounted and its check passing
// vacuously, which this harness must not allow (MR-6).
TEST(AllocationGuardTest, NestingIsRefused) {
    // The unconditional abort is tested in Release too. Matching its text
    // excludes unrelated crashes and sanitiser reports.
    EXPECT_DEATH(
        {
            const AllocationGuard outer;
            const AllocationGuard inner;
        },
        "AllocationGuard does not nest");
}

// After a guard disarms, another may arm.
// Only the armed flag is observable without replaced operators. The count is
// always zero here. CountingIsScopedToTheGuard checks re-zeroing with
// allocations (MR-6).
TEST(AllocationGuardTest, AGuardDisarmsSoALaterGuardMayArm) {
    ASSERT_FALSE(pico_util::testing::counting_armed());
    {
        const AllocationGuard first;
        EXPECT_TRUE(pico_util::testing::counting_armed());
    }
    EXPECT_FALSE(pico_util::testing::counting_armed());

    const AllocationGuard second;
    EXPECT_TRUE(pico_util::testing::counting_armed());
}

}  // namespace
