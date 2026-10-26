// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

/// \file fixed_string.hpp
/// A short string with its storage inline (BR-1, BR-2, BR-6).
///
/// Inline storage serves allocation-free paths. Append refuses an overlong
/// input; a caller wanting a marked truncation handles that refusal explicitly.
///
/// `assign` and `append` are `[[nodiscard]]` (BR-7, CXX-04.3): a refusal a
/// caller is free to drop is a guarantee available rather than made.

namespace pico_util {

/// A string of at most `N - 1` characters, always NUL-terminated.
///
/// The reserved byte is the terminator, so `c_str()` is always valid —
/// including after a truncating `assign` — and the type can be handed to a C
/// API without a copy.
template <std::size_t N>
class FixedString {
    static_assert(N > 1, "a FixedString needs room for a character and a NUL");

   public:
    /// Characters this string can hold, excluding the NUL.
    static constexpr std::size_t CAPACITY = N - 1;

    /// Construct an empty string.
    FixedString() = default;

    /// Construct from `text`, truncating to fit.
    ///
    /// Named rather than a constructor, because the name is the report. A
    /// converting constructor drops assign()'s truncation flag with nothing at
    /// the site saying so, and leaves no way to recover it: `size()`, `full()`
    /// and `view()` are all consistent with a value that fitted exactly, so a
    /// truncated string is indistinguishable from a whole one (BR-1). Use
    /// assign() where the loss must be handled rather than accepted.
    ///
    /// \param text characters to copy.
    /// \return a string holding at most `CAPACITY` characters of `text`.
    [[nodiscard]] static FixedString truncated(std::string_view text) noexcept {
        FixedString result;
        // The one deliberate discard in this library, at the one site whose
        // name already says the loss is accepted.
        static_cast<void>(result.assign(text));
        return result;
    }

    /// Replace the contents with `text`, truncating to fit.
    ///
    /// \param text characters to copy.
    /// \return false when `text` did not fit and was truncated.
    [[nodiscard]] bool assign(std::string_view text) noexcept {
        // Not named `truncated`: that is a member function now, and a local
        // of the same name shadows it under -Wshadow.
        const bool over_capacity = text.size() > CAPACITY;
        size_ = std::min(text.size(), CAPACITY);
        std::copy_n(text.begin(), static_cast<std::ptrdiff_t>(size_),
                    data_.begin());
        data_.at(size_) = '\0';
        return !over_capacity;
    }

    /// Append `text`.
    ///
    /// All-or-nothing (BR-2): an append that would not fit leaves the string
    /// byte-for-byte as it was. A partial append would produce a message that
    /// looks complete and is not.
    ///
    /// \param text characters to append.
    /// \return false when the result would not fit, the string unchanged.
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

    /// \return the contents, excluding the NUL.
    ///
    /// The view aliases this object's own storage, so it is **invalidated by
    /// the next `assign`, `append` or `clear`**, and it dangles if this string
    /// is destroyed. Copy it if it must outlive either.
    /// BR-13: aliases this object's storage rather than copying, and is
    /// invalidated by the next assign(), append() or clear().
    [[nodiscard]] std::string_view view() const noexcept {
        return {data_.data(), size_};
    }

    /// \return a NUL-terminated pointer to the contents, for a C API. Always
    ///         terminated, including after truncation.
    [[nodiscard]] const char* c_str() const noexcept { return data_.data(); }

    /// \return characters held, excluding the NUL. At most `CAPACITY`.
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    /// \return whether the string holds no characters.
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

    /// \return whether a further append of one character would be refused.
    [[nodiscard]] bool full() const noexcept { return size_ == CAPACITY; }

    /// \return `N - 1`.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return CAPACITY;
    }

    /// Discard the contents. Capacity is fixed and unaffected.
    void clear() noexcept {
        size_ = 0;
        data_.at(0) = '\0';
    }

   private:
    std::array<char, N> data_{};
    std::size_t size_ = 0;
};

}  // namespace pico_util
