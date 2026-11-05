// SPDX-License-Identifier: MIT
// In-place resets avoid whole-value stack temporaries (BR-16).
// A function-call initialiser keeps temporaries visible at -O2. A zeroed array
// alone would fold into the slot. flat_map_stack.cmake checks frame sizes.
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

// Volatile calls emit separate frames without inlining. The object is not
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
