// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

/// \file as_bytes.hpp
/// View text as transport bytes without copying (DR-5).

namespace pico_util {

// The cast below is sound only where std::uint8_t is a character type, as it is
// on every target this family builds for. A platform where it is not should
// fail here and not reinterpret through a type that might not alias.
static_assert(std::is_same_v<std::uint8_t, unsigned char>,
              "as_bytes relies on std::uint8_t being a character type, which "
              "is what permits it to alias the char objects behind a "
              "std::string_view");

/// View text as bytes without copying. An empty input gives an empty span.
/// The span borrows `text`'s storage and is valid only while that storage is.
[[nodiscard]] inline std::span<const std::uint8_t> as_bytes(
    std::string_view text) noexcept {
    if (text.empty()) {
        return {};
    }
    // `unsigned char` may alias any object, so this reinterpretation is
    // permitted although the check cannot tell it from an unsafe one. No
    // alternative avoids the cast: std::as_bytes yields std::byte, and a
    // transport that takes std::uint8_t does not accept a std::byte span.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

}  // namespace pico_util
