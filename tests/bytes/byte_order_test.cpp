// SPDX-License-Identifier: MIT
// Known vectors establish byte order (DR-1..DR-4).
// A round trip alone would pass if encoder and decoder swapped orders together.
#include "pico_util/bytes/byte_order.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

using pico_util::read_be;
using pico_util::read_be_at;
using pico_util::read_le;
using pico_util::read_le_at;
using pico_util::write_be;
using pico_util::write_be_at;
using pico_util::write_le;
using pico_util::write_le_at;

namespace {

// Each accessor's bound is probed independently (DR-2).
// A requires-expression needs no object for a non-default-constructible span.
// Missing bounds permit write overruns or over-reads.
template <std::size_t Offset, typename T, std::size_t Extent>
concept ReadableBe = requires(std::span<const std::uint8_t, Extent> bytes) {
    read_be_at<Offset, T>(bytes);
};

template <std::size_t Offset, typename T, std::size_t Extent>
concept ReadableLe = requires(std::span<const std::uint8_t, Extent> bytes) {
    read_le_at<Offset, T>(bytes);
};

template <std::size_t Offset, typename T, std::size_t Extent>
concept WritableBe = requires(std::span<std::uint8_t, Extent> bytes) {
    write_be_at<Offset, T>(bytes, T{});
};

template <std::size_t Offset, typename T, std::size_t Extent>
concept WritableLe = requires(std::span<std::uint8_t, Extent> bytes) {
    write_le_at<Offset, T>(bytes, T{});
};

// Every width, both orders, signed and unsigned (DR-1).
TEST(ByteOrderTest, RoundTripsEveryWidthInBothOrders) {
    std::array<std::uint8_t, 8> buffer{};
    const std::span<std::uint8_t> out{buffer};
    const std::span<const std::uint8_t> in{buffer};

    write_be<std::uint16_t>(out, 0xBEEF);
    EXPECT_EQ(read_be<std::uint16_t>(in), 0xBEEF);
    write_le<std::uint16_t>(out, 0xBEEF);
    EXPECT_EQ(read_le<std::uint16_t>(in), 0xBEEF);

    write_be<std::uint32_t>(out, 0xDEADBEEF);
    EXPECT_EQ(read_be<std::uint32_t>(in), 0xDEADBEEF);
    write_le<std::uint32_t>(out, 0xDEADBEEF);
    EXPECT_EQ(read_le<std::uint32_t>(in), 0xDEADBEEF);

    write_be<std::uint64_t>(out, 0x0123456789ABCDEF);
    EXPECT_EQ(read_be<std::uint64_t>(in), 0x0123456789ABCDEF);
    write_le<std::uint64_t>(out, 0x0123456789ABCDEF);
    EXPECT_EQ(read_le<std::uint64_t>(in), 0x0123456789ABCDEF);

    write_be<std::int16_t>(out, -2);
    EXPECT_EQ(read_be<std::int16_t>(in), -2);
    write_le<std::int32_t>(out, -2);
    EXPECT_EQ(read_le<std::int32_t>(in), -2);
    write_be<std::int64_t>(out, -2);
    EXPECT_EQ(read_be<std::int64_t>(in), -2);
}

// The two orders are opposite, which a round trip alone cannot show (DR-1).
TEST(ByteOrderTest, TheTwoOrdersAreMirrorImages) {
    const std::array<std::uint8_t, 4> bytes{0x11, 0x22, 0x33, 0x44};
    const std::span<const std::uint8_t> in{bytes};
    EXPECT_EQ(read_be<std::uint32_t>(in), 0x11223344U);
    EXPECT_EQ(read_le<std::uint32_t>(in), 0x44332211U);
}

// A two's-complement field decodes to the negative value it represents and not
// to its unsigned reading (DR-1).
TEST(ByteOrderTest, DecodesTwosComplementNegatives) {
    const std::array<std::uint8_t, 2> bytes{0xFF, 0xFE};
    const std::span<const std::uint8_t> in{bytes};
    EXPECT_EQ(read_be<std::int16_t>(in), -2);
    EXPECT_EQ(read_be<std::uint16_t>(in), 0xFFFEU);
}

// Real bytes from the two protocols that the family speaks (DR-1).
TEST(ByteOrderTest, MatchesKnownWireVectors) {
    // NTP, RFC 4330 section 4, is big-endian throughout. Byte 0 is LI/VN/Mode,
    // byte 1 is the stratum, offset 24 is the originate timestamp and offset 40
    // is the transmit timestamp. These are the offsets that pico-time reads.
    std::array<std::uint8_t, 48> ntp{};
    ntp[0] = 0x1C;  // LI 0, VN 3, Mode 4 (server)
    ntp[1] = 0x02;  // stratum 2
    ntp[24] = 0xE7;
    ntp[25] = 0xE3;
    ntp[26] = 0x4F;
    ntp[27] = 0x7F;
    ntp[40] = 0xE7;
    ntp[41] = 0xE3;
    ntp[42] = 0x4F;
    ntp[43] = 0x80;
    const std::span<const std::uint8_t, 48> ntp_view{ntp};
    // The reads are hoisted out of the macro calls, because EXPECT_EQ would
    // read the comma in a template argument list as a macro argument separator.
    const auto transmit = read_be_at<40, std::uint32_t>(ntp_view);
    const auto originate = read_be_at<24, std::uint32_t>(ntp_view);
    EXPECT_EQ(transmit, 0xE7E34F80U);
    EXPECT_EQ(originate, 0xE7E34F7FU);
    EXPECT_EQ(ntp_view[1], 0x02U);

    // SHTP, 1000-3535 section 2.2.1, has a 4-byte header whose first field is a
    // little-endian 16-bit length. Bit 15 is the continuation flag, so the raw
    // word and the length differ. Channel and sequence are plain bytes.
    const std::array<std::uint8_t, 4> shtp{0x05, 0x80, 0x02, 0x07};
    const std::span<const std::uint8_t, 4> shtp_view{shtp};
    const auto raw = read_le_at<0, std::uint16_t>(shtp_view);
    EXPECT_EQ(raw, 0x8005U);
    EXPECT_EQ(raw & 0x7FFFU, 5U);
    EXPECT_NE(raw & 0x8000U, 0U);
    EXPECT_EQ(shtp_view[2], 0x02U);
    EXPECT_EQ(shtp_view[3], 0x07U);
}

// The compile-time bound is a constraint, so an overrunning offset does not
// compile (DR-2). It is not a runtime check, and this test shows that.
TEST(ByteOrderTest, AnOverrunningOffsetIsIllFormed) {
    // A field ending exactly at the end of the buffer fits.
    static_assert(ReadableBe<4, std::uint32_t, 8>);
    static_assert(ReadableLe<4, std::uint32_t, 8>);
    static_assert(WritableBe<4, std::uint32_t, 8>);
    static_assert(WritableLe<4, std::uint32_t, 8>);

    // One byte past the end must not compile.
    static_assert(!ReadableBe<5, std::uint32_t, 8>);
    static_assert(!ReadableLe<5, std::uint32_t, 8>);
    static_assert(!WritableBe<5, std::uint32_t, 8>);
    static_assert(!WritableLe<5, std::uint32_t, 8>);

    // A field wider than the whole buffer must not compile.
    static_assert(!ReadableBe<0, std::uint64_t, 4>);
    static_assert(!ReadableLe<0, std::uint64_t, 4>);
    static_assert(!WritableBe<0, std::uint64_t, 4>);
    static_assert(!WritableLe<0, std::uint64_t, 4>);

    // An offset that went negative in compile-time arithmetic is a huge
    // unsigned value, and Offset + sizeof(T) wraps to a small one. It must not
    // compile either.
    constexpr std::size_t WRAPPED = SIZE_MAX - 1;
    static_assert(!ReadableBe<WRAPPED, std::uint32_t, 8>);
    static_assert(!ReadableLe<WRAPPED, std::uint32_t, 8>);
    static_assert(!WritableBe<WRAPPED, std::uint32_t, 8>);
    static_assert(!WritableLe<WRAPPED, std::uint32_t, 8>);

    // A dynamic-extent span carries no size that the compiler can check, so it
    // must not bind here at all. Otherwise the bound would not be a bound.
    static_assert(!ReadableBe<0, std::uint32_t, std::dynamic_extent>);
    static_assert(!ReadableLe<0, std::uint32_t, std::dynamic_extent>);
    static_assert(!WritableBe<0, std::uint32_t, std::dynamic_extent>);
    static_assert(!WritableLe<0, std::uint32_t, std::dynamic_extent>);
    SUCCEED();
}

// Runtime offsets are unchecked (DR-3). The caller must keep them in range.
TEST(ByteOrderTest, RuntimeOffsetReadsTheNamedField) {
    std::array<std::uint8_t, 8> buffer{};
    const std::span<std::uint8_t> out{buffer};
    write_be<std::uint16_t>(out, 0xAABB, 3);
    EXPECT_EQ(read_be<std::uint16_t>(std::span<const std::uint8_t>{buffer}, 3),
              0xAABBU);
    EXPECT_EQ(buffer[3], 0xAAU);
    EXPECT_EQ(buffer[4], 0xBBU);
    // Bytes outside the field are untouched.
    EXPECT_EQ(buffer[2], 0U);
    EXPECT_EQ(buffer[5], 0U);

    // The little-endian family does its own offset arithmetic.
    std::array<std::uint8_t, 8> le_buffer{};
    const std::span<std::uint8_t> le_out{le_buffer};
    write_le<std::uint16_t>(le_out, 0xAABB, 3);
    EXPECT_EQ(
        read_le<std::uint16_t>(std::span<const std::uint8_t>{le_buffer}, 3),
        0xAABBU);
    EXPECT_EQ(le_buffer[3], 0xBBU);
    EXPECT_EQ(le_buffer[4], 0xAAU);
    EXPECT_EQ(le_buffer[2], 0U);
    EXPECT_EQ(le_buffer[5], 0U);
}

// Every accessor is constexpr, so a wire layout can be decoded at compile time
// (DR-4). An accessor that is not constexpr fails this test at build time.
TEST(ByteOrderTest, DecodesAtCompileTime) {
    static constexpr std::array<std::uint8_t, 4> BYTES{0xDE, 0xAD, 0xBE, 0xEF};
    static_assert(read_be<std::uint32_t>(
                      std::span<const std::uint8_t>{BYTES}) == 0xDEADBEEFU);
    static_assert(read_le<std::uint32_t>(
                      std::span<const std::uint8_t>{BYTES}) == 0xEFBEADDEU);
    static_assert(read_be_at<0, std::uint16_t>(
                      std::span<const std::uint8_t, 4>{BYTES}) == 0xDEADU);
    static_assert(read_le_at<2, std::uint16_t>(
                      std::span<const std::uint8_t, 4>{BYTES}) == 0xEFBEU);

    // Encoding is constexpr too, so a constant table can be built at compile
    // time and not filled at boot.
    static constexpr auto ENCODED = [] {
        std::array<std::uint8_t, 4> out{};
        write_be<std::uint32_t>(std::span<std::uint8_t>{out}, 0xCAFEF00D);
        return out;
    }();
    static_assert(ENCODED[0] == 0xCA && ENCODED[3] == 0x0D);
    static constexpr auto ENCODED_LE = [] {
        std::array<std::uint8_t, 4> out{};
        write_le<std::uint32_t>(std::span<std::uint8_t>{out}, 0xCAFEF00D);
        return out;
    }();
    static_assert(ENCODED_LE[0] == 0x0D && ENCODED_LE[3] == 0xCA);
    static constexpr auto ENCODED_AT = [] {
        std::array<std::uint8_t, 4> out{};
        write_be_at<0, std::uint16_t>(std::span<std::uint8_t, 4>{out}, 0xCAFE);
        write_le_at<2, std::uint16_t>(std::span<std::uint8_t, 4>{out}, 0xF00D);
        return out;
    }();
    static_assert(ENCODED_AT[0] == 0xCA && ENCODED_AT[1] == 0xFE &&
                  ENCODED_AT[2] == 0x0D && ENCODED_AT[3] == 0xF0);
    SUCCEED();
}

// The compile-time family writes through the same bound (DR-2).
TEST(ByteOrderTest, CompileTimeOffsetWritesTheNamedField) {
    std::array<std::uint8_t, 8> buffer{};
    const std::span<std::uint8_t, 8> out{buffer};
    write_be_at<2, std::uint32_t>(out, 0x01020304);
    const auto readback =
        read_be_at<2, std::uint32_t>(std::span<const std::uint8_t, 8>{buffer});
    EXPECT_EQ(readback, 0x01020304U);
    write_le_at<2, std::uint32_t>(out, 0x01020304);
    EXPECT_EQ(buffer[2], 0x04U);
    EXPECT_EQ(buffer[5], 0x01U);
}

}  // namespace
