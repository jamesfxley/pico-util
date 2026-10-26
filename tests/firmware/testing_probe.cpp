// SPDX-License-Identifier: MIT
// AR-2: compilation compatibility of the counting header, guard and count.
// Allocation behavior belongs to host tests; no RMW allocator exercise here.
#include "pico_util/testing/allocation_counter.hpp"

int main() {
    const pico_util::testing::AllocationGuard guard;
    return static_cast<int>(pico_util::testing::AllocationGuard::count());
}
