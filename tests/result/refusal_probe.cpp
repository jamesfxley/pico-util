// SPDX-License-Identifier: MIT
// Compile-failure probes (FR-1, FR-6). With PROBE undefined the file builds
// normally.
// Each PROBE value enables one rejected statement.
#include <cstdint>

#include "pico_util/result/result.hpp"

namespace {

enum class ProbeError : std::uint8_t { NONE = 0, FAILED };

pico_util::Result<int, ProbeError> produce() { return 1; }

[[maybe_unused]] int result_probe() {
#if defined(PROBE) && PROBE == 1
    // Discarding a Result is a diagnostic (FR-6).
    produce();
#elif defined(PROBE) && PROBE == 2
    // One type in both roles (FR-1).
    const pico_util::Result<int, int> same{1};
    (void)same;
#elif defined(PROBE) && PROBE == 3
    // Two types that convert to each other (FR-1).
    const pico_util::Result<std::int32_t, std::int64_t> convertible{1};
    (void)convertible;
#endif
    return produce().value_or(0);
}

}  // namespace
