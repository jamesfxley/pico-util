// SPDX-License-Identifier: MIT
// RingBuffer<T, N, Counter> (BR-5). The FIFO-across-a-wrap case is what a ring
// exists for and is where an off-by-one hides, so it runs 2N+1 elements rather
// than filling once. The SPSC leg runs two real threads, which establishes FIFO
// order under real concurrency and nothing about memory ordering: the host is
// x86, whose own ordering is strong enough that a missing acquire or release
// passes. CR-9's ordering is held by review, which its row says.
#include "pico_util/containers/ring_buffer.hpp"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

using pico_util::RingBuffer;
using pico_util::SingleContextCounter;
using pico_util::SpscCounter;

namespace {

using Ring = RingBuffer<int, 4, SingleContextCounter>;

TEST(RingBufferTest, PreservesFifoAcrossAWrap) {
    Ring ring;
    EXPECT_TRUE(ring.empty());
    EXPECT_EQ(ring.capacity(), 4U);

    // 2N + 1 elements through a ring of N, so every slot is reused twice and a
    // cursor that failed to wrap shows up as an out-of-order read.
    for (int value = 0; value < 9; ++value) {
        ASSERT_TRUE(ring.push(value));
        const auto* head = ring.front();
        ASSERT_NE(head, nullptr);
        EXPECT_EQ(*head, value);
        ring.pop();
    }
    EXPECT_TRUE(ring.empty());

    // And with the ring held part-full across the wrap, rather than drained
    // between every push.
    for (int value = 0; value < 3; ++value) {
        ASSERT_TRUE(ring.push(value));
    }
    for (int value = 3; value < 9; ++value) {
        const auto* head = ring.front();
        ASSERT_NE(head, nullptr);
        EXPECT_EQ(*head, value - 3);
        ring.pop();
        ASSERT_TRUE(ring.push(value));
    }
}

TEST(RingBufferTest, PushAtCapacityIsRefused) {
    Ring ring;
    for (int value = 0; value < 4; ++value) {
        ASSERT_TRUE(ring.push(value));
    }
    EXPECT_TRUE(ring.full());
    EXPECT_EQ(ring.size(), 4U);

    // Refused, not overwritten: the oldest element is still the oldest.
    EXPECT_FALSE(ring.push(99));
    EXPECT_EQ(ring.size(), 4U);
    const auto* head = ring.front();
    ASSERT_NE(head, nullptr);
    EXPECT_EQ(*head, 0);
    EXPECT_EQ(ring.acquire(), nullptr);
}

// BR-12: a slot is filled in place and committed.
TEST(RingBufferTest, AcquireAndCommitFillInPlace) {
    RingBuffer<std::array<std::uint8_t, 4>, 2, SingleContextCounter> ring;
    auto* slot = ring.acquire();
    ASSERT_NE(slot, nullptr);
    // Nothing is visible to the consumer until commit().
    EXPECT_TRUE(ring.empty());
    EXPECT_EQ(ring.front(), nullptr);

    (*slot)[0] = 0xAB;
    ring.commit();
    EXPECT_EQ(ring.size(), 1U);
    const auto* head = ring.front();
    ASSERT_NE(head, nullptr);
    EXPECT_EQ((*head)[0], 0xAB);
}

TEST(RingBufferTest, EmptyRingHasNoFrontAndPopIsANoOp) {
    Ring ring;
    EXPECT_EQ(ring.front(), nullptr);
    ring.pop();  // must not underflow the count
    EXPECT_TRUE(ring.empty());
    EXPECT_EQ(ring.size(), 0U);
    EXPECT_TRUE(ring.push(1));
    EXPECT_EQ(ring.size(), 1U);
}

TEST(RingBufferTest, ClearDiscardsEverything) {
    Ring ring;
    EXPECT_TRUE(ring.push(1));
    EXPECT_TRUE(ring.push(2));
    ring.clear();
    EXPECT_TRUE(ring.empty());
    EXPECT_EQ(ring.front(), nullptr);
    // Usable again from a clean cursor.
    EXPECT_TRUE(ring.push(7));
    const auto* head = ring.front();
    ASSERT_NE(head, nullptr);
    EXPECT_EQ(*head, 7);
}

// A ring of one is the degenerate case both cursors share.
TEST(RingBufferTest, CapacityOfOne) {
    RingBuffer<int, 1, SingleContextCounter> ring;
    EXPECT_TRUE(ring.push(1));
    EXPECT_TRUE(ring.full());
    EXPECT_FALSE(ring.push(2));
    const auto* head = ring.front();
    ASSERT_NE(head, nullptr);
    EXPECT_EQ(*head, 1);
    ring.pop();
    EXPECT_TRUE(ring.empty());
    EXPECT_TRUE(ring.push(2));
    const auto* next = ring.front();
    ASSERT_NE(next, nullptr);
    EXPECT_EQ(*next, 2);
}

// BR-5 under real concurrency: a producer and a consumer in separate threads.
// Every value the consumer sees must be one the producer sent, in order, with
// no gap and no duplicate.
TEST(RingBufferTest, SpscRingCarriesEveryValueInOrder) {
    constexpr int TOTAL = 20000;
    RingBuffer<int, 8, SpscCounter> ring;
    std::vector<int> received;
    received.reserve(TOTAL);

    std::thread producer([&ring] {
        for (int value = 0; value < TOTAL; ++value) {
            while (!ring.push(value)) {
                std::this_thread::yield();
            }
        }
    });

    while (static_cast<int>(received.size()) < TOTAL) {
        const int* head = ring.front();
        if (head == nullptr) {
            std::this_thread::yield();
            continue;
        }
        received.push_back(*head);
        ring.pop();
    }
    producer.join();

    ASSERT_EQ(received.size(), static_cast<std::size_t>(TOTAL));
    for (int value = 0; value < TOTAL; ++value) {
        ASSERT_EQ(received[static_cast<std::size_t>(value)], value)
            << "gap or reorder at " << value;
    }
    EXPECT_TRUE(ring.empty());
}

// The counter is a policy, not a default: naming it is what makes the
// assumption reviewable at the declaration site.
TEST(RingBufferTest, BothCounterPoliciesInstantiate) {
    // BR-6: slots are a member, so each ring is at least its own storage. The
    // element is wide on purpose: with an int, a ring holding a pointer to heap
    // storage is already large enough on LP64 to pass the floor.
    using Wide = std::array<char, 64>;
    static_assert(sizeof(RingBuffer<Wide, 4, SingleContextCounter>) >=
                  std::size_t{4} * 64U);
    static_assert(sizeof(RingBuffer<Wide, 4, SpscCounter>) >=
                  std::size_t{4} * 64U);
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                  "the SPSC counter is only lock-free if its atomic is");
    SUCCEED();
}

// BR-5: a commit() with no successful acquire() is a caller bug the container
// cannot detect, but it must not convert a refusing ring into an evicting one.
// full() tests `>= N`, so the count running past capacity costs the one slot
// that bug published and nothing after it.
TEST(RingBufferTest, AnOverCommittedRingStillRefuses) {
    Ring ring;
    for (int value = 0; value < 4; ++value) {
        ASSERT_TRUE(ring.push(value));
    }
    ASSERT_TRUE(ring.full());
    ASSERT_EQ(ring.acquire(), nullptr);

    // The caller bug: publish a slot acquire() refused to hand out.
    ring.commit();

    EXPECT_TRUE(ring.full());
    EXPECT_FALSE(ring.push(99));
    const auto* head = ring.front();
    ASSERT_NE(head, nullptr);
    EXPECT_EQ(*head, 0);
}
}  // namespace
