// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

/// \file fixed_string.hpp
/// An inline string with reported capacity failures (BR-1, BR-2, BR-6).
/// Assignment and append results cannot be silently discarded (BR-7).

namespace pico_util {

/// At most `N - 1` characters, NUL-terminated even after truncation.
template <std::size_t N>
class FixedString {
    static_assert(N > 1, "a FixedString needs room for a character and a NUL");

   public:
    /// Characters this string can hold, excluding the NUL.
    static constexpr std::size_t CAPACITY = N - 1;

    /// Construct an empty string.
    FixedString() = default;

    /// Construct from `text`, truncating to fit by explicit choice (BR-1).
    /// Use assign() where the caller must handle the loss.
    [[nodiscard]] static FixedString truncated(std::string_view text) noexcept {
        FixedString result;
        // The only deliberate discard in the library. The function name states
        // that the loss is accepted.
        static_cast<void>(result.assign(text));
        return result;
    }

    /// Replace with `text`, truncating to fit. Return false if truncated.
    [[nodiscard]] bool assign(std::string_view text) noexcept {
        // A local named `truncated` would shadow the member function under
        // -Wshadow.
        const bool over_capacity = text.size() > CAPACITY;
        size_ = std::min(text.size(), CAPACITY);
        std::copy_n(text.begin(), static_cast<std::ptrdiff_t>(size_),
                    data_.begin());
        data_.at(size_) = '\0';
        return !over_capacity;
    }

    /// Append `text` (BR-2).
    /// If it will not fit, return false and leave the string unchanged.
    [[nodiscard]] bool append(std::string_view text) noexcept {
        if (size_ + text.size() > CAPACITY) {
            return false;
        }
        std::copy_n(text.begin(), static_cast<std::ptrdiff_t>(text.size()),
                    data_.begin() + static_cast<std::ptrdiff_t>(size_));
        size_ += text.size();
        data_.at(size_) = '\0';
        return true;
    }

    /// \return the contents without the NUL, aliasing this object's storage.
    /// assign(), append() and clear() invalidate it. Copy it if it must outlive
    /// those calls or destruction (BR-13).
    [[nodiscard]] std::string_view view() const noexcept {
        return {data_.data(), size_};
    }

    /// \return a NUL-terminated pointer to the contents for a C API.
    [[nodiscard]] const char* c_str() const noexcept { return data_.data(); }

    /// \return characters held, excluding the NUL. At most `CAPACITY`.
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    /// \return whether the string holds no characters.
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

    /// \return whether an append of one character would fail.
    [[nodiscard]] bool full() const noexcept { return size_ == CAPACITY; }

    /// \return `N - 1`.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return CAPACITY;
    }

    /// Discard the contents. The capacity is unchanged.
    void clear() noexcept {
        size_ = 0;
        data_.at(0) = '\0';
    }

   private:
    std::array<char, N> data_{};
    std::size_t size_ = 0;
};

}  // namespace pico_util
