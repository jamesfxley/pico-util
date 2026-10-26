// SPDX-License-Identifier: MIT
// The installed package, consumed by a firmware program. It builds against the
// prefix `cmake --install` staged, links every exported module and the SDK's
// standard library, and is never run: a probe image has no board to run on.
#include <array>
#include <cstdint>
#include <span>

#include "pico/stdlib.h"
#include "pico_util/pico_util.hpp"

int main() {
    stdio_init_all();
    std::array<std::uint8_t, 8> buffer{};
    pico_util::write_le<std::uint16_t>(std::span<std::uint8_t>{buffer}, 0xBEEF,
                                       4);
    pico_util::HeapWatermark watermark;
    watermark.arm();
    return buffer[4] == 0xEF ? 0 : 1;
}
