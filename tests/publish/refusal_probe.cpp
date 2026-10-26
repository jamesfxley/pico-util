// SPDX-License-Identifier: MIT
// Refusals of Published (CR-1, CR-7). With PROBE undefined this compiles and is
// part of every build, so the probe itself is sound; each PROBE value enables
// one declaration the library promises to refuse.
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
    // CR-1: no default strategy.
    const pico_util::Published<std::uint32_t> unstrategied;
#elif defined(PROBE) && PROBE == 2
    // CR-7: a payload that is not trivially copyable, refused at declaration.
    const pico_util::Published<std::string, SEQLOCK> refused;
#endif
    return published.load();
}

}  // namespace
