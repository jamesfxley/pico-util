// SPDX-License-Identifier: MIT
// A counter back at zero does not mean nothing was stored (CR-2, CR-3, CR-8).
// The seqlock wraps after 2^31 stores and the ring after 2^32. Compiling this
// separate executable at -O2 keeps it practical without changing the state
// tested.
#include <gtest/gtest.h>

#include <cstdint>

#include "pico_util/publish/published.hpp"
#include "pico_util/result/result.hpp"

using pico_util::Published;
using pico_util::PublishStrategy;

TEST(PublishedWrapTest, ReturnsThePublishedValueAfterTheCounterWraps) {
    Published<std::uint32_t, PublishStrategy::SEQLOCK_WRITER_NOT_PREEMPTIBLE>
        seqlock;
    for (std::uint64_t i = 0; i < (std::uint64_t{1} << 31U); ++i) {
        seqlock.store(7U);
    }
    EXPECT_EQ(seqlock.load(), 7U);

    Published<std::uint32_t, PublishStrategy::SLOT_RING_READER_MAY_PREEMPT>
        ring;
    for (std::uint64_t i = 0; i < (std::uint64_t{1} << 32U); ++i) {
        ring.store(7U);
    }
    const auto read = ring.load();
    ASSERT_TRUE(read.ok());
    EXPECT_EQ(read.value_or(0U), 7U);
}
