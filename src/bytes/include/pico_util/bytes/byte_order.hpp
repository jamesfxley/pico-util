// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

/// \file byte_order.hpp
/// Big- and little-endian integer access over byte spans (DR-1..DR-4).
///
/// Every protocol in the family lays multi-byte fields out in one of the two
/// orders — SHTP and SH-2 are little-endian, NTP is big-endian — and each
/// repository had written its own shifts for the order it happened to need.
/// Both live here, in two families that differ in where the bounds check goes:
///
/// - `read_be_at<Offset, T>` and friends take a **static-extent** span and
///   constrain the offset against that extent, so an over-run is a build error
///   (DR-2). Prefer this spelling wherever the buffer's size is known at
///   compile time, which for a fixed protocol header it usually is.
/// - `read_be<T>(bytes, offset)` and friends take a **dynamic-extent** span and
///   do not bounds-check (DR-3). The caller's own length validation is the
///   precondition — a framing layer that has already checked the length should
///   not pay for a second check it cannot fail.
///
/// Every accessor is `constexpr` (DR-4), so a wire layout can be built or
/// decoded at compile time and a decoder test can be a `static_assert`.
///
/// Every shift below takes an unsigned operand and an unsigned shift amount.
/// `std::uint8_t` integer-promotes to signed `int`, so the natural spelling
/// `bytes[i] << 8` shifts a signed value, and an untyped literal shift amount
/// is signed too. Neither is wrong for the non-negative values on a wire, but
/// both are the shape the standard leaves implementation-defined for negatives,
/// and neither promotion is visible at the call site.

namespace pico_util {

/// The integer types these accessors carry: 2, 4 or 8 bytes wide, signed or
/// unsigned.
///
/// `bool` and the character types are excluded deliberately. A single byte has
/// no byte order, so reading one through an endian accessor says the opposite
/// of what it does, and `std::span<const std::uint8_t>` already indexes to a
/// byte.
template <typename T>
concept WireInteger =
    std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool> &&
    (sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8);

/// Read a big-endian `T` at a runtime `offset`.
///
/// \param bytes buffer to read from. Not bounds-checked: `offset + sizeof(T)`
///        must be within it, which is the caller's precondition to keep (DR-3).
/// \param offset byte position of the field's most significant byte.
/// \return the decoded value.
template <WireInteger T>
[[nodiscard]] constexpr T read_be(std::span<const std::uint8_t> bytes,
                                  std::size_t offset = 0) {
    using Raw = std::make_unsigned_t<T>;
    Raw value{};
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        value = static_cast<Raw>(static_cast<Raw>(value << 8U) |
                                 static_cast<Raw>(bytes[offset + i]));
    }
    // Well-defined in C++20: conversion to a signed type is modular, so a
    // two's-complement field decodes to the negative value it represents.
    return static_cast<T>(value);
}

/// Read a little-endian `T` at a runtime `offset`.
///
/// \param bytes buffer to read from. Not bounds-checked, as for read_be().
/// \param offset byte position of the field's least significant byte.
/// \return the decoded value.
template <WireInteger T>
[[nodiscard]] constexpr T read_le(std::span<const std::uint8_t> bytes,
                                  std::size_t offset = 0) {
    using Raw = std::make_unsigned_t<T>;
    Raw value{};
    for (std::size_t i = sizeof(T); i > 0; --i) {
        value = static_cast<Raw>(static_cast<Raw>(value << 8U) |
                                 static_cast<Raw>(bytes[offset + i - 1]));
    }
    return static_cast<T>(value);
}

/// Write `value` big-endian at a runtime `offset`.
///
/// \param bytes buffer to write into. Not bounds-checked, as for read_be().
/// \param value value to encode.
/// \param offset byte position of the field's most significant byte.
template <WireInteger T>
constexpr void write_be(std::span<std::uint8_t> bytes, T value,
                        std::size_t offset = 0) {
    using Raw = std::make_unsigned_t<T>;
    const auto raw = static_cast<Raw>(value);
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        // std::size_t, not a narrower unsigned: it is already unsigned, and
        // on a 32-bit target where size_t *is* unsigned int a cast to one
        // trips -Wuseless-cast -- which the host build, where size_t is
        // wider, does not see.
        const std::size_t shift = (sizeof(T) - 1 - i) * 8U;
        bytes[offset + i] = static_cast<std::uint8_t>(
            static_cast<Raw>(raw >> shift) & static_cast<Raw>(0xFFU));
    }
}

/// Write `value` little-endian at a runtime `offset`.
///
/// \param bytes buffer to write into. Not bounds-checked, as for read_be().
/// \param value value to encode.
/// \param offset byte position of the field's least significant byte.
template <WireInteger T>
constexpr void write_le(std::span<std::uint8_t> bytes, T value,
                        std::size_t offset = 0) {
    using Raw = std::make_unsigned_t<T>;
    const auto raw = static_cast<Raw>(value);
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        // Unsigned already; see the note in write_be().
        const std::size_t shift = i * 8U;
        bytes[offset + i] = static_cast<std::uint8_t>(
            static_cast<Raw>(raw >> shift) & static_cast<Raw>(0xFFU));
    }
}

/// Whether a field of type `T` at `Offset` fits a buffer of `Extent` bytes.
///
/// The constraint behind the compile-time-offset family (DR-2). `Extent` must
/// be static: a dynamic-extent span carries no size the compiler can check, so
/// binding one here would silently give a bound that is not a bound. The bound
/// is written without `Offset + sizeof(T)`, which wraps for an offset that went
/// negative in compile-time arithmetic and would then read as fitting.
template <typename T, std::size_t Offset, std::size_t Extent>
concept FieldFits = WireInteger<T> && Extent != std::dynamic_extent &&
                    Offset <= Extent && sizeof(T) <= Extent - Offset;

/// Read a big-endian `T` at a compile-time `Offset`, bounds-checked by the
/// type.
///
/// \param bytes buffer to read from; its extent is part of its type.
/// \return the decoded value.
template <std::size_t Offset, typename T, std::size_t Extent>
    requires FieldFits<T, Offset, Extent>
[[nodiscard]] constexpr T read_be_at(
    std::span<const std::uint8_t, Extent> bytes) {
    return read_be<T>(bytes, Offset);
}

/// Read a little-endian `T` at a compile-time `Offset`, bounds-checked by the
/// type.
///
/// \param bytes buffer to read from; its extent is part of its type.
/// \return the decoded value.
template <std::size_t Offset, typename T, std::size_t Extent>
    requires FieldFits<T, Offset, Extent>
[[nodiscard]] constexpr T read_le_at(
    std::span<const std::uint8_t, Extent> bytes) {
    return read_le<T>(bytes, Offset);
}

/// Write `value` big-endian at a compile-time `Offset`, bounds-checked by the
/// type.
///
/// \param bytes buffer to write into; its extent is part of its type.
/// \param value value to encode.
template <std::size_t Offset, typename T, std::size_t Extent>
    requires FieldFits<T, Offset, Extent>
constexpr void write_be_at(std::span<std::uint8_t, Extent> bytes, T value) {
    write_be<T>(bytes, value, Offset);
}

/// Write `value` little-endian at a compile-time `Offset`, bounds-checked by
/// the type.
///
/// \param bytes buffer to write into; its extent is part of its type.
/// \param value value to encode.
template <std::size_t Offset, typename T, std::size_t Extent>
    requires FieldFits<T, Offset, Extent>
constexpr void write_le_at(std::span<std::uint8_t, Extent> bytes, T value) {
    write_le<T>(bytes, value, Offset);
}

}  // namespace pico_util
