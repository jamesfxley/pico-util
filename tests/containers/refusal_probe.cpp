// SPDX-License-Identifier: MIT
// Refusals of the containers (BR-5, BR-7, BR-16). With PROBE undefined this
// compiles and is part of every build, so the probe itself is sound; each PROBE
// value enables one statement the library promises to refuse.
#include "pico_util/containers/fixed_string.hpp"
#include "pico_util/containers/flat_map.hpp"
#include "pico_util/containers/ring_buffer.hpp"

namespace {

// A default constructor that is not noexcept.
struct MayThrow {
    MayThrow() noexcept(false) = default;
    int value = 0;
};

using Ring = pico_util::RingBuffer<int, 4, pico_util::SingleContextCounter>;

[[maybe_unused]] void containers_probe() {
#if defined(PROBE) && PROBE == 1
    pico_util::FixedString<8> text;
    text.assign("abc");  // BR-7
#elif defined(PROBE) && PROBE == 2
    pico_util::FixedString<8> text;
    text.append("abc");  // BR-7
#elif defined(PROBE) && PROBE == 3
    pico_util::FlatMap<int, int, 4> map;
    map.insert_or_assign(1, 1);  // BR-7
#elif defined(PROBE) && PROBE == 4
    pico_util::FlatMap<int, int, 4> map;
    map.try_emplace(1);  // BR-7
#elif defined(PROBE) && PROBE == 5
    Ring ring;
    ring.push(1);  // BR-7
#elif defined(PROBE) && PROBE == 6
    Ring ring;
    ring.acquire();  // BR-7
#elif defined(PROBE) && PROBE == 7
    // BR-16: a value whose default constructor may throw, once the map is used.
    pico_util::FlatMap<int, MayThrow, 4> throwing;
    (void)throwing.try_emplace(1);
#elif defined(PROBE) && PROBE == 8
    // BR-5: Counter has no default, so a ring naming none does not compile.
    pico_util::RingBuffer<int, 4> unnamed;
#else
    // The same statements, their results received: these must compile.
    pico_util::FixedString<8> text;
    (void)text.assign("abc");
    (void)text.append("abc");
    pico_util::FlatMap<int, int, 4> map;
    (void)map.insert_or_assign(1, 1);
    (void)map.try_emplace(2);
    Ring ring;
    (void)ring.push(1);
    (void)ring.acquire();
#endif
}

}  // namespace
