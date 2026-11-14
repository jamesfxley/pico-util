// SPDX-License-Identifier: MIT
// Result<T, E> (FR-1..FR-4, FR-7, FR-9, FR-11). The container is generic and
// the error taxonomy stays in the library that owns the failures, so these
// tests instantiate two unrelated error types in one translation unit.
#include "pico_util/result/result.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>

using pico_util::Result;

namespace {

// Both error enums use zero for success, as ok() expects.
enum class BusError : std::uint8_t { NONE = 0, TIMEOUT, NACK };
enum class ParseError : std::uint8_t { NONE = 0, TRUNCATED, BAD_MAGIC };

// Implicit constructors allow unnamed Result returns (FR-2).
constexpr Result<int, BusError> read_register(bool succeed) {
    if (!succeed) {
        return BusError::TIMEOUT;
    }
    return 7;
}

constexpr Result<void, ParseError> validate(bool succeed) {
    if (!succeed) {
        return ParseError::BAD_MAGIC;
    }
    return {};
}

TEST(ResultTest, HoldsAValueOrAnErrorNeverBoth) {
    const Result<int, BusError> good = 42;
    EXPECT_TRUE(good.ok());
    EXPECT_TRUE(static_cast<bool>(good));
    EXPECT_EQ(good.error(), BusError::NONE);
    EXPECT_EQ(good.value(), 42);

    const Result<int, BusError> bad = BusError::NACK;
    EXPECT_FALSE(bad.ok());
    EXPECT_FALSE(static_cast<bool>(bad));
    EXPECT_EQ(bad.error(), BusError::NACK);
}

// The container costs the value plus a flat error code and stays trivially
// copyable, so it crosses a seam without allocation (FR-1, FR-8).
TEST(ResultTest, IsTriviallyCopyableAndCheap) {
    static_assert(std::is_trivially_copyable_v<Result<int, BusError>>);
    static_assert(std::is_trivially_copyable_v<Result<void, BusError>>);
    static_assert(sizeof(Result<int, BusError>) <= 2 * sizeof(int));
    static_assert(sizeof(Result<void, BusError>) == sizeof(BusError));
    SUCCEED();
}

// Two unrelated error taxonomies coexist in one translation unit (FR-1).
TEST(ResultTest, ParameterisedOnTheErrorType) {
    const Result<int, BusError> bus = BusError::TIMEOUT;
    const Result<int, ParseError> parse = ParseError::TRUNCATED;
    EXPECT_EQ(bus.error(), BusError::TIMEOUT);
    EXPECT_EQ(parse.error(), ParseError::TRUNCATED);
    static_assert(
        !std::is_same_v<decltype(bus)::ErrorType, decltype(parse)::ErrorType>);
}

TEST(ResultTest, ReturnsAValueOrAnErrorWithoutNamingResult) {
    const auto good = read_register(true);
    EXPECT_TRUE(good.ok());
    EXPECT_EQ(good.value(), 7);

    const auto bad = read_register(false);
    EXPECT_FALSE(bad.ok());
    EXPECT_EQ(bad.error(), BusError::TIMEOUT);
}

// A valueless Result defaults to success (FR-3).
TEST(ResultTest, VoidResultDefaultsToSuccess) {
    const Result<void, ParseError> fresh;
    EXPECT_TRUE(fresh.ok());
    EXPECT_EQ(fresh.error(), ParseError::NONE);

    EXPECT_TRUE(validate(true).ok());
    EXPECT_FALSE(validate(false).ok());
    EXPECT_EQ(validate(false).error(), ParseError::BAD_MAGIC);
}

// The fallback is the caller's and not a meaningless default-constructed value
// (FR-4).
TEST(ResultTest, ValueOrReturnsTheFallbackOnAnError) {
    const Result<int, BusError> bad = BusError::TIMEOUT;
    EXPECT_EQ(bad.value_or(-1), -1);

    const Result<int, BusError> good = 5;
    EXPECT_EQ(good.value_or(-1), 5);

    // A default-constructed int is 0, which is a plausible reading, so the
    // fallback has to differ from it for this test to mean anything.
    EXPECT_NE(bad.value_or(-1), 0);
}

// The whole type works in a constant expression, so a table of results can be
// built without running anything (FR-1..FR-4).
TEST(ResultTest, WorksInAConstantExpression) {
    static_assert(read_register(true).ok());
    static_assert(read_register(true).value() == 7);
    static_assert(!read_register(false).ok());
    static_assert(read_register(false).error() == BusError::TIMEOUT);
    static_assert(read_register(false).value_or(-1) == -1);
    static_assert(read_register(true).value_or(-1) == 7);
    static_assert(validate(true).ok());
    SUCCEED();
}

// An integral E can carry errno or vendor status codes (FR-1).
TEST(ResultTest, AcceptsAnIntegralErrorType) {
    // T does not convert to or from int. Otherwise an argument could
    // silently select the error constructor. Compile-failure probes cover that
    // (FR-1).
    struct Reading {
        int raw;
    };

    const Result<Reading, int> failed = 3;
    EXPECT_FALSE(failed.ok());
    EXPECT_EQ(failed.error(), 3);

    const Result<Reading, int> succeeded = Reading{7};
    EXPECT_TRUE(succeeded.ok());
    EXPECT_EQ(succeeded.value().raw, 7);
}

// Whether Result<int, E> can be named. This is a concept because a requirement
// on a type that no template parameter reaches is a hard error and not false.
template <typename E>
concept NamesAResult = requires { typename pico_util::Result<int, E>; };

// Enums and integers satisfy ErrorCode (FR-7). Constraints are testable here.
// Convertible pairs fail by static_assert and need a compile-failure probe
// (FR-1).
TEST(ResultTest, ErrorCodeRefusesAStruct) {
    struct NotAnErrorCode {
        int code;
    };

    static_assert(pico_util::ErrorCode<BusError>);
    static_assert(pico_util::ErrorCode<int>);
    static_assert(!pico_util::ErrorCode<NotAnErrorCode>);
    static_assert(!pico_util::ErrorCode<float>);
    // Result itself carries the constraint, so naming one fails to compile.
    static_assert(!NamesAResult<NotAnErrorCode>);
    SUCCEED();
}

// An errored value read asserts only without NDEBUG (FR-9).
// Release skips this test because it removes that assertion (FR-9).
TEST(ResultTest, ReadingTheValueOfAFailureIsRefused) {
#ifdef NDEBUG
    GTEST_SKIP() << "the refusal is an assert, which NDEBUG removes";
#else
    const Result<int, BusError> failed = BusError::TIMEOUT;
    EXPECT_DEATH(static_cast<void>(failed.value()), "ok\\(\\)");
#endif
}

// An E{} failure would fabricate success with no computed value (FR-11).
// Result<void, E> accepts E{} to propagate success.
TEST(ResultTest, AFailureFromTheSuccessCodeIsRefused) {
#ifdef NDEBUG
    GTEST_SKIP() << "the refusal is an assert, which NDEBUG removes";
#else
    // The alias is needed because the comma in the template argument list would
    // split the macro's arguments.
    using IntResult = Result<int, BusError>;
    EXPECT_DEATH(
        {
            const IntResult fabricated = BusError::NONE;
            static_cast<void>(fabricated);
        },
        "a failure constructed from E\\{\\} is a success holding no value");
#endif
    const Result<void, BusError> propagated = BusError::NONE;
    EXPECT_TRUE(propagated.ok());
}

// Identical or convertible T/E fail to compile in
// pico_util_refuses_result_same_type and
// pico_util_refuses_result_convertible_types (FR-1).

}  // namespace
