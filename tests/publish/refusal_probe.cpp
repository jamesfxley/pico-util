// SPDX-License-Identifier: MIT
// Compile-failure probes (CR-1, CR-7). With PROBE undefined the file builds
// normally.
// Each PROBE value enables one rejected declaration.
#include <cstdint>
#if defined(PROBE) && PROBE == 2
#include <string>
#endif

#include "pico_util/publish/published.hpp"

namespace {

constexpr auto SEQLOCK =
    pico_util::PublishStrategy::SEQLOCK_WRITER_NOT_PREEMPTIBLE;

[[maybe_unused]] std::uint32_t publish_probe() {
    const pico_util::Published<std::uint32_t, SEQLOCK> published;
#if defined(PROBE) && PROBE == 1
    // No default strategy (CR-1).
    const pico_util::Published<std::uint32_t> unstrategied;
#elif defined(PROBE) && PROBE == 2
    // A payload that is not trivially copyable fails at the declaration
    // (CR-7).
    const pico_util::Published<std::string, SEQLOCK> refused;
#endif
    return published.load();
}

}  // namespace
