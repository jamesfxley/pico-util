// SPDX-License-Identifier: MIT
// HeapWatermark must report steady-state growth (MR-1). These tests check
// arming (MR-8) and clamping, and skip the growth cases where the heap cannot
// be read (MR-2).
#include "pico_util/heap/heap_watermark.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#if defined(__GLIBC__)
#include <features.h>
#endif
#include <memory>
#include <vector>

using pico_util::HeapWatermark;

// On glibc 2.33 or later the header takes the mallinfo2 branch. Without this
// check the suite would also pass on the fallback, which reads 0 and skips the
// growth cases (MR-2).
#if defined(__GLIBC__) &&                                           \
    (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33)) && \
    !defined(PICO_UTIL_HAVE_MALLINFO2)
#error \
    "glibc 2.33 or later provides mallinfo2(), and the header did not take it"
#endif

namespace {

// A fresh block below glibc's default 128 KiB mmap threshold stays visible
// to uordblks. A free-list remnant could hide growth.
constexpr std::size_t ALLOCATION_BYTES = std::size_t{64} * 1024;

// Nothing is reported before a baseline exists (MR-4).
TEST(HeapWatermarkTest, ReportsNoGrowthBeforeArming) {
    const HeapWatermark watermark;
    EXPECT_FALSE(watermark.armed());
    const std::vector<char> block(ALLOCATION_BYTES, 'x');
    EXPECT_EQ(watermark.growth_bytes(), 0U);
}

// A live block distinguishes a readable heap from the zero reading under a
// sanitiser (MR-8).
// Checking the reading prevents a broken mallinfo branch from skipping growth.
TEST(HeapWatermarkTest, MeasuringAgreesWithTheReading) {
    const auto block =
        std::make_unique<std::vector<char>>(ALLOCATION_BYTES, 'x');
    if (HeapWatermark::measuring()) {
        EXPECT_NE(HeapWatermark::in_use_bytes(), 0U);
    } else {
        EXPECT_EQ(HeapWatermark::in_use_bytes(), 0U);
    }
}

TEST(HeapWatermarkTest, ArmingLatchesTheBaseline) {
    HeapWatermark watermark;
    watermark.arm();
    EXPECT_TRUE(watermark.armed());
    EXPECT_EQ(watermark.growth_bytes(), 0U);
}

TEST(HeapWatermarkTest, ReportsGrowthHeldAfterArming) {
    HeapWatermark watermark;
    watermark.arm();

    auto block = std::make_unique<std::vector<char>>(ALLOCATION_BYTES, 'x');
    if (!HeapWatermark::measuring()) {
        GTEST_SKIP() << "the heap cannot be read here; growth is always 0";
    }
    EXPECT_GE(watermark.growth_bytes(), ALLOCATION_BYTES);

    // Framework allocations can keep the reading above zero after release.
    block.reset();
    EXPECT_LT(watermark.growth_bytes(), ALLOCATION_BYTES);
}

// Below the baseline the reading clamps and does not wrap, which an unsigned
// subtraction would do silently (MR-4).
TEST(HeapWatermarkTest, NetShrinkClampsToZeroRatherThanUnderflowing) {
    auto block = std::make_unique<std::vector<char>>(ALLOCATION_BYTES, 'x');

    HeapWatermark watermark;
    watermark.arm();  // baseline captured while the block is held
    block.reset();    // now below the baseline

    EXPECT_EQ(watermark.growth_bytes(), 0U);
}

// A watermark that has never been armed reports nothing, and arming a second
// time sets a new baseline and does not accumulate against the first (MR-3).
TEST(HeapWatermarkTest, ReArmingRebaselines) {
    HeapWatermark watermark;
    watermark.arm();

    auto block = std::make_unique<std::vector<char>>(ALLOCATION_BYTES, 'x');
    if (!HeapWatermark::measuring()) {
        GTEST_SKIP() << "the heap cannot be read here; growth is always 0";
    }
    EXPECT_GE(watermark.growth_bytes(), ALLOCATION_BYTES);

    watermark.arm();
    EXPECT_EQ(watermark.growth_bytes(), 0U);
    block.reset();
}

}  // namespace
