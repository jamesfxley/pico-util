// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

/// \file byte_order.hpp
/// Big- and little-endian integer access over byte spans (DR-1..DR-4).
/// Static offsets are bounds-checked (DR-2). Runtime offsets are not (DR-3).
/// Every accessor is constexpr (DR-4).
/// Shifts use unsigned values and amounts. uint8_t promotes to signed int,
/// and literal shift amounts are signed. Neither is visible at the call site.

namespace pico_util {

/// Signed or unsigned integers of 2, 4 or 8 bytes, excluding bool and
/// characters.
template <typename T>
concept WireInteger =
    std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool> &&
    (sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8);

/// Read a big-endian `T` at a runtime `offset`.
///
/// \param bytes buffer to read from. It is not bounds-checked: `offset +
///        sizeof(T)` must lie within it (DR-3).
/// \param offset byte position of the field's most significant byte.
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

/// Write `value` big-endian with its most significant byte at `offset`.
/// The buffer is not bounds-checked, as for read_be().
template <WireInteger T>
constexpr void write_be(std::span<std::uint8_t> bytes, T value,
                        std::size_t offset = 0) {
    using Raw = std::make_unsigned_t<T>;
    const auto raw = static_cast<Raw>(value);
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        // std::size_t is already unsigned. On a 32-bit target it is `unsigned
        // int`, so a cast to that type trips -Wuseless-cast, which the host
        // build, where size_t is wider, does not report.
        const std::size_t shift = (sizeof(T) - 1 - i) * 8U;
        bytes[offset + i] = static_cast<std::uint8_t>(
            static_cast<Raw>(raw >> shift) & static_cast<Raw>(0xFFU));
    }
}

/// Write `value` little-endian with its least significant byte at `offset`.
/// The buffer is not bounds-checked, as for read_be().
template <WireInteger T>
constexpr void write_le(std::span<std::uint8_t> bytes, T value,
                        std::size_t offset = 0) {
    using Raw = std::make_unsigned_t<T>;
    const auto raw = static_cast<Raw>(value);
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        // std::size_t is already unsigned, as in write_be().
        const std::size_t shift = i * 8U;
        bytes[offset + i] = static_cast<std::uint8_t>(
            static_cast<Raw>(raw >> shift) & static_cast<Raw>(0xFFU));
    }
}

/// Whether a field at a static byte offset fits a static extent (DR-2).
/// Extent must be static. Subtraction avoids Offset + sizeof(T) wrapping.
template <typename T, std::size_t Offset, std::size_t Extent>
concept FieldFits = WireInteger<T> && Extent != std::dynamic_extent &&
                    Offset <= Extent && sizeof(T) <= Extent - Offset;

/// Read a big-endian `T` at a type-checked byte offset.
template <std::size_t Offset, typename T, std::size_t Extent>
    requires FieldFits<T, Offset, Extent>
[[nodiscard]] constexpr T read_be_at(
    std::span<const std::uint8_t, Extent> bytes) {
    return read_be<T>(bytes, Offset);
}

/// Read a little-endian `T` at a type-checked byte offset.
template <std::size_t Offset, typename T, std::size_t Extent>
    requires FieldFits<T, Offset, Extent>
[[nodiscard]] constexpr T read_le_at(
    std::span<const std::uint8_t, Extent> bytes) {
    return read_le<T>(bytes, Offset);
}

/// Write `value` big-endian at a type-checked byte offset.
template <std::size_t Offset, typename T, std::size_t Extent>
    requires FieldFits<T, Offset, Extent>
constexpr void write_be_at(std::span<std::uint8_t, Extent> bytes, T value) {
    write_be<T>(bytes, value, Offset);
}

/// Write `value` little-endian at a type-checked byte offset.
template <std::size_t Offset, typename T, std::size_t Extent>
    requires FieldFits<T, Offset, Extent>
constexpr void write_le_at(std::span<std::uint8_t, Extent> bytes, T value) {
    write_le<T>(bytes, value, Offset);
}

}  // namespace pico_util
