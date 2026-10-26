// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

/// \file as_bytes.hpp
/// Viewing text as the bytes a transport speaks (DR-5).
///
/// Separate from byte_order.hpp so that a firmware parser including the wire
/// accessors does not also pull in the `<string_view>` header.
///
/// This header exists to hold one `reinterpret_cast` at one site. Every
/// repository that writes a `std::string_view` to a byte-oriented transport
/// needs the same conversion, and a cast copied into each of them is a cast
/// nobody reviews. Here it is reviewed once and suppressed once.

namespace pico_util {

// The aliasing argument below is only sound where std::uint8_t is a character
// type. It is on every target this family builds for, and a platform where it
// is not should fail here rather than reinterpret through a type that may not
// alias.
static_assert(std::is_same_v<std::uint8_t, unsigned char>,
              "as_bytes relies on std::uint8_t being a character type, which "
              "is what permits it to alias the char objects behind a "
              "std::string_view");

/// View text as bytes, without copying.
///
/// \param text characters to view. The returned span borrows them: it is valid
///        only while `text`'s own storage is, and never owns anything.
/// \return a span over the same bytes. Empty in, empty out — a default-
///         constructed `std::string_view` has a null `data()`, and the result
///         is an empty span rather than a span over a null pointer with a size.
[[nodiscard]] inline std::span<const std::uint8_t> as_bytes(
    std::string_view text) noexcept {
    if (text.empty()) {
        return {};
    }
    // `unsigned char` may alias any object, which is what makes this the one
    // reinterpretation the standard permits and the rule cannot distinguish
    // from the ones it is right to refuse. There is no formulation that avoids
    // it: std::as_bytes yields std::byte, and a std::byte span is not what a
    // transport speaking std::uint8_t accepts.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

}  // namespace pico_util
