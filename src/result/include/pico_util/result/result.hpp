// SPDX-License-Identifier: MIT
#pragma once

#include <cassert>
#include <type_traits>
#include <utility>

/// \file result.hpp
/// Fallible returns using the caller's error type (FR-1..FR-4, FR-6..FR-9,
/// FR-11). The zero enumerator must mean success. Flat codes keep the type
/// cheap and trivially copyable whenever its value is (FR-8).

namespace pico_util {

/// Flat enums or integers, whose zero state means success.
template <typename E>
concept ErrorCode = std::is_enum_v<E> || std::is_integral_v<E>;  // FR-7

/// Holds a value or an error and diagnoses discarded results (FR-6).
/// Reading an errored value is a caller bug. value_or() is the checked read.
template <typename T, ErrorCode E>
class [[nodiscard]] Result {
    // The constructors must not claim the same argument (FR-1).
    static_assert(
        !std::is_convertible_v<T, E> && !std::is_convertible_v<E, T>,
        "Result<T, E> needs a T and an E that do not convert to each other: "
        "otherwise the value and error constructors compete for the same "
        "argument and overload resolution picks by conversion rank, not by "
        "intent. Use a dedicated error enum, which is what E is for.");

   public:
    /// The error type this Result reports failures with.
    using ErrorType = E;

    /// Construct a success implicitly from value (FR-2).
    // NOLINTNEXTLINE(google-explicit-constructor)
    constexpr Result(T value) : value_(std::move(value)), error_(E{}) {}

    /// Construct a failure implicitly from error.
    /// error must not be E{}. Debug builds assert this, as for value() (FR-9).
    // NOLINTNEXTLINE(google-explicit-constructor)
    constexpr Result(E error) : value_{}, error_(error) {
        assert(error != E{} &&
               "a failure constructed from E{} is a success holding no value");
    }

    /// \return whether the operation succeeded, meaning the error equals `E{}`.
    [[nodiscard]] constexpr bool ok() const { return error_ == E{}; }

    /// \return ok().
    constexpr explicit operator bool() const { return ok(); }

    /// \return the error, or `E{}` on success.
    [[nodiscard]] constexpr E error() const { return error_; }

    /// \return the held value. Reading it from an errored Result is a caller
    ///         bug. A debug build asserts it (FR-9). `NDEBUG` removes the
    ///         assertion, so a release build does not check.
    [[nodiscard]] constexpr const T& value() const {
        assert(ok());
        return value_;
    }

    /// \return the held value, mutable, with the same precondition.
    constexpr T& value() {
        assert(ok());
        return value_;
    }

    /// The value, or fallback on error, instead of a meaningless default
    /// (FR-4).
    // No \return: Doxygen 1.15 also files this under Result<void, E> and warns.
    [[nodiscard]] constexpr T value_or(T fallback) const {
        return ok() ? value_ : fallback;
    }

   private:
    T value_;
    E error_;
};

/// Holds success or an error without a value.
template <ErrorCode E>
class [[nodiscard]] Result<void, E> {
   public:
    /// The error type this Result reports failures with.
    using ErrorType = E;

    /// Construct the success state (FR-3).
    constexpr Result() : error_(E{}) {}

    /// Construct implicitly from an error code, including E{} for success.
    // NOLINTNEXTLINE(google-explicit-constructor)
    constexpr Result(E error) : error_(error) {}

    /// \return whether the operation succeeded.
    [[nodiscard]] constexpr bool ok() const { return error_ == E{}; }

    /// \return ok().
    constexpr explicit operator bool() const { return ok(); }

    /// \return the error, or `E{}` on success.
    [[nodiscard]] constexpr E error() const { return error_; }

   private:
    E error_;
};

}  // namespace pico_util
