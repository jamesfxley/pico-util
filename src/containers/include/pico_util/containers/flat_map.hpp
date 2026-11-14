// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

/// \file flat_map.hpp
/// Fixed-capacity lookup by a caller-supplied key (BR-3, BR-4, BR-6).
/// Entries never move. Unrelated erases leave held pointers valid (BR-4).

namespace pico_util {

/// Up to `N` entries searched by linear scan, in unspecified slot order.
template <typename K, typename V, std::size_t N>
class FlatMap {
    static_assert(N > 0, "a FlatMap needs at least one slot");

   public:
    /// One key and its value, as stored.
    struct Entry {
        K key;    ///< The caller-supplied key.
        V value;  ///< The value mapped to it.
    };

    /// A visitor's verdict, for for_each().
    enum class Visit : std::uint8_t {
        CONTINUE,  ///< Carry on to the next entry.
        STOP,      ///< Stop the walk.
    };

    /// Entries this map holds when full.
    static constexpr std::size_t CAPACITY = N;

    /// Insert or replace. Return nullptr for a new key at capacity (BR-7).
    [[nodiscard]] V* insert_or_assign(const K& key, const V& value) {
        V* slot = try_emplace(key);
        if (slot != nullptr) {
            *slot = value;
        }
        return slot;
    }

    /// \return the existing or value-initialised slot.
    /// A new key at capacity returns nullptr.
    [[nodiscard]] V* try_emplace(const K& key) {
        if (V* existing = find(key); existing != nullptr) {
            return existing;
        }
        for (std::size_t i = 0; i < N; ++i) {
            if (!used_[i]) {
                entries_[i].key = key;
                reset_value(entries_[i]);
                used_[i] = true;
                ++size_;
                return &entries_[i].value;
            }
        }
        return nullptr;
    }

    /// \return the value or nullptr. Valid until this entry is erased (BR-4).
    [[nodiscard]] V* find(const K& key) noexcept {
        for (std::size_t i = 0; i < N; ++i) {
            if (used_[i] && entries_[i].key == key) {
                return &entries_[i].value;
            }
        }
        return nullptr;
    }

    /// \return the value mapped to `key`, or nullptr when absent.
    [[nodiscard]] const V* find(const K& key) const noexcept {
        for (std::size_t i = 0; i < N; ++i) {
            if (used_[i] && entries_[i].key == key) {
                return &entries_[i].value;
            }
        }
        return nullptr;
    }

    /// Whether `key` is present.
    [[nodiscard]] bool contains(const K& key) const noexcept {
        return find(key) != nullptr;
    }

    /// \return the mapped value, or `fallback` when absent.
    [[nodiscard]] V value_or(const K& key, V fallback) const {
        const V* found = find(key);
        return found != nullptr ? *found : fallback;
    }

    /// Remove `key` and reset its value. Return whether it was present (BR-14).
    bool erase(const K& key) {
        for (std::size_t i = 0; i < N; ++i) {
            if (used_[i] && entries_[i].key == key) {
                entries_[i].key = K{};
                reset_value(entries_[i]);
                used_[i] = false;
                --size_;
                return true;
            }
        }
        return false;
    }

    /// Remove every entry. The capacity is unchanged.
    void clear() {
        for (std::size_t i = 0; i < N; ++i) {
            if (used_[i]) {
                entries_[i].key = K{};
                reset_value(entries_[i]);
            }
        }
        used_ = {};
        size_ = 0;
    }

    /// Visit entries as (const key, mutable value), protecting keys (BR-3).
    /// STOP ends the walk. The visited entry may be erased. Insertion during
    /// a walk is undefined (BR-15).
    template <typename F>
    void for_each(F visit) {
        for (std::size_t i = 0; i < N; ++i) {
            if (used_[i] && visit(std::as_const(entries_[i].key),
                                  entries_[i].value) == Visit::STOP) {
                return;
            }
        }
    }

    /// Visit live entries as (const key, const value). STOP ends the walk.
    template <typename F>
    void for_each(F visit) const {
        for (std::size_t i = 0; i < N; ++i) {
            if (used_[i] &&
                visit(entries_[i].key, entries_[i].value) == Visit::STOP) {
                return;
            }
        }
    }

    /// \return the number of entries held.
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    /// \return whether the map holds nothing.
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

    /// \return whether inserting a new key would fail.
    [[nodiscard]] bool full() const noexcept { return size_ == N; }

    /// \return `N`.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return N; }

   private:
    // In-place construction avoids a V-sized stack temporary for a V that is
    // not trivially default-constructible, at every optimisation level (BR-16).
    static void reset_value(Entry& entry) noexcept {
        // A throw after destruction would leave no live value to restore.
        // The check waits for use: a nested V's default member initialisers are
        // not usable while its enclosing class is incomplete.
        static_assert(std::is_nothrow_default_constructible_v<V>,
                      "FlatMap resets values in place, which needs V's default "
                      "constructor to be noexcept");
        std::destroy_at(&entry.value);
        std::construct_at(&entry.value);
    }

    std::array<Entry, N> entries_{};
    /// Whether the slot at the same index holds a live entry.
    std::array<bool, N> used_{};
    std::size_t size_ = 0;
};

}  // namespace pico_util
