// SPDX-License-Identifier: MIT
// The allocation harness (MR-5..MR-7, MR-9) and the containers' no-allocation
// claim (BR-6).
//
// This is the only test that replaces the global allocation functions, and it
// is its own executable for that reason: the replacement is program-wide, so
// hosting it alongside tests that allocate freely would make every one of them
// depend on the counter's armed state.
//
// The counter is armed only around the statements under test. GoogleTest itself
// allocates while reporting, so a counter armed for the whole run would measure
// the framework.
#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <thread>

#include "pico_util/bytes/as_bytes.hpp"
#include "pico_util/bytes/byte_order.hpp"
#include "pico_util/containers/fixed_string.hpp"
#include "pico_util/containers/flat_map.hpp"
#include "pico_util/containers/ring_buffer.hpp"
#include "pico_util/heap/heap_watermark.hpp"
#include "pico_util/publish/published.hpp"
#include "pico_util/result/callback.hpp"
#include "pico_util/result/result.hpp"
#include "pico_util/testing/allocation_counter.hpp"

// The operator definitions, in exactly one translation unit of this executable.
#include "pico_util/testing/allocation_operators.ipp"  // IWYU pragma: keep, it defines the replaced operators

using pico_util::testing::AllocationGuard;

namespace {

enum class TestError : std::uint8_t { NONE = 0, FAILED };

// MR-5, gate integrity. Every zero below is worthless unless the counter can be
// shown to move at all -- a harness whose operators were never linked would
// report zero for everything and read as a pass.
// MR-6, CXX-08.1: worker allocations share one armed counting scope.
TEST(AllocationTest, ConcurrentAllocationsHaveAnExactTotal) {
    constexpr std::size_t ITERATIONS = 10000;
    std::atomic<bool> start{false};
    const auto work = [&] {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (std::size_t i = 0; i < ITERATIONS; ++i) {
            void* memory = ::operator new(8);
            ::operator delete(memory);
        }
    };
    std::thread first(work);
    std::thread second(work);
    const AllocationGuard guard;
    start.store(true, std::memory_order_release);
    first.join();
    second.join();
    EXPECT_EQ(AllocationGuard::count(), 2 * ITERATIONS);
}

TEST(AllocationTest, TheCounterObservesAnAllocation) {
    const AllocationGuard guard;
    const std::string block(1024, 'x');
    ASSERT_EQ(block.size(), 1024U);
    ASSERT_GT(AllocationGuard::count(), 0U) << "the counting operators are not "
                                               "linked into this executable";
}

// MR-5: the array and over-aligned forms specifically. These are the ones a
// partial replacement leaves with the implementation's own allocator.
//
// The array form, called by name. std::vector goes through std::allocator,
// which calls the *scalar* operator new, so a vector exercises no array form at
// all; and a new-expression of an array type needs a C array in the source.
// Calling operator new[] directly reaches exactly the replaced function, and
// the deleter hands the block back to its matching operator delete[].
struct ArrayDelete {
    void operator()(void* block) const noexcept { ::operator delete[](block); }
};

using ArrayBlock = std::unique_ptr<void, ArrayDelete>;

TEST(AllocationTest, ArrayAndOverAlignedFormsAreCounted) {
    {
        const AllocationGuard guard;
        const ArrayBlock block(::operator new[](512));
        EXPECT_GT(AllocationGuard::count(), 0U) << "operator new[] not counted";
        EXPECT_NE(block, nullptr);
    }
    {
        struct alignas(64) Wide {
            std::uint64_t value;
        };

        const AllocationGuard guard;
        // Over-aligned: routed through operator new(size, align_val_t) and
        // released through the matching operator delete.
        const auto wide = std::make_unique<Wide>();
        EXPECT_GT(AllocationGuard::count(), 0U)
            << "over-aligned operator new not counted";
        EXPECT_NE(wide, nullptr);
    }
}

// MR-6: counting is scoped to the guard, and each guard starts from zero.
TEST(AllocationTest, CountingIsScopedToTheGuard) {
    // Outside any guard: not counted.
    const std::string before(256, 'a');
    {
        const AllocationGuard guard;
        EXPECT_EQ(AllocationGuard::count(), 0U);
        const std::string inside(256, 'b');
        EXPECT_EQ(inside.size(), 256U);
        EXPECT_GT(AllocationGuard::count(), 0U);
    }
    // Outside any guard again: the tally the guard left does not move.
    const std::size_t tally = AllocationGuard::count();
    const std::string after(256, 'c');
    EXPECT_EQ(AllocationGuard::count(), tally)
        << "an allocation outside a guard was counted";
    // A second guard re-zeroes rather than accumulating.
    {
        const AllocationGuard guard;
        EXPECT_EQ(AllocationGuard::count(), 0U);
    }
    EXPECT_FALSE(before.empty());
    EXPECT_FALSE(after.empty());
}

// MR-7: the replacement is complete, so every allocation and every free goes
// through the same allocator. A block from one allocator reaching the other's
// deallocation function is what a partial replacement produces, and what the
// sanitizer leg reports as alloc-dealloc-mismatch -- so this test's real
// verdict is delivered by running it under ASan, not by its assertions.
//
// All eight allocation forms and all twelve deallocation forms, each called by
// name. A form left unreplaced reaches the implementation's own definition. On
// the sanitizer leg that is the sanitizer's allocator, and the count stays put
// or alloc-dealloc-mismatch is reported. Elsewhere the standard's default
// nothrow and array forms forward to the replaced scalar ones, so nothing here
// fails; pico_util_testing_every_form_replaced catches that case by symbol.
template <typename Allocate, typename Release>
void round_trip(const char* form, Allocate allocate, Release release) {
    void* block = nullptr;
    {
        const AllocationGuard guard;
        block = allocate();
        EXPECT_EQ(AllocationGuard::count(), 1U) << form << " not counted";
    }
    EXPECT_NE(block, nullptr) << form;
    release(block);
}

TEST(AllocationTest, EveryFormRoundTripsThroughOneAllocator) {
    constexpr std::size_t SIZE = 64;
    constexpr auto ALIGN = std::align_val_t{64};

    round_trip(
        "new / delete", [] { return ::operator new(SIZE); },
        [](void* block) { ::operator delete(block); });
    round_trip(
        "new[] / delete[]", [] { return ::operator new[](SIZE); },
        [](void* block) { ::operator delete[](block); });
    round_trip(
        "new / sized delete", [] { return ::operator new(SIZE); },
        [](void* block) { ::operator delete(block, SIZE); });
    round_trip(
        "new[] / sized delete[]", [] { return ::operator new[](SIZE); },
        [](void* block) { ::operator delete[](block, SIZE); });
    round_trip(
        "nothrow new / nothrow delete",
        [] { return ::operator new(SIZE, std::nothrow); },
        [](void* block) { ::operator delete(block, std::nothrow); });
    round_trip(
        "nothrow new[] / nothrow delete[]",
        [] { return ::operator new[](SIZE, std::nothrow); },
        [](void* block) { ::operator delete[](block, std::nothrow); });
    round_trip(
        "aligned new / aligned delete",
        [] { return ::operator new(SIZE, ALIGN); },
        [](void* block) { ::operator delete(block, ALIGN); });
    round_trip(
        "aligned new[] / aligned delete[]",
        [] { return ::operator new[](SIZE, ALIGN); },
        [](void* block) { ::operator delete[](block, ALIGN); });
    round_trip(
        "aligned new / sized aligned delete",
        [] { return ::operator new(SIZE, ALIGN); },
        [](void* block) { ::operator delete(block, SIZE, ALIGN); });
    round_trip(
        "aligned new[] / sized aligned delete[]",
        [] { return ::operator new[](SIZE, ALIGN); },
        [](void* block) { ::operator delete[](block, SIZE, ALIGN); });
    round_trip(
        "aligned nothrow new / aligned nothrow delete",
        [] { return ::operator new(SIZE, ALIGN, std::nothrow); },
        [](void* block) { ::operator delete(block, ALIGN, std::nothrow); });
    round_trip(
        "aligned nothrow new[] / aligned nothrow delete[]",
        [] { return ::operator new[](SIZE, ALIGN, std::nothrow); },
        [](void* block) { ::operator delete[](block, ALIGN, std::nothrow); });
}

// MR-5, CXX-07.2: an over-aligned request no allocator can satisfy is refused.
// Rounding a size near SIZE_MAX up to the alignment wraps to a small one, and
// a tiny block would then be returned for it.
TEST(AllocationTest, AnUnsatisfiableAlignedRequestIsRefused) {
    constexpr auto ALIGN = std::align_val_t{64};
    constexpr auto BYTES = static_cast<std::size_t>(ALIGN);
    constexpr std::size_t MAX = std::numeric_limits<std::size_t>::max();
    // The first size whose rounding would wrap, and the largest. The size just
    // below them does not wrap, so only the allocator can refuse it, and how it
    // does is the allocator's: a sanitizer's aborts instead of returning null.
    for (const std::size_t size : {MAX - BYTES + 2U, MAX}) {
        // Read through a volatile, so the compiler's own size check does not
        // refuse the call before the allocator is asked.
        const volatile std::size_t requested = size;
        const std::size_t unsatisfiable = requested;
        void* const block = ::operator new(unsatisfiable, ALIGN, std::nothrow);
        EXPECT_EQ(block, nullptr) << "size " << size;
        ::operator delete(block, ALIGN, std::nothrow);
    }
}

// MR-9: the count stays readable after the guard's scope closes, until the next
// guard arms. That is what lets a test assert on a tally after the code under
// test has returned, and a destructor that zeroed the count would make every
// such assertion vacuously green. Read here, in the translation unit that
// replaces the allocation functions, because anywhere else the count is always
// zero and the assertion could not fail.
TEST(AllocationTest, TheCountOutlivesTheGuard) {
    {
        const AllocationGuard guard;
        const auto block = std::make_unique<int>(3);
        EXPECT_NE(block, nullptr);
    }
    EXPECT_GT(AllocationGuard::count(), 0U);
}

// BR-6: nothing this library ships allocates. This is what makes the harness a
// gate rather than a facility.
TEST(AllocationTest, NothingInThisLibraryAllocates) {
    const AllocationGuard guard;

    pico_util::FixedString<64> text;
    EXPECT_TRUE(text.assign("hello"));
    EXPECT_TRUE(text.append(" world"));
    const auto clipped = pico_util::FixedString<4>::truncated("hello");
    EXPECT_EQ(clipped.size(), pico_util::FixedString<4>::CAPACITY);
    text.clear();

    pico_util::FlatMap<std::uint8_t, std::uint32_t, 8> table;
    for (std::uint8_t key = 0; key < 8; ++key) {
        EXPECT_NE(table.insert_or_assign(key, key), nullptr);
    }
    table.erase(3);
    EXPECT_NE(table.find(5), nullptr);
    EXPECT_EQ(table.value_or(9, 0), 0U);
    std::size_t walked = 0;
    table.for_each([&walked](std::uint8_t, std::uint32_t&) {
        ++walked;
        return decltype(table)::Visit::CONTINUE;
    });
    EXPECT_EQ(walked, 7U);
    table.clear();

    pico_util::RingBuffer<std::uint32_t, 4, pico_util::SingleContextCounter>
        ring;
    for (std::uint32_t value = 0; value < 9; ++value) {
        EXPECT_TRUE(ring.push(value));
        ring.pop();
    }
    // The acquire/commit pair, which push() does not go through.
    std::uint32_t* slot = ring.acquire();
    ASSERT_NE(slot, nullptr);
    *slot = 7;
    ring.commit();
    EXPECT_EQ(ring.size(), 1U);
    ring.clear();

    // The watermark reads the heap through mallinfo, which allocates nothing.
    pico_util::HeapWatermark watermark;
    watermark.arm();
    EXPECT_EQ(watermark.growth_bytes(), 0U);

    pico_util::Published<
        std::uint64_t,
        pico_util::PublishStrategy::SEQLOCK_WRITER_NOT_PREEMPTIBLE>
        seqlock;
    seqlock.store(1);
    EXPECT_EQ(seqlock.load(), 1U);
    pico_util::Published<
        std::uint64_t, pico_util::PublishStrategy::SLOT_RING_READER_MAY_PREEMPT>
        ring_published;
    ring_published.store(2);
    EXPECT_EQ(ring_published.load().value_or(0U), 2U);

    const pico_util::Result<int, TestError> ok = 1;
    const pico_util::Result<int, TestError> bad = TestError::FAILED;
    EXPECT_EQ(ok.value_or(0), 1);
    EXPECT_EQ(bad.value_or(0), 0);
    const pico_util::Callback<void(int)> unbound;
    unbound(1);

    std::array<std::uint8_t, 8> buffer{};
    pico_util::write_be<std::uint32_t>(std::span<std::uint8_t>{buffer},
                                       0xDEADBEEF);
    const auto decoded = pico_util::read_le<std::uint32_t>(
        std::span<const std::uint8_t>{buffer});
    EXPECT_EQ(decoded, 0xEFBEADDEU);

    EXPECT_EQ(pico_util::as_bytes("pico").size(), 4U);

    EXPECT_EQ(AllocationGuard::count(), 0U);
}

}  // namespace
