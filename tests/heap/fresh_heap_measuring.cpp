// SPDX-License-Identifier: MIT
// MR-8: measuring() says whether the heap can be read, not whether anything is
// on it. A program arming before its first allocation -- the zero-heap
// firmware MR-1 is about -- has a working reading of 0, and must still be told
// it is measuring. Its own executable, with no test framework: GoogleTest
// allocates before any case runs, so only a bare main() sees an empty heap.
#include "pico_util/heap/heap_watermark.hpp"

int main() {
    // The premise: nothing is on the heap yet. If something linked in starts
    // allocating before main(), this case no longer tests an empty heap and
    // says so, rather than passing without testing anything.
    if (pico_util::HeapWatermark::measuring() &&
        pico_util::HeapWatermark::in_use_bytes() != 0U) {
        return 2;
    }
    pico_util::HeapWatermark watermark;
    watermark.arm();
    // Armed over an empty heap the reading is 0, which says nothing about
    // whether the heap can be read: measuring() must still say it can.
    const bool want = pico_util::detail::HEAP_READABLE;
    return pico_util::HeapWatermark::measuring() == want ? 0 : 1;
}
