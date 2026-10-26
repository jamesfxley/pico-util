// SPDX-License-Identifier: MIT
// DR-5: viewing text as bytes without copying. The point of the header is that
// the span aliases the caller's storage rather than owning a copy, so the
// address is the assertion that matters -- a version that copied would pass a
// content comparison and fail these.
#include "pico_util/bytes/as_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>

using pico_util::as_bytes;

namespace {

TEST(AsBytesTest, ViewsTextWithoutCopying) {
    const std::string text = "GET / HTTP/1.1";
    const auto bytes = as_bytes(text);

    // The same storage, not a copy of it.
    EXPECT_EQ(static_cast<const void*>(bytes.data()),
              static_cast<const void*>(text.data()));
    EXPECT_EQ(bytes.size(), text.size());
    EXPECT_EQ(bytes[0], static_cast<std::uint8_t>('G'));
    EXPECT_EQ(bytes.back(), static_cast<std::uint8_t>('1'));
}

// An empty view has a null data() when default-constructed, and a span over a
// null pointer with a nonzero size would be the bug this guards.
TEST(AsBytesTest, EmptyTextYieldsAnEmptySpan) {
    EXPECT_TRUE(as_bytes(std::string_view{}).empty());
    EXPECT_EQ(as_bytes(std::string_view{}).size(), 0U);
    EXPECT_TRUE(as_bytes(std::string_view{""}).empty());
}

// High bytes survive as unsigned values rather than sign-extending through
// char, which is signed on this target.
TEST(AsBytesTest, HighBytesReadAsUnsigned) {
    const std::string text = "\x80\xFF";
    const auto bytes = as_bytes(text);

    ASSERT_EQ(bytes.size(), 2U);
    EXPECT_EQ(bytes[0], 0x80U);
    EXPECT_EQ(bytes[1], 0xFFU);
}

}  // namespace
