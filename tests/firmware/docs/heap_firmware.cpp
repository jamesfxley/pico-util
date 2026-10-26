// SPDX-License-Identifier: MIT
// The usage guide's firmware bring-up, compiled in the cross tree against the
// SDK it calls. The application's own functions are declared, not defined:
// what is under test is that the example uses pico_util and the SDK as a
// pasted main.cpp would.
#include <cstddef>

void my_driver_init();
void service();
void report_heap(bool armed, bool measuring, std::size_t growth_bytes);

// At global scope: the example's main() is the program's.
#include "heap_firmware.inc"
