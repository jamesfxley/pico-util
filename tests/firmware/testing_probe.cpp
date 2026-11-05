// SPDX-License-Identifier: MIT
// Compilation compatibility of the counting header, guard and count (AR-2).
// Host tests cover allocation behaviour, and there is no read-modify-write
// allocator exercise here.
#include "pico_util/testing/allocation_counter.hpp"

int main() {
    const pico_util::testing::AllocationGuard guard;
    return static_cast<int>(pico_util::testing::AllocationGuard::count());
}
