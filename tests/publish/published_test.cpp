// SPDX-License-Identifier: MIT
// Published<T, Strategy> (CR-1..CR-10). Both strategies are exercised under two
// real threads, because a publication protocol read only single-threaded is a
// protocol nothing has tested: a torn read needs a writer running underneath
// the reader to appear at all.
//
// The values published are constructed so that a splice of two of them is
// detectable. A counter whose halves are (n, ~n) means any mismatched pair of
// words is a torn read, which a plain increasing counter would hide -- splicing
// the high half of 0x0000_0001 onto the low half of 0x0000_0002 yields a value
// that looks perfectly ordinary.
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

// High half is the complement of the low half, so any splice is visible.
constexpr std::uint64_t checked(std::uint32_t n) {
    return (static_cast<std::uint64_t>(~n) << 32U) | n;
}

constexpr bool is_intact(std::uint64_t value) {
    const auto low = static_cast<std::uint32_t>(value);
    const auto high = static_cast<std::uint32_t>(value >> 32U);
    return high == ~low;
}

// A slot-ring read the test expects to validate, failing the case if it did
// not. Single-threaded reads always validate, so a failure here is the reader's
// validation being wrong rather than a writer lapping it.
template <typename T>
T validated(const pico_util::Result<T, pico_util::PublishedReadError>& read) {
    EXPECT_TRUE(read.ok()) << "the slot-ring read was not validated";
    return read.value_or(T{});
}

// CR-1: the primary template is declared and not defined, so a strategy is
// mandatory. Both named ones instantiate here; that the unstrategied form fails
// is the pico_util_refuses_published_without_strategy build-failure test, since
// naming it in this file would fail this build.
TEST(PublishedTest, BothStrategiesInstantiate) {
    static_assert(
        std::is_default_constructible_v<Published<std::uint64_t, SEQLOCK>>);
    static_assert(
        std::is_default_constructible_v<Published<std::uint64_t, SLOT_RING>>);
    SUCCEED();
}

// CR-6: nothing is published before the first store.
TEST(PublishedTest, ReportsNothingPublishedBeforeTheFirstStore) {
    const Published<std::uint64_t, SEQLOCK> seqlock;
    EXPECT_FALSE(seqlock.published());
    EXPECT_EQ(seqlock.load(), 0U);

    const Published<std::uint64_t, SLOT_RING> ring;
    EXPECT_FALSE(ring.published());
    EXPECT_EQ(validated(ring.load()), 0U);
}

// CR-8: before the first store, load() returns T{} -- not the all-bits-zero
// value the words start as. The two coincide for std::uint64_t, which is why
// the case above cannot tell them apart; a default member initialiser is what
// separates them.
struct Defaulted {
    std::int32_t value = 5;
};

TEST(PublishedTest, ReturnsTheValueInitialisedTBeforeTheFirstStore) {
    const Published<Defaulted, SEQLOCK> seqlock;
    EXPECT_EQ(seqlock.load().value, 5);

    const Published<Defaulted, SLOT_RING> ring;
    EXPECT_EQ(validated(ring.load()).value, 5);
}

// CR-6: published() turns true on a store and stays true.
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

    // Republished values are seen, and published() never goes back.
    ring.store(1);
    EXPECT_EQ(validated(ring.load()), 1U);
    EXPECT_TRUE(ring.published());
}

// A payload wider than one word, so the value spans several of them. Its size
// is a whole number of words; the round-up is the test below.
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

// The word count rounds up, so a payload that is not a whole number of words
// still gets a word to spill into. Nothing else in the tree instantiates such a
// type, and without one PUBLISHED_WORD_COUNT's round-up term is never
// evaluated: truncating it to a plain division would write sizeof(T) bytes
// through a word array one word short, off the end of the writer's stack.
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

// CR-2: the seqlock's direction -- the writer cannot be preempted by the
// reader. Under a concurrent writer, every value the reader observes must be
// one the writer published whole.
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
        // Before the first store the value is value-initialised, which is not
        // a checked pair and would read as torn. published() going true is
        // ordered after a completed store, so past it every value is whole.
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
    // Join before asserting, on every path out of the loop. ASSERT_* expands to
    // a return from the test body, which would run ~thread on a joinable thread
    // and call std::terminate() -- losing the very failure it was reporting,
    // along with the rest of the cases in this binary.
    writer.join();
    EXPECT_FALSE(torn.has_value()) << "torn read: " << torn.value_or(0);
    EXPECT_TRUE(is_intact(published.load()));
    EXPECT_GT(reads, 0U);
    EXPECT_EQ(published.load(), checked(ROUNDS));
}

// CR-3: the slot ring's direction -- the reader may preempt the writer, so it
// must never spin, and a value it reports as validated was published whole
// whatever the writer does. Both hold unconditionally, so the writer here is
// deliberately unthrottled and outside its own contract. If load() could spin,
// this loop would never finish; if the validation let a lapped read through, a
// torn value would arrive as a success. An exhausted read is the writer's
// contract failing and is reported rather than returned, so it is counted and
// not asserted on.
//
// The payload is 64 words, every one set to the round number, so a read the
// writer laps part-way through is visible as mixed words. Whether a lap lands
// inside a read at all depends on thread timing, so this case exercises the
// protocol under a real concurrent writer without being able to prove the
// predicate right; SlotReadValidatesOnlyBelowALap above is what pins that.
struct Wide {
    std::array<std::uint32_t, 64> words{};
};

bool is_whole(const Wide& value) {
    return std::ranges::all_of(value.words, [&value](std::uint32_t word) {
        return word == value.words[0];
    });
}

// CR-2, CR-8: a read racing the first store returns either T{} or the value
// stored, never the zeroed words the storage starts as. The window is between
// a validated snapshot and the read of the published flag, so each object is
// stored once while a reader spins on it; a correct reader can never produce
// the zeroed value, so this can miss the defect but never fail spuriously.
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
    // Join before asserting: an ASSERT_* return would run ~thread on a
    // joinable thread and call std::terminate().
    writer.join();
    EXPECT_EQ(torn, 0U) << "torn values reported as validated";
    EXPECT_GT(reads, 0U);
    RecordProperty("exhausted_reads", std::to_string(exhausted));
    EXPECT_EQ(validated(published.load()).words[0], ROUNDS);
}

// CR-10: the validation arithmetic, pinned at its boundaries. With four slots
// the writer reaches the slot a reader chose after three stores, so a read
// across zero, one or two completed stores validates and a read across three
// or more does not -- including across the counter's wrap, where the
// difference is taken in 32 bits. This is the deterministic half of CR-3: the
// concurrent case below cannot reliably catch a predicate off by one, because
// whether a writer laps a read part-way through depends on thread timing.
TEST(PublishedTest, SlotReadValidatesOnlyBelowALap) {
    using pico_util::detail::slot_read_validates;
    // CR-10 states these values. The header could change them and every other
    // case would still pass; a consumer's writer contract would not.
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

// CR-4: within the writer contract -- stores rare relative to a read -- every
// value the reader observes is one published whole.
//
// The throttle is the contract, not a workaround for a flaky test. A writer
// storing in a tight loop laps the four-slot ring inside a single read, which
// no ring of any depth survives; pico-time's wall clock, the design this is
// promoted from, stores at most once a second. A test that ignored the
// contract would be measuring a case the strategy never claimed.
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
        // Within the contract a read validates. It can still exhaust if this
        // thread is descheduled for longer than three stores take, which a
        // loaded CI host does; that is reported, not returned, so it is
        // skipped rather than treated as a failure of the strategy.
        if (!read.ok()) {
            continue;
        }
        if (!is_intact(read.value())) {
            torn = read.value();
            break;
        }
        ++reads;
    }
    // Join before asserting, on every path out of the loop. ASSERT_* expands to
    // a return from the test body, which would run ~thread on a joinable thread
    // and call std::terminate() -- losing the very failure it was reporting,
    // along with the rest of the cases in this binary.
    writer.join();
    EXPECT_FALSE(torn.has_value()) << "torn read: " << torn.value_or(0);
    EXPECT_GT(reads, 0U);
    EXPECT_EQ(validated(published.load()), checked(ROUNDS));
}

// CR-5: every atomic is 32 bits wide, so a 64-bit payload never reaches
// __atomic_load_8 -- which the SDK implements as a global spinlock with
// interrupts disabled, the opposite of what either strategy is for.
TEST(PublishedTest, PublicationUsesThirtyTwoBitAtomicsOnly) {
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

    // Alignment, not size. Swapping the word array for a std::atomic<uint64_t>
    // leaves sizeof unchanged -- 16 and 40 either way, because the padding the
    // wider alignment demands is exactly the space the extra words occupied --
    // so a size bound cannot see the substitution this test exists to refuse.
    // A 64-bit atomic aligns to 8 and drags its enclosing object's alignment
    // with it, which is the property that does separate them.
    static_assert(
        alignof(Published<std::uint64_t, SLOT_RING>) == alignof(std::uint32_t),
        "an atomic wider than a word would raise the alignment");
    static_assert(
        alignof(Published<std::uint64_t, SEQLOCK>) == alignof(std::uint32_t),
        "an atomic wider than a word would raise the alignment");
    SUCCEED();
}

}  // namespace
