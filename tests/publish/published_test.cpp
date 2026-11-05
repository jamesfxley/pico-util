// SPDX-License-Identifier: MIT
// Concurrent publication tests detect spliced values (CR-1..CR-10).
// Complementary halves expose splices that plain counters can hide.
#include "pico_util/publish/published.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "pico_util/result/result.hpp"

using pico_util::Published;
using pico_util::PublishStrategy;

namespace {

constexpr auto SEQLOCK = PublishStrategy::SEQLOCK_WRITER_NOT_PREEMPTIBLE;
constexpr auto SLOT_RING = PublishStrategy::SLOT_RING_READER_MAY_PREEMPT;

// The high half is the complement of the low half, so any splice is visible.
constexpr std::uint64_t checked(std::uint32_t n) {
    return (static_cast<std::uint64_t>(~n) << 32U) | n;
}

constexpr bool is_intact(std::uint64_t value) {
    const auto low = static_cast<std::uint32_t>(value);
    const auto high = static_cast<std::uint32_t>(value >> 32U);
    return high == ~low;
}

// A single-threaded read always validates. Exhaustion here is a defect.
template <typename T>
T validated(const pico_util::Result<T, pico_util::PublishedReadError>& read) {
    EXPECT_TRUE(read.ok()) << "the slot-ring read was not validated";
    return read.value_or(T{});
}

// Both strategies instantiate. pico_util_refuses_published_without_strategy
// checks that omitting the strategy fails to compile (CR-1).
TEST(PublishedTest, BothStrategiesInstantiate) {
    static_assert(
        std::is_default_constructible_v<Published<std::uint64_t, SEQLOCK>>);
    static_assert(
        std::is_default_constructible_v<Published<std::uint64_t, SLOT_RING>>);
    SUCCEED();
}

// Nothing is published before the first store (CR-6).
TEST(PublishedTest, ReportsNothingPublishedBeforeTheFirstStore) {
    const Published<std::uint64_t, SEQLOCK> seqlock;
    EXPECT_FALSE(seqlock.published());
    EXPECT_EQ(seqlock.load(), 0U);

    const Published<std::uint64_t, SLOT_RING> ring;
    EXPECT_FALSE(ring.published());
    EXPECT_EQ(validated(ring.load()), 0U);
}

// Before a store, load() returns T{}, not zeroed words (CR-8).
// A default member initialiser makes the two distinguishable.
struct Defaulted {
    std::int32_t value = 5;
};

TEST(PublishedTest, ReturnsTheValueInitialisedTBeforeTheFirstStore) {
    const Published<Defaulted, SEQLOCK> seqlock;
    EXPECT_EQ(seqlock.load().value, 5);

    const Published<Defaulted, SLOT_RING> ring;
    EXPECT_EQ(validated(ring.load()).value, 5);
}

// published() becomes true on a store and stays true (CR-6).
TEST(PublishedTest, RoundTripsAValueThroughBothStrategies) {
    Published<std::uint64_t, SEQLOCK> seqlock;
    seqlock.store(0x0123456789ABCDEF);
    EXPECT_TRUE(seqlock.published());
    EXPECT_EQ(seqlock.load(), 0x0123456789ABCDEFU);
    seqlock.store(1);
    EXPECT_EQ(seqlock.load(), 1U);
    EXPECT_TRUE(seqlock.published());

    Published<std::uint64_t, SLOT_RING> ring;
    ring.store(0x0123456789ABCDEF);
    EXPECT_TRUE(ring.published());
    EXPECT_EQ(validated(ring.load()), 0x0123456789ABCDEFU);

    // Republished values are seen, and published() never goes back to false.
    ring.store(1);
    EXPECT_EQ(validated(ring.load()), 1U);
    EXPECT_TRUE(ring.published());
}

// This payload occupies whole words. The next case covers rounding.
TEST(PublishedTest, CarriesAStructWiderThanAWord) {
    struct Sample {
        std::uint32_t ticks;
        std::uint16_t code;
    };

    static_assert(sizeof(Sample) % sizeof(std::uint32_t) == 0,
                  "this case is the whole-words one; the round-up is below");

    Published<Sample, SEQLOCK> published;
    published.store(Sample{.ticks = 0xDEADBEEF, .code = 0x1234});
    const auto read = published.load();
    EXPECT_EQ(read.ticks, 0xDEADBEEFU);
    EXPECT_EQ(read.code, 0x1234U);
}

// A partial final word needs rounding up to avoid overflowing the word array.
TEST(PublishedTest, CarriesATypeThatIsNotAWholeNumberOfWords) {
    struct Odd {
        std::uint16_t a;
        std::uint16_t b;
        std::uint16_t c;
    };

    static_assert(sizeof(Odd) == 6, "three 16-bit fields, no padding");
    static_assert(pico_util::detail::PUBLISHED_WORD_COUNT<Odd> == 2,
                  "six bytes span two 32-bit words -- a truncating division "
                  "would say one, and the memcpy would run off the end");

    Published<Odd, SEQLOCK> seqlock;
    seqlock.store(Odd{.a = 0x1111, .b = 0x2222, .c = 0x3333});
    const auto from_seqlock = seqlock.load();
    EXPECT_EQ(from_seqlock.a, 0x1111U);
    EXPECT_EQ(from_seqlock.b, 0x2222U);
    EXPECT_EQ(from_seqlock.c, 0x3333U);

    Published<Odd, SLOT_RING> ring;
    ring.store(Odd{.a = 0x4444, .b = 0x5555, .c = 0x6666});
    const auto from_ring = validated(ring.load());
    EXPECT_EQ(from_ring.a, 0x4444U);
    EXPECT_EQ(from_ring.b, 0x5555U);
    EXPECT_EQ(from_ring.c, 0x6666U);
}

// The seqlock direction, where the reader cannot preempt the writer (CR-2).
// Under a concurrent writer, every value the reader observes must be one that
// the writer published whole.
TEST(PublishedTest, SeqlockReaderNeverObservesATornValue) {
    constexpr std::uint32_t ROUNDS = 200000;
    Published<std::uint64_t, SEQLOCK> published;
    std::atomic<bool> done{false};

    std::thread writer([&published, &done] {
        for (std::uint32_t round = 1; round <= ROUNDS; ++round) {
            published.store(checked(round));
        }
        done.store(true, std::memory_order_release);
    });

    std::uint64_t reads = 0;
    std::optional<std::uint64_t> torn;
    while (!done.load(std::memory_order_acquire)) {
        // Before the first store, T{} is not a checked pair. The flag is
        // published after a complete store, so true makes the check meaningful.
        if (!published.published()) {
            continue;
        }
        const std::uint64_t value = published.load();
        if (!is_intact(value)) {
            torn = value;
            break;
        }
        ++reads;
    }
    // Join before asserting: ASSERT_* can return and terminate a joinable
    // thread.
    writer.join();
    EXPECT_FALSE(torn.has_value()) << "torn read: " << torn.value_or(0);
    EXPECT_TRUE(is_intact(published.load()));
    EXPECT_GT(reads, 0U);
    EXPECT_EQ(published.load(), checked(ROUNDS));
}

// The unthrottled writer deliberately breaks its rate contract (CR-3).
// The reader must never spin. Validated words are whole whatever the writer
// does. Exhaustion is counted, not asserted on. A 64-word payload exposes laps.
// Their timing is statistical, so SlotReadValidatesOnlyBelowALap checks the
// predicate deterministically.
struct Wide {
    std::array<std::uint32_t, 64> words{};
};

bool is_whole(const Wide& value) {
    return std::ranges::all_of(value.words, [&value](std::uint32_t word) {
        return word == value.words[0];
    });
}

// A first-store race returns T{} or the stored value, never zeroed words (CR-2,
// CR-8). Each object is stored once. Timing can miss the defect but cannot
// invent it.
TEST(PublishedTest, SeqlockReadRacingTheFirstStoreIsNeverZeroedWords) {
    constexpr std::size_t OBJECTS = 2000000;
    constexpr int ROUNDS = 5;
    using Latch = Published<Defaulted, SEQLOCK>;
    std::size_t zeroed = 0;
    for (int round = 0; round < ROUNDS && zeroed == 0; ++round) {
        std::vector<Latch> latches(OBJECTS);
        std::thread reader([&latches, &zeroed] {
            for (const Latch& latch : latches) {
                for (;;) {
                    const std::int32_t seen = latch.load().value;
                    if (seen == Defaulted{}.value) {
                        continue;
                    }
                    if (seen == 0) {
                        ++zeroed;
                    }
                    break;
                }
            }
        });
        for (Latch& latch : latches) {
            latch.store(Defaulted{.value = 9});
        }
        reader.join();
    }
    EXPECT_EQ(zeroed, 0U) << "a read returned the storage's zeroed words";
}

TEST(PublishedTest, SlotRingReaderNeverSpinsAndNeverValidatesATornValue) {
    constexpr std::uint32_t ROUNDS = 400000;
    Published<Wide, SLOT_RING> published;
    std::atomic<bool> done{false};

    std::thread writer([&published, &done] {
        Wide value;
        for (std::uint32_t round = 1; round <= ROUNDS; ++round) {
            value.words.fill(round);
            published.store(value);
        }
        done.store(true, std::memory_order_release);
    });

    std::uint64_t reads = 0;
    std::uint64_t exhausted = 0;
    std::uint64_t torn = 0;
    while (!done.load(std::memory_order_acquire)) {
        const auto read = published.load();
        ++reads;
        if (!read.ok()) {
            ++exhausted;
        } else if (!is_whole(read.value())) {
            ++torn;
        }
    }
    // Join before asserting. An ASSERT_* return would run ~thread on a
    // joinable thread and call std::terminate().
    writer.join();
    EXPECT_EQ(torn, 0U) << "torn values reported as validated";
    EXPECT_GT(reads, 0U);
    RecordProperty("exhausted_reads", std::to_string(exhausted));
    EXPECT_EQ(validated(published.load()).words[0], ROUNDS);
}

// Validation boundaries, including 32-bit wrap, are deterministic (CR-10).
// This pins the arithmetic that a concurrent test cannot reliably prove (CR-3).
TEST(PublishedTest, SlotReadValidatesOnlyBelowALap) {
    using pico_util::detail::slot_read_validates;
    // A consumer's writer contract depends on these exact values (CR-10).
    using Ring = Published<std::uint32_t, SLOT_RING>;
    static_assert(Ring::SLOT_COUNT == 4);
    static_assert(Ring::MAX_READ_ATTEMPTS == 3);
    constexpr std::size_t SLOTS = Ring::SLOT_COUNT;
    static_assert(slot_read_validates(10, 10, SLOTS));
    static_assert(slot_read_validates(10, 11, SLOTS));
    static_assert(slot_read_validates(10, 12, SLOTS));
    static_assert(!slot_read_validates(10, 13, SLOTS));
    static_assert(!slot_read_validates(10, 14, SLOTS));

    constexpr std::uint32_t TOP = 0xFFFFFFFFU;
    static_assert(slot_read_validates(TOP, TOP, SLOTS));
    static_assert(slot_read_validates(TOP, 1U, SLOTS));
    static_assert(!slot_read_validates(TOP, 2U, SLOTS));
    SUCCEED();
}

// Rare stores implement the writer contract. The test checks intact values
// (CR-4).
TEST(PublishedTest, SlotRingReaderNeverTearsWithinTheWriterContract) {
    constexpr std::uint32_t ROUNDS = 2000;
    Published<std::uint64_t, SLOT_RING> published;
    std::atomic<bool> done{false};

    std::thread writer([&published, &done] {
        for (std::uint32_t round = 1; round <= ROUNDS; ++round) {
            published.store(checked(round));
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
        done.store(true, std::memory_order_release);
    });

    std::uint64_t reads = 0;
    std::optional<std::uint64_t> torn;
    while (!done.load(std::memory_order_acquire)) {
        if (!published.published()) {
            continue;
        }
        const auto read = published.load();
        // A host read descheduled across three stores can exhaust, so this
        // case skips errors. SlotReadValidatesOnlyBelowALap checks success
        // within bounds deterministically.
        if (!read.ok()) {
            continue;
        }
        if (!is_intact(read.value())) {
            torn = read.value();
            break;
        }
        ++reads;
    }
    // Join before asserting: ASSERT_* can return and terminate a joinable
    // thread.
    writer.join();
    EXPECT_FALSE(torn.has_value()) << "torn read: " << torn.value_or(0);
    EXPECT_GT(reads, 0U);
    EXPECT_EQ(validated(published.load()), checked(ROUNDS));
}

// A 64-bit payload uses only 32-bit atomics (CR-5).
TEST(PublishedTest, PublicationUsesThirtyTwoBitAtomicsOnly) {
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

    // A 64-bit atomic raises alignment to 8 but leaves size unchanged:
    // padding replaces the extra words. Only alignment detects that
    // substitution.
    static_assert(
        alignof(Published<std::uint64_t, SLOT_RING>) == alignof(std::uint32_t),
        "an atomic wider than a word would raise the alignment");
    static_assert(
        alignof(Published<std::uint64_t, SEQLOCK>) == alignof(std::uint32_t),
        "an atomic wider than a word would raise the alignment");
    SUCCEED();
}

}  // namespace
