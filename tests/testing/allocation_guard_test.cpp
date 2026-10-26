// SPDX-License-Identifier: MIT
// AllocationGuard's own contract (MR-6), in a translation unit that does *not*
// include allocation_operators.ipp.
//
// Deliberately separate from allocation_test.cpp. The guard's arm/disarm rules
// are properties of the guard, not of the replaced allocation functions, and
// the static analyzer follows a replaced `operator new` into GoogleTest's
// matcher machinery -- so a death test hosted alongside the operators reports a
// leak inside gtest that neither this library nor that test owns. Testing the
// guard where nothing is replaced is the fix; suppressing the analyzer there
// would have been a silencing.
#include <gtest/gtest.h>

#include "pico_util/testing/allocation_counter.hpp"

using pico_util::testing::AllocationGuard;

namespace {

// MR-6: the guard arms and disarms by scope, and there is one counter and one
// flag behind it. A second guard inside a live one would zero the outer tally
// and disarm the outer scope on its own destruction, leaving the rest of that
// scope uncounted and its EXPECT_EQ(count(), 0) vacuously true -- a green test
// asserting nothing, which is the one direction this harness must not fail in.
// Nesting is refused rather than given an accounting rule.
TEST(AllocationGuardTest, NestingIsRefused) {
    // Runs in every build type: the refusal is unconditional, so the Release
    // leg -- the one a consumer's test build most often is -- is proven too.
    //
    // Matched on the refusal's own text. An empty matcher passes on *any*
    // death, so a null dereference or a sanitizer report anywhere in the
    // construction path would report this test green -- including the case
    // where the nesting refusal has been removed and the crash comes from
    // something else entirely.
    EXPECT_DEATH(
        {
            const AllocationGuard outer;
            const AllocationGuard inner;
        },
        "AllocationGuard does not nest");
}

// One guard after another is the supported shape: a guard disarms on
// destruction, so a later one may arm.
//
// Asserted on the armed flag, not on count(). The operators are not linked into
// this translation unit, so allocation_count() is zero for the life of the
// process and an assertion on it holds whatever the guard does -- including
// with the constructor's re-zeroing deleted outright. The flag is the guard's
// own state and is observable here; the count is not.
//
// The re-zeroing half of MR-6 needs a counted allocation standing between the
// two guards, so it is established where the operators are linked:
// allocation_test.cpp -- CountingIsScopedToTheGuard.
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
