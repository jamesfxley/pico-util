// SPDX-License-Identifier: MIT
// The staged package links every module with the SDK. This image is not run.
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
