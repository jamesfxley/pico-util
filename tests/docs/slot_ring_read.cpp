// SPDX-License-Identifier: MIT
// The bounded slot-ring read from the usage guide.
#include <cstdint>
#include <pico_util/pico_util.hpp>
#include <pico_util/publish/published.hpp>

namespace doc_examples {
void use(std::int64_t offset);

pico_util::PublishedReadError slot_ring_read(
    const pico_util::Published<
        std::int64_t, pico_util::PublishStrategy::SLOT_RING_READER_MAY_PREEMPT>&
        clock) {
#include "slot_ring_read.inc"
    return pico_util::PublishedReadError::NONE;
}
}  // namespace doc_examples
