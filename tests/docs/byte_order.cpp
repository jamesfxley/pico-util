// SPDX-License-Identifier: MIT
// The two byte-order families, over the buffers the usage guide names.
#include <array>
#include <cstddef>
#include <cstdint>
#include <pico_util/pico_util.hpp>
#include <span>

namespace doc_examples {
void byte_order(const std::array<std::uint8_t, 4>& bytes,
                std::span<const std::uint8_t> payload, std::size_t offset) {
#include "byte_order.inc"
    static_cast<void>(length);
    static_cast<void>(field);
}
}  // namespace doc_examples
