// SPDX-License-Identifier: MIT
// Refusals of Result (FR-1, FR-6). With PROBE undefined this compiles and is
// part of every build, so the probe itself is sound; each PROBE value enables
// one statement the library promises to refuse.
#include <cstdint>

#include "pico_util/result/result.hpp"

namespace {

enum class ProbeError : std::uint8_t { NONE = 0, FAILED };

pico_util::Result<int, ProbeError> produce() { return 1; }

[[maybe_unused]] int result_probe() {
#if defined(PROBE) && PROBE == 1
    // FR-6: discarding a Result is a diagnostic.
    produce();
#elif defined(PROBE) && PROBE == 2
    // FR-1: one type in both roles.
    const pico_util::Result<int, int> same{1};
    (void)same;
#elif defined(PROBE) && PROBE == 3
    // FR-1: two types that convert to each other.
    const pico_util::Result<std::int32_t, std::int64_t> convertible{1};
    (void)convertible;
#endif
    return produce().value_or(0);
}

}  // namespace
