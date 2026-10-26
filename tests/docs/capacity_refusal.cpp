// SPDX-License-Identifier: MIT
// The three ways to meet a capacity refusal, over the names the guide uses.
#include <pico_util/containers/ring_buffer.hpp>
#include <pico_util/pico_util.hpp>
#include <string_view>

namespace doc_examples {
void capacity_refusal(
    std::string_view value,
    pico_util::RingBuffer<int, 4, pico_util::SingleContextCounter>& ring,
    int sample) {
#include "capacity_refusal.inc"
    static_cast<void>(label);
}
}  // namespace doc_examples
