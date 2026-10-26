// SPDX-License-Identifier: MIT
// HeapWatermark is the falsifiable half of MR-1 ("zero post-init heap"): it
// cannot see an alloc/free pair that nets to zero, but it must report steady-
// state growth. These tests pin the arming contract (MR-8) and the clamping,
// which every consumer of this library relies on, and skip where MR-2's
// fallback reports 0.
#include "pico_util/heap/heap_watermark.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#if defined(__GLIBC__)
#include <features.h>
#endif
#include <memory>
#include <vector>

using pico_util::HeapWatermark;

// MR-2: on glibc 2.33 or later the header takes the mallinfo2() arm. Without
// this the suite would pass the same way on the arm that reads nothing, which
// reports 0 and skips the growth cases.
#if defined(__GLIBC__) &&                                           \
    (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33)) && \
    !defined(PICO_UTIL_HAVE_MALLINFO2)
#error \
    "glibc 2.33 or later provides mallinfo2(), and the header did not take it"
#endif

namespace {

// Large enough that the allocator satisfies it with a fresh block rather than
// out of an existing free-list remnant, but below glibc's default 128 KiB
// mmap threshold: uordblks counts the main arena only, so an mmap-backed
// allocation is invisible to this proxy on the host.
constexpr std::size_t ALLOCATION_BYTES = std::size_t{64} * 1024;

// MR-4: nothing is reported before a baseline exists.
TEST(HeapWatermarkTest, ReportsNoGrowthBeforeArming) {
    const HeapWatermark watermark;
    EXPECT_FALSE(watermark.armed());
    const std::vector<char> block(ALLOCATION_BYTES, 'x');
    EXPECT_EQ(watermark.growth_bytes(), 0U);
}

// MR-8: measuring() is checked against the reading itself. With a live block
// held, a heap that can be read reads nonzero, and one that cannot -- this
// build's AddressSanitizer leg -- reads 0. CI runs both, so a build whose
// mallinfo arm is broken fails here rather than skipping the growth cases.
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

    // Releasing it drops the reported growth again, which is what a
    // steady-state path that allocates and frees within one request looks
    // like. The reading does not have to return to exactly zero: anything else
    // running in this process, the test framework included, also allocates.
    block.reset();
    EXPECT_LT(watermark.growth_bytes(), ALLOCATION_BYTES);
}

// MR-4: below the baseline the reading clamps rather than wrapping, which an
// unsigned subtraction would do silently.
TEST(HeapWatermarkTest, NetShrinkClampsToZeroRatherThanUnderflowing) {
    auto block = std::make_unique<std::vector<char>>(ALLOCATION_BYTES, 'x');

    HeapWatermark watermark;
    watermark.arm();  // baseline captured while the block is held
    block.reset();    // now below the baseline

    EXPECT_EQ(watermark.growth_bytes(), 0U);
}

// MR-3: a watermark that has never been armed reports nothing, and arming a
// second time re-baselines rather than accumulating against the first.
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
