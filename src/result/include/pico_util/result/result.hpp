// SPDX-License-Identifier: MIT
#pragma once

#include <cassert>
#include <type_traits>
#include <utility>

/// \file result.hpp
/// A minimal expected-style return type for fallible operations (FR-1..FR-4,
/// FR-6..FR-9, FR-11).
///
/// No exceptions and no heap: every fallible operation returns a `Result` and
/// the caller inspects it. The error type is the caller's own, because the
/// taxonomy belongs to the library that owns the failures — a driver's bus
/// errors and a parser's malformed-input errors are not one vocabulary, and
/// forcing them into one here would make this header the place every library
/// has to edit to add an error code.
///
/// The convention `E{}` means success is what `ok()` rests on, so the error
/// enum's zero enumerator must be its "no error" one. A flat enum keeps a
/// `Result` no wider than its value, the error code and the padding between
/// them, and trivially copyable whenever the value is (FR-8), which is what
/// lets it cross a HAL boundary without allocation.

namespace pico_util {

/// The error types a Result carries: a flat enum or an integer.
///
/// Deliberately narrow. A rich error type would make `Result` expensive to copy
/// across the seams it exists to cross, and its value-initialised state is what
/// `ok()` compares against — which only means "no error" for a type whose zero
/// is the success state.
template <typename E>
concept ErrorCode = std::is_enum_v<E> || std::is_integral_v<E>;  // FR-7

/// Holds either a value or an error, never both.
///
/// Trivially copyable when `T` is. Reading `value()` from an errored `Result`
/// is a caller bug: the value is default-constructed and means nothing. It is
/// asserted in a debug build, and `value_or()` is the checked alternative where
/// either outcome is expected.
///
/// `[[nodiscard]]` at class scope (FR-6, CXX-07.4): discarding this type is the
/// loss of the only report a failed operation makes, and `result;` on its own
/// is a statement the compiler would otherwise accept without comment. A caller
/// that genuinely does not care casts to `void`, which says so at the site.
template <typename T, ErrorCode E>
class [[nodiscard]] Result {
    // Distinct is not enough: the two constructors below must not be able to
    // claim the same argument. With one type in both roles they have the same
    // signature. With merely *convertible* types -- T std::uint8_t and E int,
    // say, which ErrorCode permits -- they have different signatures and
    // overload resolution picks between them by conversion rank rather than by
    // intent, so `return 5;` from a function returning Result<std::uint8_t,
    // int> binds the error constructor as an exact match and yields a failure
    // carrying 5. That is a wrong value at compile time with no diagnostic
    // anywhere, and no constraint can fix it: whatever stops the error
    // constructor claiming an int also removes the only way to report an error.
    // Refusing the pair is the fix, and the message names it.
    static_assert(
        !std::is_convertible_v<T, E> && !std::is_convertible_v<E, T>,
        "Result<T, E> needs a T and an E that do not convert to each other: "
        "otherwise the value and error constructors compete for the same "
        "argument and overload resolution picks by conversion rank, not by "
        "intent. Use a dedicated error enum, which is what E is for.");

   public:
    /// The error type this Result reports failures with.
    using ErrorType = E;

    /// Construct a success holding `value`.
    ///
    /// Deliberately implicit (FR-2): the whole ergonomic case for this type is
    /// `return value;` from a function returning `Result<T, E>`. Marking it
    /// explicit would require a `Result{...}` wrapper at every return site in
    /// every consuming library. std::expected makes the same choice.
    // NOLINTNEXTLINE(google-explicit-constructor)
    constexpr Result(T value) : value_(std::move(value)), error_(E{}) {}

    /// Construct a failure carrying `error`, which must not be `E{}`. Implicit
    /// for the reason above.
    ///
    /// `E{}` is the success state, so passing it here yields an ok() Result
    /// holding a value-initialised `T` nobody computed -- a fabricated success
    /// that passes every later check. `return map_status(s);` whose mapping
    /// falls through to the zero enumerator is how it happens. Asserted in a
    /// debug build, as FR-9 is; Result<void, E> accepts `E{}`, because there it
    /// is correct propagation of a success.
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
    ///         bug; asserted in a debug build (FR-9). NDEBUG removes the
    ///         assertion, so a release build does not check this.
    [[nodiscard]] constexpr const T& value() const {
        assert(ok());
        return value_;
    }

    /// \return the held value, mutably, under the same precondition.
    constexpr T& value() {
        assert(ok());
        return value_;
    }

    /// The value, or `fallback` when this Result holds an error: the checked
    /// alternative to value(), returning the caller's fallback rather than a
    /// default-constructed value, which means nothing (FR-4).
    ///
    /// \param fallback returned when this Result holds an error.
    //
    // The return is stated in the brief rather than as `\return`: doxygen 1.15
    // also files this member under Result<void, E>, where a documented return
    // on a function it reads as returning void is a warning.
    [[nodiscard]] constexpr T value_or(T fallback) const {
        return ok() ? value_ : fallback;
    }

   private:
    T value_;
    E error_;
};

/// A Result for an operation with no value: success or an error, nothing more.
///
/// `[[nodiscard]]` for the reason the primary template gives, and more sharply:
/// with no value to use, the error is the entire return.
template <ErrorCode E>
class [[nodiscard]] Result<void, E> {
   public:
    /// The error type this Result reports failures with.
    using ErrorType = E;

    /// Construct the success state (FR-3).
    constexpr Result() : error_(E{}) {}

    /// Construct a failure carrying `error`. Implicit for the same reason as
    /// the primary template's constructors.
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
