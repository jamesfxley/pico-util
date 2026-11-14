// SPDX-License-Identifier: MIT
// An empty readable heap must still report measuring() (MR-8).
// Only a bare main() avoids GoogleTest's allocations and models zero-heap
// firmware (MR-1).
#include "pico_util/heap/heap_watermark.hpp"

int main() {
    // A pre-main allocation invalidates the empty-heap premise.
    if (pico_util::HeapWatermark::measuring() &&
        pico_util::HeapWatermark::in_use_bytes() != 0U) {
        return 2;
    }
    pico_util::HeapWatermark watermark;
    watermark.arm();
    // Armed over an empty heap, the reading is 0, which does not show whether
    // the heap can be read. measuring() must still report that it can.
    const bool want = pico_util::detail::HEAP_READABLE;
    return pico_util::HeapWatermark::measuring() == want ? 0 : 1;
}
