// SPDX-License-Identifier: MIT
// The usage example compiles against the SDK. Application functions are only
// declared.
#include <cstddef>

void my_driver_init();
void service();
void report_heap(bool armed, bool measuring, std::size_t growth_bytes);

// At global scope, because the main() of the example is the program's.
#include "heap_firmware.inc"
