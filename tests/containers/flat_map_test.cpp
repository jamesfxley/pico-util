// SPDX-License-Identifier: MIT
// Fixed-capacity map tests (BR-3, BR-4, BR-6, BR-14, BR-15).
// Entries stay in place after unrelated erases (BR-4). Entry order is
// unspecified, so assertions check the set of remaining keys, never the order.
#include "pico_util/containers/flat_map.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <type_traits>

using pico_util::FlatMap;

namespace {

using Table = FlatMap<std::uint8_t, int, 4>;

std::set<std::uint8_t> keys_of(const Table& table) {
    std::set<std::uint8_t> found;
    table.for_each([&found](std::uint8_t key, int) {
        found.insert(key);
        return Table::Visit::CONTINUE;
    });
    return found;
}

TEST(FlatMapTest, InsertsUpToCapacity) {
    Table table;
    EXPECT_TRUE(table.empty());
    EXPECT_EQ(table.capacity(), 4U);

    for (std::uint8_t key = 1; key <= 4; ++key) {
        ASSERT_NE(table.insert_or_assign(key, key * 10), nullptr);
    }
    EXPECT_EQ(table.size(), 4U);
    EXPECT_TRUE(table.full());
    EXPECT_EQ(keys_of(table), (std::set<std::uint8_t>{1, 2, 3, 4}));
}

TEST(FlatMapTest, DuplicateKeyReplaces) {
    Table table;
    ASSERT_NE(table.insert_or_assign(7, 1), nullptr);
    ASSERT_NE(table.insert_or_assign(7, 2), nullptr);
    EXPECT_EQ(table.size(), 1U);
    ASSERT_NE(table.find(7), nullptr);
    EXPECT_EQ(*table.find(7), 2);
}

TEST(FlatMapTest, NewKeyAtCapacityIsRefused) {
    Table table;
    for (std::uint8_t key = 1; key <= 4; ++key) {
        ASSERT_NE(table.insert_or_assign(key, key * 10), nullptr);
    }
    // The insert fails and evicts nothing: no existing entry is displaced.
    EXPECT_EQ(table.insert_or_assign(9, 99), nullptr);
    EXPECT_EQ(table.size(), 4U);
    EXPECT_FALSE(table.contains(9));
    EXPECT_EQ(keys_of(table), (std::set<std::uint8_t>{1, 2, 3, 4}));
    EXPECT_EQ(table.value_or(1, -1), 10);

    // A key already present is still found at capacity and still replaced.
    ASSERT_NE(table.insert_or_assign(2, 22), nullptr);
    EXPECT_EQ(table.value_or(2, -1), 22);
}

TEST(FlatMapTest, EraseLeavesTheRestFindable) {
    Table table;
    for (std::uint8_t key = 1; key <= 4; ++key) {
        ASSERT_NE(table.insert_or_assign(key, key * 10), nullptr);
    }
    // A middle key. A table that compacted on erase would move an entry here,
    // which BR-4 forbids and this test would catch.
    EXPECT_TRUE(table.erase(2));
    EXPECT_EQ(table.size(), 3U);
    EXPECT_FALSE(table.contains(2));
    EXPECT_EQ(keys_of(table), (std::set<std::uint8_t>{1, 3, 4}));
    EXPECT_EQ(table.value_or(1, -1), 10);
    EXPECT_EQ(table.value_or(3, -1), 30);
    EXPECT_EQ(table.value_or(4, -1), 40);

    // Erasing frees a slot, so a new key fits again.
    EXPECT_NE(table.insert_or_assign(9, 99), nullptr);
    EXPECT_EQ(keys_of(table), (std::set<std::uint8_t>{1, 3, 4, 9}));
}

TEST(FlatMapTest, EraseOfAnAbsentKeyReportsIt) {
    Table table;
    ASSERT_NE(table.insert_or_assign(1, 10), nullptr);
    EXPECT_FALSE(table.erase(2));
    EXPECT_EQ(table.size(), 1U);
    EXPECT_TRUE(table.erase(1));
    EXPECT_TRUE(table.empty());
    EXPECT_FALSE(table.erase(1));
}

TEST(FlatMapTest, LookupOfAnAbsentKey) {
    Table table;
    ASSERT_NE(table.insert_or_assign(1, 10), nullptr);
    EXPECT_EQ(table.find(2), nullptr);
    EXPECT_FALSE(table.contains(2));
    EXPECT_EQ(table.value_or(2, -1), -1);
}

TEST(FlatMapTest, TryEmplaceValueInitialisesANewSlot) {
    Table table;
    int* slot = table.try_emplace(3);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(*slot, 0);
    *slot = 42;
    // A second call finds the existing entry and does not add another.
    int* again = table.try_emplace(3);
    ASSERT_NE(again, nullptr);
    EXPECT_EQ(*again, 42);
    EXPECT_EQ(table.size(), 1U);
}

TEST(FlatMapTest, ClearEmptiesWithoutChangingCapacity) {
    Table table;
    ASSERT_NE(table.insert_or_assign(1, 10), nullptr);
    ASSERT_NE(table.insert_or_assign(2, 20), nullptr);
    table.clear();
    EXPECT_TRUE(table.empty());
    EXPECT_EQ(table.capacity(), 4U);
    EXPECT_FALSE(table.contains(1));
    EXPECT_TRUE(keys_of(table).empty());
}

// An unrelated erase leaves a held pointer naming the same entry (BR-4).
TEST(FlatMapTest, AnEntryNeverMoves) {
    Table table;
    ASSERT_NE(table.insert_or_assign(1, 10), nullptr);
    ASSERT_NE(table.insert_or_assign(2, 20), nullptr);
    ASSERT_NE(table.insert_or_assign(3, 30), nullptr);

    // The last-inserted entry is the one that a swap-erase would move into the
    // hole.
    int* held = table.find(3);
    ASSERT_NE(held, nullptr);

    // A swap-erase would move the held entry into key 1's slot.
    EXPECT_TRUE(table.erase(1));
    EXPECT_NE(table.try_emplace(4), nullptr);

    EXPECT_EQ(held, table.find(3)) << "entry 3 moved";
    EXPECT_EQ(*held, 30);
}

// A released slot is reused and not stranded, so the capacity is a true
// capacity and not a high-water mark.
TEST(FlatMapTest, AReleasedSlotIsReused) {
    Table table;
    for (std::uint8_t key = 1; key <= 4; ++key) {
        ASSERT_NE(table.insert_or_assign(key, key * 10), nullptr);
    }
    EXPECT_TRUE(table.full());

    EXPECT_TRUE(table.erase(2));
    EXPECT_FALSE(table.full());
    EXPECT_NE(table.insert_or_assign(9, 90), nullptr);
    EXPECT_TRUE(table.full());
    EXPECT_EQ(keys_of(table), (std::set<std::uint8_t>{1, 3, 4, 9}));
}

// The walk stops when asked, so a search works without exposing the slots that
// a span would have to include.
TEST(FlatMapTest, ForEachStopsWhenAsked) {
    Table table;
    ASSERT_NE(table.insert_or_assign(1, 10), nullptr);
    ASSERT_NE(table.insert_or_assign(2, 20), nullptr);
    ASSERT_NE(table.insert_or_assign(3, 30), nullptr);

    int visited = 0;
    table.for_each([&visited](std::uint8_t, int&) {
        ++visited;
        return Table::Visit::STOP;
    });
    EXPECT_EQ(visited, 1);
}

// Wide elements distinguish inline storage from a pointer-sized layout (BR-6).
TEST(FlatMapTest, StorageIsInline) {
    using Wide = FlatMap<std::uint8_t, std::array<char, 64>, 4>;
    static_assert(sizeof(Wide) >= std::size_t{4} * 64U);
    SUCCEED();
}

// Erase must bring the shared count back to one (BR-14).
// No refill follows: overwriting the slot would hide a missing reset.
TEST(FlatMapTest, EraseReleasesWhatTheValueOwned) {
    FlatMap<std::uint8_t, std::shared_ptr<int>, 4> table;
    const auto owned = std::make_shared<int>(7);
    ASSERT_NE(table.insert_or_assign(1, owned), nullptr);
    ASSERT_EQ(owned.use_count(), 2);

    ASSERT_TRUE(table.erase(1));
    EXPECT_EQ(owned.use_count(), 1);
}

// The companion case for clear() (BR-14): every live value is released, and not
// only the slot count reset.
TEST(FlatMapTest, ClearReleasesWhatTheValuesOwned) {
    FlatMap<std::uint8_t, std::shared_ptr<int>, 4> table;
    const auto first = std::make_shared<int>(1);
    const auto second = std::make_shared<int>(2);
    ASSERT_NE(table.insert_or_assign(1, first), nullptr);
    ASSERT_NE(table.insert_or_assign(2, second), nullptr);

    table.clear();
    EXPECT_EQ(first.use_count(), 1);
    EXPECT_EQ(second.use_count(), 1);
    EXPECT_EQ(table.size(), 0U);
}

// The throwing-V rejection must not apply to a nested value type (BR-16).
// The enclosing class is incomplete at the map declaration. Checking the trait
// there would reject a value that is nothrow once the class is complete.
struct Holder {
    struct Entry {
        int count = 1;
        std::array<char, 8> tag{};
    };

    FlatMap<std::uint8_t, Entry, 2> map;
};

TEST(FlatMapTest, AValueNestedInItsHolderIsAccepted) {
    Holder holder;
    Holder::Entry* entry = holder.map.try_emplace(1);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->count, 1);
    EXPECT_TRUE(holder.map.erase(1));
}

// A visitor may erase the current entry and still visit each live slot once
// (BR-15).
TEST(FlatMapTest, AVisitorMayEraseTheEntryItIsVisiting) {
    Table table;
    for (std::uint8_t key = 1; key <= 4; ++key) {
        ASSERT_NE(table.insert_or_assign(key, key * 10), nullptr);
    }
    std::set<std::uint8_t> visited;
    table.for_each([&table, &visited](std::uint8_t key, int) {
        visited.insert(key);
        if (key % 2 == 0) {
            EXPECT_TRUE(table.erase(key));
        }
        return Table::Visit::CONTINUE;
    });
    EXPECT_EQ(visited, (std::set<std::uint8_t>{1, 2, 3, 4}));
    EXPECT_EQ(keys_of(table), (std::set<std::uint8_t>{1, 3}));
    EXPECT_EQ(table.size(), 2U);
}

// A mutating walk cannot rewrite a key (BR-3), so the visitor receives the key
// by const reference and the value by mutable reference.
TEST(FlatMapTest, AVisitorCannotRewriteAKey) {
    Table table;
    ASSERT_NE(table.insert_or_assign(1, 10), nullptr);
    ASSERT_NE(table.insert_or_assign(2, 20), nullptr);

    // Checking the deduced type inside the visitor catches a missing
    // as_const(). It cannot be checked from outside: a mutable-key call fails
    // in the unconstrained body, not by substitution.
    table.for_each([](auto&& key, int& value) {
        static_assert(std::is_const_v<std::remove_reference_t<decltype(key)>>,
                      "for_each must pass the key by const reference (BR-3)");
        value *= 2;
        return Table::Visit::CONTINUE;
    });

    EXPECT_EQ(table.value_or(1, 0), 20);
    EXPECT_EQ(table.value_or(2, 0), 40);
    EXPECT_EQ(table.size(), 2U);
    EXPECT_EQ(keys_of(table), (std::set<std::uint8_t>{1, 2}));
}
}  // namespace
