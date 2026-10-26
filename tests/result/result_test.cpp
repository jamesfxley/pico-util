// SPDX-License-Identifier: MIT
// Result<T, E> (FR-1..FR-4, FR-7, FR-9, FR-11). The parameterisation is the
// point: the container is generic and the taxonomy stays in the library that
// owns the failures, so these tests instantiate two unrelated error types in
// one translation unit.
#include "pico_util/result/result.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>

using pico_util::Result;

namespace {

// Two unrelated taxonomies, as two consuming libraries would define them. Both
// have a zero enumerator meaning success, which is the convention ok() rests
// on.
enum class BusError : std::uint8_t { NONE = 0, TIMEOUT, NACK };
enum class ParseError : std::uint8_t { NONE = 0, TRUNCATED, BAD_MAGIC };

// FR-2: neither return site names Result. That is what the implicit
// constructors buy, and what a change making them explicit would break.
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

// FR-1, FR-8: the container costs the value plus a flat error code, and stays
// trivially copyable so it crosses a seam without allocation.
TEST(ResultTest, IsTriviallyCopyableAndCheap) {
    static_assert(std::is_trivially_copyable_v<Result<int, BusError>>);
    static_assert(std::is_trivially_copyable_v<Result<void, BusError>>);
    static_assert(sizeof(Result<int, BusError>) <= 2 * sizeof(int));
    static_assert(sizeof(Result<void, BusError>) == sizeof(BusError));
    SUCCEED();
}

// FR-1: the error type really is a parameter, not a fixed vocabulary. Two
// unrelated taxonomies instantiate side by side in one translation unit.
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

// FR-3: a Result carrying no value defaults to success, so `return {};` is the
// success path and needs no enumerator spelled out.
TEST(ResultTest, VoidResultDefaultsToSuccess) {
    const Result<void, ParseError> fresh;
    EXPECT_TRUE(fresh.ok());
    EXPECT_EQ(fresh.error(), ParseError::NONE);

    EXPECT_TRUE(validate(true).ok());
    EXPECT_FALSE(validate(false).ok());
    EXPECT_EQ(validate(false).error(), ParseError::BAD_MAGIC);
}

// FR-4: the fallback is the caller's, not a default-constructed value that
// means nothing.
TEST(ResultTest, ValueOrReturnsTheFallbackOnAnError) {
    const Result<int, BusError> bad = BusError::TIMEOUT;
    EXPECT_EQ(bad.value_or(-1), -1);

    const Result<int, BusError> good = 5;
    EXPECT_EQ(good.value_or(-1), 5);

    // A default-constructed int is 0, which is a plausible reading -- so the
    // fallback has to be distinguishable from it for this test to mean
    // anything.
    EXPECT_NE(bad.value_or(-1), 0);
}

// FR-1..FR-4 at compile time: the whole type works in a constant expression, so
// a table of results can be built without running anything.
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

// FR-1: an integral error type is admitted alongside an enum, because a
// consumer wrapping errno or a vendor status code has no enum to offer.
TEST(ResultTest, AcceptsAnIntegralErrorType) {
    // A plain int error, with a T that does not convert to or from it. An
    // arithmetic T here -- Result<std::uint8_t, int> -- is refused by
    // result.hpp's static_assert rather than silently resolving `= 3` to the
    // error constructor; the FR-1 note at the end of this file names that
    // refusal's test.
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

// Whether Result<int, E> can be named: a concept, because a requirement on a
// type no template parameter reaches is a hard error rather than false.
template <typename E>
concept NamesAResult = requires { typename pico_util::Result<int, E>; };

// FR-7: ErrorCode admits an enumeration and an integer and nothing else. The
// refusal is a concept, so it is observable without instantiating anything --
// unlike FR-1's convertible-pair refusal below, which is a static_assert
// inside the class and can only be seen by failing a build.
TEST(ResultTest, ErrorCodeRefusesAStruct) {
    struct NotAnErrorCode {
        int code;
    };

    static_assert(pico_util::ErrorCode<BusError>);
    static_assert(pico_util::ErrorCode<int>);
    static_assert(!pico_util::ErrorCode<NotAnErrorCode>);
    static_assert(!pico_util::ErrorCode<float>);
    // And Result itself carries the constraint, so naming one is refused.
    static_assert(!NamesAResult<NotAnErrorCode>);
    SUCCEED();
}

// FR-9: reading value() from an errored Result is a caller bug, asserted in a
// build with assertions enabled. A Release build removes the check, which FR-9
// says, so this is proven on the Debug leg and skips on the other.
TEST(ResultTest, ReadingTheValueOfAFailureIsRefused) {
#ifdef NDEBUG
    GTEST_SKIP() << "the refusal is an assert, which NDEBUG removes";
#else
    const Result<int, BusError> failed = BusError::TIMEOUT;
    EXPECT_DEATH(static_cast<void>(failed.value()), "ok\\(\\)");
#endif
}

// FR-11: a failure constructed from E{} is refused, because E{} is the success
// state and the result would be an ok() Result holding a value nobody
// computed. Result<void, E> is exempt and accepts it: there E{} is correct
// propagation of a success.
TEST(ResultTest, AFailureFromTheSuccessCodeIsRefused) {
#ifdef NDEBUG
    GTEST_SKIP() << "the refusal is an assert, which NDEBUG removes";
#else
    // An alias, because the comma in the template argument list would split
    // the macro's arguments.
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

// FR-1: T and E cannot be the same type, nor convertible to each other -- in
// the first case the value and error constructors have the same signature, and
// in the second they compete for the same argument and overload resolution
// picks by conversion rank rather than by intent. result.hpp refuses both with
// a static_assert naming the fix. Naming Result<E, E> or
// Result<std::uint8_t, int> here would fail this translation unit's build, so
// the refusals are the build-failure tests pico_util_refuses_result_same_type
// and pico_util_refuses_result_convertible_types.

}  // namespace
