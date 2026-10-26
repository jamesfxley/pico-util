// SPDX-License-Identifier: MIT
// BR-16: FlatMap resets a value where it lives, so no whole-value temporary
// reaches the stack. Compiled with -fstack-usage; flat_map_stack.cmake holds
// every frame here far below the value's size. The value is shaped like
// pico-http's HTTPResponse: a member initialised through a function call is
// what keeps a temporary alive at -O2, where a plain zero-initialised array is
// folded into the slot and would show nothing.
#include <array>
#include <cstdint>

#include "pico_util/containers/fixed_string.hpp"
#include "pico_util/containers/flat_map.hpp"

namespace {

struct Wide {
    std::array<std::uint8_t, 4096> body{};
    pico_util::FixedString<33> text =
        pico_util::FixedString<33>::truncated("OK");
};

using Map = pico_util::FlatMap<int, Wide, 4>;

bool try_emplace(Map& map, int key) { return map.try_emplace(key) != nullptr; }

bool erase(Map& map, int key) { return map.erase(key); }

void clear(Map& map) { map.clear(); }

}  // namespace

// Called through volatile pointers, so each function is emitted with its own
// frame rather than inlined into this one. The object is compiled, never
// linked.
int main() {
    static Map map;
    bool (*volatile emplace_fn)(Map&, int) = &try_emplace;
    bool (*volatile erase_fn)(Map&, int) = &erase;
    void (*volatile clear_fn)(Map&) = &clear;
    const bool emplaced = emplace_fn(map, 1);
    const bool erased = erase_fn(map, 1);
    clear_fn(map);
    return emplaced && erased ? 0 : 1;
}
