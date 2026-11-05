// SPDX-License-Identifier: MIT
// FixedString<N> (BR-1, BR-2, BR-13). The boundary cases are the test: a string
// of exactly CAPACITY must fit, and an append that does not fit must leave the
// contents unchanged.
#include "pico_util/containers/fixed_string.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

using pico_util::FixedString;

namespace {

TEST(FixedStringTest, HoldsCapacityMinusOne) {
    FixedString<8> text;
    EXPECT_EQ(text.capacity(), 7U);
    EXPECT_TRUE(text.empty());

    // Exactly CAPACITY fits and is not a truncation.
    EXPECT_TRUE(text.assign("1234567"));
    EXPECT_EQ(text.size(), 7U);
    EXPECT_TRUE(text.full());
    EXPECT_EQ(text.view(), "1234567");
    EXPECT_STREQ(text.c_str(), "1234567");
}

TEST(FixedStringTest, TruncationStaysTerminated) {
    FixedString<8> text;
    EXPECT_FALSE(text.assign("12345678"));
    EXPECT_EQ(text.size(), 7U);
    EXPECT_EQ(text.view(), "1234567");
    // The NUL is the reserved byte, so c_str() is valid after truncation too.
    EXPECT_EQ(std::string(text.c_str()).size(), 7U);
}

TEST(FixedStringTest, AppendIsAllOrNothing) {
    FixedString<8> text;
    EXPECT_TRUE(text.append("abc"));
    EXPECT_TRUE(text.append("defg"));
    EXPECT_EQ(text.view(), "abcdefg");

    // A failed append must leave the contents untouched.
    EXPECT_FALSE(text.append("h"));
    EXPECT_EQ(text.view(), "abcdefg");
    EXPECT_EQ(text.size(), 7U);
    EXPECT_STREQ(text.c_str(), "abcdefg");

    // A failed append longer than the whole capacity does likewise.
    FixedString<8> other;
    EXPECT_TRUE(other.append("ab"));
    EXPECT_FALSE(other.append("cdefghijkl"));
    EXPECT_EQ(other.view(), "ab");
}

TEST(FixedStringTest, ClearLeavesAValidEmptyString) {
    auto text = FixedString<8>::truncated("hello");
    EXPECT_FALSE(text.empty());
    text.clear();
    EXPECT_TRUE(text.empty());
    EXPECT_EQ(text.size(), 0U);
    EXPECT_EQ(text.view(), "");
    EXPECT_STREQ(text.c_str(), "");
    EXPECT_EQ(text.capacity(), 7U);
}

TEST(FixedStringTest, ViewExcludesTheTerminator) {
    const auto text = FixedString<8>::truncated("ab");
    EXPECT_EQ(text.view().size(), 2U);
    EXPECT_EQ(text.view(), "ab");
}

// Shortening exposes a missing NUL because the old tail remains (BR-1).
TEST(FixedStringTest, AShorterStringOverALongerOneStaysTerminated) {
    FixedString<8> text;
    ASSERT_TRUE(text.assign("hello"));
    ASSERT_TRUE(text.assign("hi"));
    EXPECT_STREQ(text.c_str(), "hi");

    ASSERT_TRUE(text.assign("hello"));
    text.clear();
    ASSERT_TRUE(text.append("h"));
    EXPECT_STREQ(text.c_str(), "h");
}

// Only truncated() constructs from text, naming the accepted loss (BR-1).
TEST(FixedStringTest, TextConvertsOnlyThroughTruncated) {
    static_assert(!std::is_constructible_v<FixedString<8>, std::string_view>);
    static_assert(!std::is_convertible_v<std::string_view, FixedString<8>>);
    static_assert(!std::is_constructible_v<FixedString<8>, const char*>);
    SUCCEED();
}

// The storage is a member, so the object is its own buffer (BR-6).
TEST(FixedStringTest, StorageIsInline) {
    // 64 characters is more than a pointer and a size, so a layout that holds
    // its text elsewhere cannot reach it.
    static_assert(sizeof(FixedString<64>) >= 64);
    SUCCEED();
}

// The named factory accepts truncation. assign() reports it (BR-1).
TEST(FixedStringTest, TruncatedNamesTheLossItAccepts) {
    const auto cut = FixedString<8>::truncated("content-length");
    EXPECT_EQ(cut.size(), 7U);
    EXPECT_EQ(cut.view(), "content");
    EXPECT_STREQ(cut.c_str(), "content");
    EXPECT_TRUE(cut.full());

    // A string that fits round-trips through the same factory.
    const auto whole = FixedString<8>::truncated("content");
    EXPECT_EQ(whole.view(), "content");

    // assign() still reports what the factory ignored.
    FixedString<8> reported;
    EXPECT_FALSE(reported.assign("content-length"));
    EXPECT_EQ(reported.view(), cut.view());
}

// Address equality and a stale view seeing new text establish aliasing (BR-13).
TEST(FixedStringTest, ViewAliasesTheStorage) {
    FixedString<16> text;
    ASSERT_TRUE(text.assign("abc"));
    const std::string_view before = text.view();
    EXPECT_EQ(before.data(), text.c_str());

    ASSERT_TRUE(text.assign("xyz"));
    EXPECT_EQ(before, "xyz");
}

// nothrow mutators let a nested FixedString value satisfy FlatMap's trait.
TEST(FixedStringTest, TheMutatorsDoNotThrow) {
    static_assert(noexcept(FixedString<8>::truncated("abc")));
    static_assert(noexcept(std::declval<FixedString<8>&>().assign("abc")));
    static_assert(noexcept(std::declval<FixedString<8>&>().append("abc")));
    SUCCEED();
}
}  // namespace
