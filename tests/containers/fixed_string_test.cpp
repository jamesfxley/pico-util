// SPDX-License-Identifier: MIT
// FixedString<N> (BR-1, BR-2, BR-13). The boundary cases are the whole test: a
// string of exactly CAPACITY must fit, and an append that would not fit must
// leave the contents byte-for-byte as they were.
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

    // One more character does not fit. The refusal must leave the contents
    // untouched -- a partial append is a corrupted message, not a short one.
    EXPECT_FALSE(text.append("h"));
    EXPECT_EQ(text.view(), "abcdefg");
    EXPECT_EQ(text.size(), 7U);
    EXPECT_STREQ(text.c_str(), "abcdefg");

    // A refused append that is longer than the whole capacity, likewise.
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

// BR-1: the terminator moves with the contents. A shorter string written over
// a longer one is where a missing NUL shows, because the tail of the old
// contents is still in the storage.
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

// BR-1: truncated() is the only way in from text. A converting constructor
// would let a long string_view truncate silently wherever one is passed.
TEST(FixedStringTest, TextConvertsOnlyThroughTruncated) {
    static_assert(!std::is_constructible_v<FixedString<8>, std::string_view>);
    static_assert(!std::is_convertible_v<std::string_view, FixedString<8>>);
    static_assert(!std::is_constructible_v<FixedString<8>, const char*>);
    SUCCEED();
}

// BR-6: the storage is a member, so the object is its own buffer.
TEST(FixedStringTest, StorageIsInline) {
    // 64 characters: more than a pointer and a size, so a layout holding its
    // text elsewhere cannot reach it.
    static_assert(sizeof(FixedString<64>) >= 64);
    SUCCEED();
}

// BR-1: construction from text is named, because the name is the report. The
// factory accepts the loss; assign() is what reports it. A converting
// constructor could do neither, and a truncated value it produced was
// indistinguishable from one that fitted.
TEST(FixedStringTest, TruncatedNamesTheLossItAccepts) {
    const auto cut = FixedString<8>::truncated("content-length");
    EXPECT_EQ(cut.size(), 7U);
    EXPECT_EQ(cut.view(), "content");
    EXPECT_STREQ(cut.c_str(), "content");
    EXPECT_TRUE(cut.full());

    // A string that fits round-trips through the same factory.
    const auto whole = FixedString<8>::truncated("content");
    EXPECT_EQ(whole.view(), "content");

    // And assign() still reports what the factory swallowed.
    FixedString<8> reported;
    EXPECT_FALSE(reported.assign("content-length"));
    EXPECT_EQ(reported.view(), cut.view());
}

// BR-13: view() aliases the string's own storage rather than copying it. Two
// observable consequences, and both are asserted: the view points at the same
// bytes c_str() does, and a view taken before an assign() sees the new
// contents -- which is exactly why an assign() invalidates it. A copying view
// would keep the old text and pass the first assertion only by accident of
// address.
TEST(FixedStringTest, ViewAliasesTheStorage) {
    FixedString<16> text;
    ASSERT_TRUE(text.assign("abc"));
    const std::string_view before = text.view();
    EXPECT_EQ(before.data(), text.c_str());

    ASSERT_TRUE(text.assign("xyz"));
    EXPECT_EQ(before, "xyz");
}

// The noexcept on the three mutators is part of the type's contract rather than
// a detail: a type holding a FixedString with a default member initialiser is
// nothrow default-constructible only because of it, and FlatMap refuses a value
// that is not.
TEST(FixedStringTest, TheMutatorsDoNotThrow) {
    static_assert(noexcept(FixedString<8>::truncated("abc")));
    static_assert(noexcept(std::declval<FixedString<8>&>().assign("abc")));
    static_assert(noexcept(std::declval<FixedString<8>&>().append("abc")));
    SUCCEED();
}
}  // namespace
