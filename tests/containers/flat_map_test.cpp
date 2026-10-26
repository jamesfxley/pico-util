// SPDX-License-Identifier: MIT
// FlatMap<K, V, N> (BR-3, BR-4, BR-6, BR-14, BR-15). Erase releases the slot in
// place and touches no other entry, which is BR-4 and is what makes a pointer
// held across an unrelated erase stay valid. Entry order is still not part of
// the contract, so every assertion here is on the *set* of remaining keys and
// never on their order -- a test that pinned order would pass today and fail on
// any change to the erase strategy without a real defect behind it.
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
    // Refused, not evicted: nothing already present is displaced.
    EXPECT_EQ(table.insert_or_assign(9, 99), nullptr);
    EXPECT_EQ(table.size(), 4U);
    EXPECT_FALSE(table.contains(9));
    EXPECT_EQ(keys_of(table), (std::set<std::uint8_t>{1, 2, 3, 4}));
    EXPECT_EQ(table.value_or(1, -1), 10);

    // A key already present still resolves at capacity, and still replaces.
    ASSERT_NE(table.insert_or_assign(2, 22), nullptr);
    EXPECT_EQ(table.value_or(2, -1), 22);
}

TEST(FlatMapTest, EraseLeavesTheRestFindable) {
    Table table;
    for (std::uint8_t key = 1; key <= 4; ++key) {
        ASSERT_NE(table.insert_or_assign(key, key * 10), nullptr);
    }
    // A middle key: a table that compacted on erase would move an entry here,
    // which is what BR-4 refuses and what this would then catch.
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
    // A second call finds the existing entry rather than adding a second one.
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

// BR-4: the property a caller holding a pointer depends on and cannot check
// for itself. Dense storage would fail this -- an unrelated erase would move
// the last entry into the hole and the pointer would name a different one.
TEST(FlatMapTest, AnEntryNeverMoves) {
    Table table;
    ASSERT_NE(table.insert_or_assign(1, 10), nullptr);
    ASSERT_NE(table.insert_or_assign(2, 20), nullptr);
    ASSERT_NE(table.insert_or_assign(3, 30), nullptr);

    // The last-inserted entry: the one a swap-erase moves into the hole.
    int* held = table.find(3);
    ASSERT_NE(held, nullptr);

    // Erase a different key, then insert another. Under swap-erase the last
    // entry lands in key 1's slot and the map repacks; here nothing moves.
    EXPECT_TRUE(table.erase(1));
    EXPECT_NE(table.try_emplace(4), nullptr);

    EXPECT_EQ(held, table.find(3)) << "entry 3 moved";
    EXPECT_EQ(*held, 30);
}

// A released slot is reused rather than stranded, so capacity is really
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

// The walk stops when asked, which is what makes a search expressible without
// exposing the slots a span would have to include.
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

// BR-6: storage is a member sized at instantiation. The element is wide on
// purpose: with a four-byte value a map holding a pointer to heap storage is
// already large enough on LP64 to pass a size floor, so the floor would not
// tell the two layouts apart.
TEST(FlatMapTest, StorageIsInline) {
    using Wide = FlatMap<std::uint8_t, std::array<char, 64>, 4>;
    static_assert(sizeof(Wide) >= std::size_t{4} * 64U);
    SUCCEED();
}

// BR-14: erase resets the released value rather than leaving it in place. A
// value with an observable lifetime is what makes that visible: the map's copy
// of the pointer is the only other owner, so erase must bring the count back to
// one. Refilling the slot afterwards would hide a missing reset, because the
// refill overwrites it -- which is why nothing is inserted here after the
// erase.
TEST(FlatMapTest, EraseReleasesWhatTheValueOwned) {
    FlatMap<std::uint8_t, std::shared_ptr<int>, 4> table;
    const auto owned = std::make_shared<int>(7);
    ASSERT_NE(table.insert_or_assign(1, owned), nullptr);
    ASSERT_EQ(owned.use_count(), 2);

    ASSERT_TRUE(table.erase(1));
    EXPECT_EQ(owned.use_count(), 1);
}

// BR-14's companion for clear(): every live value is released, not only the
// slot count reset.
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

// BR-16's refusal of a throwing V must not refuse a nested value type. A struct
// with default member initialisers declared inside the class that holds the
// map is not yet usable for its initialisers while that class is incomplete,
// so a class-scope trait check reads it as throwing and fails the build --
// which is how pico-bno085 declares its table. The inserted value is also the
// value-initialised one, default member initialisers included.
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

// BR-15: a visitor may erase the entry it is visiting. The walk continues over
// the remaining live slots and visits each exactly once, and the map is
// consistent afterwards.
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

// BR-3: a mutating walk cannot rewrite a key. Two live slots under one key are
// unreconcilable -- find() and try_emplace() resolve to the first, erase()
// removes one and decrements size_ once -- so the visitor sees the key by
// const reference and the value by mutable one.
TEST(FlatMapTest, AVisitorCannotRewriteAKey) {
    Table table;
    ASSERT_NE(table.insert_or_assign(1, 10), nullptr);
    ASSERT_NE(table.insert_or_assign(2, 20), nullptr);

    // The constness is asserted inside the visitor, on the type for_each
    // deduces into it. That binds the check to the real call: drop the
    // std::as_const() in for_each and this static_assert fires.
    //
    // It cannot be asserted from outside. for_each is an unconstrained
    // template, so a visitor taking the key by mutable reference is a hard
    // error in the body rather than a substitution failure -- is_invocable_v
    // and a requires-expression both report it callable either way, and an
    // assertion written over a bare function pointer states a property of the
    // language rather than one of this map.
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
