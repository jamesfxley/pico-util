// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

/// \file flat_map.hpp
/// A fixed-capacity map over a linear scan (BR-3, BR-4, BR-6).
///
/// Three repositories had written this shape: pico-sh2's report-quality
/// override table, pico-http's pending-response pool, and pico-bno085's desired
/// sensor configuration, the last written inline in a driver source rather than
/// as a container at all. All three take a caller-supplied key, scan for it,
/// and refuse a new one at capacity.
///
/// **An entry never moves.** A slot is claimed on insert and released on erase,
/// and nothing relocates it in between, so a pointer or reference obtained from
/// `find` or `try_emplace` stays valid until that entry is itself erased.
///
/// The alternative — packing live entries at the front and filling an erase's
/// hole with the last one — makes iteration cost `size()` rather than `N` and
/// lets `entries()` be a contiguous span. Both are worth little at these
/// capacities, and they are bought with a contract in which erasing one key
/// invalidates a pointer to an unrelated one. Two of the three tables this
/// replaces already used a per-slot flag for exactly that reason: one hands a
/// pointer to a caller and holds it across a callback, and a value there is
/// kilobytes wide, so relocating it on an unrelated erase would be both a
/// dangling pointer and a memcpy on the hot path.
///
/// A linear scan is the right structure at these capacities. The tables this
/// replaces hold single-digit to low-double-digit entries, where a scan beats a
/// hash on both code size and constant factor, and it keeps the container free
/// of any hashing requirement on `K`.
///
/// Not for a key the container mints itself. A generation-tagged slot allocator
/// — pico-http's connection table — makes a stale handle fail to resolve, which
/// is a different contract, and folding it in here would drag generation
/// semantics into a map that has no business with them.

namespace pico_util {

/// A map of at most `N` entries from `K` to `V`, searched by linear scan.
///
/// Entry order is not part of the contract: a released slot is reused by a
/// later insert, so entries appear in claim order rather than insertion order.
/// Iterate for the set of entries, never for their order.
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

    /// Insert `key`, or replace the value of one already present.
    ///
    /// \param key the key to store under.
    /// \param value the value to store.
    /// \return the stored value, or nullptr when `key` is new and the map is
    ///         full. Refused, never evicted: an existing entry is never
    ///         displaced to make room. `[[nodiscard]]` because dropping that
    ///         refusal is a silently absent entry (BR-7, CXX-04.3).
    [[nodiscard]] V* insert_or_assign(const K& key, const V& value) {
        V* slot = try_emplace(key);
        if (slot != nullptr) {
            *slot = value;
        }
        return slot;
    }

    /// The slot for `key`, inserting a value-initialised one if absent.
    ///
    /// For a `V` too large or too awkward to pass by value.
    ///
    /// \param key the key to find or insert.
    /// \return the slot, or nullptr when `key` is new and the map is full.
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

    /// \param key the key to look up.
    /// \return the value mapped to `key`, or nullptr when it is absent. The
    ///         pointer stays valid until that entry is itself erased, whatever
    ///         is inserted or erased in between (BR-4).
    [[nodiscard]] V* find(const K& key) noexcept {
        for (std::size_t i = 0; i < N; ++i) {
            if (used_[i] && entries_[i].key == key) {
                return &entries_[i].value;
            }
        }
        return nullptr;
    }

    /// \param key the key to look up.
    /// \return the value mapped to `key`, or nullptr when it is absent.
    [[nodiscard]] const V* find(const K& key) const noexcept {
        for (std::size_t i = 0; i < N; ++i) {
            if (used_[i] && entries_[i].key == key) {
                return &entries_[i].value;
            }
        }
        return nullptr;
    }

    /// \param key the key to look up.
    /// \return whether `key` is present.
    [[nodiscard]] bool contains(const K& key) const noexcept {
        return find(key) != nullptr;
    }

    /// \param key the key to look up.
    /// \param fallback returned when `key` is absent.
    /// \return the mapped value, or `fallback`.
    [[nodiscard]] V value_or(const K& key, V fallback) const {
        const V* found = find(key);
        return found != nullptr ? *found : fallback;
    }

    /// Remove `key`.
    ///
    /// Releases that key's slot and touches no other, so a pointer to any other
    /// entry stays valid. The value is reset rather than left behind, because a
    /// released slot holding the last value keeps whatever that value owns
    /// alive until some later insert happens to land on it.
    ///
    /// \param key the key to remove.
    /// \return whether it was present.
    /// BR-14: the released value is reset rather than left in place, so a
    /// dead slot keeps nothing the last value owned alive.
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

    /// Remove every entry. Capacity is fixed and unaffected.
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

    /// Call `visit` for each live entry, in no particular order.
    ///
    /// A walk rather than a span, because live entries are not contiguous: a
    /// slot released by `erase` stays where it is until a later insert reuses
    /// it. Returning `Visit::STOP` ends the walk, which is what makes a search
    /// or an early exit expressible without exposing the holes.
    ///
    /// Erasing the entry being visited is safe; inserting during a walk is not
    /// defined, because a released slot ahead of the cursor may be reused.
    ///
    /// The key is passed by const reference and the value by mutable one, as
    /// two parameters rather than one entry. Handing out a mutable `Entry&`
    /// would let a visitor rewrite a key, leaving two live slots under the same
    /// key — which nothing here can reconcile: `find` and `try_emplace` resolve
    /// to the first, `insert_or_assign` leaves the duplicate behind, and
    /// `erase` removes one of the two while decrementing `size_` once. BR-3
    /// would be false for the rest of the map's life. A small struct of
    /// references would say the same thing, but a type whose members are
    /// references is its own defect
    /// (`cppcoreguidelines-avoid-const-or-ref-data-members`); two parameters
    /// carry the constness with no such type to own.
    ///
    /// \param visit called with each live entry's key and value. Taken by
    ///        value, not by forwarding reference: it is invoked once per live
    ///        slot, so forwarding it would move from it on the first call.
    ///
    /// BR-15: the visited entry may be erased during the walk; inserting
    /// during one is undefined.
    template <typename F>
    void for_each(F visit) {
        for (std::size_t i = 0; i < N; ++i) {
            if (used_[i] && visit(std::as_const(entries_[i].key),
                                  entries_[i].value) == Visit::STOP) {
                return;
            }
        }
    }

    /// Call `visit` for each live entry, for reading. As above.
    ///
    /// \param visit called with each live entry's key and value, both const.
    ///        By value, as above.
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

    /// \return whether a new key would be refused.
    [[nodiscard]] bool full() const noexcept { return size_ == N; }

    /// \return `N`.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return N; }

   private:
    // BR-16. Resets a slot's value where it lives. Assigning `V{}` or `Entry{}`
    // over it builds the whole value as a temporary first, and for a `V` that
    // is not trivially default-constructible the compiler keeps that temporary
    // on the stack at every optimisation level -- a value this map exists to
    // hold because it is kilobytes wide then costs its full size in stack on
    // every insert, erase and clear, and N times that in clear. Destroying and
    // constructing in place builds nothing but the value itself.
    static void reset_value(Entry& entry) noexcept {
        // Destroy-then-construct leaves a destroyed value behind if
        // construction throws, and there is no second slot to fall back on, so
        // a V whose default constructor may throw is refused rather than
        // documented as a precondition nobody can see.
        //
        // In this body rather than at class scope. A V declared inside the
        // class that holds the map -- a nested struct with default member
        // initialisers, which is the ordinary way to write one -- is not yet
        // usable for its initialisers while that class is incomplete, so a
        // class-scope check reads the trait as false and refuses correct code.
        // A member function body is instantiated on first use, by which point
        // the enclosing class is complete and the trait answers truthfully.
        static_assert(std::is_nothrow_default_constructible_v<V>,
                      "FlatMap resets values in place, which needs V's default "
                      "constructor to be noexcept");
        std::destroy_at(&entry.value);
        std::construct_at(&entry.value);
    }

    std::array<Entry, N> entries_{};
    /// Whether the slot at the same index holds a live entry. A per-slot flag
    /// rather than a count of a packed prefix: it is what lets a slot stay
    /// where it is.
    std::array<bool, N> used_{};
    std::size_t size_ = 0;
};

}  // namespace pico_util
