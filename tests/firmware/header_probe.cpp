// SPDX-License-Identifier: MIT
// AR-2's cross-compiled probe: the same headers a host test uses, compiled and
// linked for the target with no consumer-side conditionals.
//
// Nothing runs this. It exists so the linker has to resolve everything the
// umbrella header pulls in, which is what turns "it compiles" into "it also
// links on a freestanding target". A header that reached for a hosted facility
// -- <iostream>, an exception path, a heap-assuming container -- fails here
// rather than in the first firmware consumer's build.
#include <array>
#include <cstdint>
#include <span>

#include "pico_util/bytes/as_bytes.hpp"
#include "pico_util/bytes/byte_order.hpp"
#include "pico_util/containers/fixed_string.hpp"
#include "pico_util/containers/flat_map.hpp"
#include "pico_util/containers/ring_buffer.hpp"
#include "pico_util/heap/heap_watermark.hpp"
#include "pico_util/pico_util.hpp"
#include "pico_util/publish/published.hpp"
#include "pico_util/result/callback.hpp"
#include "pico_util/result/result.hpp"

// A minimal taxonomy: Result is parameterised on the consumer's error type, so
// the probe has to supply one to instantiate it at all.
enum class ProbeError : unsigned char { NONE = 0, FAILED };

int main() {
    pico_util::HeapWatermark watermark;
    watermark.arm();

    // Instantiating one entity per module is what turns "the umbrella compiles"
    // into "it also links freestanding", and it is where the footprint figure
    // comes from -- including a template header instantiates nothing.
    std::array<std::uint8_t, 8> buffer{};
    pico_util::write_be_at<0, std::uint32_t>(std::span<std::uint8_t, 8>{buffer},
                                             0xDEADBEEF);
    pico_util::write_le<std::uint16_t>(std::span<std::uint8_t>{buffer}, 0xBEEF,
                                       4);
    const auto decoded = pico_util::read_be_at<0, std::uint32_t>(
        std::span<const std::uint8_t, 8>{buffer});
    const auto text = pico_util::as_bytes("probe");

    const pico_util::Result<int, ProbeError> ok_result = 3;
    const pico_util::Result<int, ProbeError> bad_result = ProbeError::FAILED;
    const pico_util::Callback<void(int)> unbound;

    // Function-local, and every capacity small: a container at namespace scope
    // would land in .bss and the static-RAM budget would be measuring the
    // probe's own choices rather than the headers'.
    unbound(1);

    auto label = pico_util::FixedString<32>::truncated("probe");
    // Discarded deliberately: this probe exists to instantiate and link the
    // headers freestanding, not to check what they report.
    static_cast<void>(label.append("!"));
    pico_util::FlatMap<std::uint8_t, std::uint32_t, 4> table;
    static_cast<void>(table.insert_or_assign(1, 100));
    pico_util::RingBuffer<std::uint32_t, 4, pico_util::SingleContextCounter>
        ring;
    static_cast<void>(ring.push(7));
    // CR-9's counter, instantiated so both boards have to link it. On ARMv6-M a
    // read-modify-write on a shared count compiles to a call into the atomic
    // runtime, which a freestanding link cannot resolve -- so a regression to
    // one fails the pico_w leg here rather than taking a spinlock in a
    // consumer's interrupt handler. Both sides and reset, so every member of
    // the counter is emitted and has to link.
    pico_util::RingBuffer<std::uint32_t, 4, pico_util::SpscCounter> spsc;
    static_cast<void>(spsc.push(8));
    static_cast<void>(spsc.push(9));
    spsc.pop();
    const std::uint32_t* const oldest = spsc.front();
    const std::uint32_t oldest_value = oldest != nullptr ? *oldest : 0U;
    spsc.clear();
    pico_util::Published<
        std::uint64_t,
        pico_util::PublishStrategy::SEQLOCK_WRITER_NOT_PREEMPTIBLE>
        edge;
    edge.store(42);
    pico_util::Published<
        std::uint64_t, pico_util::PublishStrategy::SLOT_RING_READER_MAY_PREEMPT>
        clock;
    clock.store(43);
    // Returned rather than discarded so the call cannot be optimised away
    // before the linker has had to resolve it.
    return static_cast<int>(watermark.growth_bytes()) +
           static_cast<int>(pico_util::HeapWatermark::in_use_bytes()) +
           static_cast<int>(decoded >> 24U) + static_cast<int>(text.size()) +
           ok_result.value_or(-1) + static_cast<int>(bad_result.error()) +
           static_cast<int>(static_cast<bool>(unbound)) +
           static_cast<int>(label.size()) +
           static_cast<int>(table.value_or(1, 0)) +
           static_cast<int>(ring.size()) + static_cast<int>(spsc.size()) +
           static_cast<int>(oldest_value) +
           static_cast<int>(edge.load() + clock.load().value_or(0U)) +
           pico_util::VERSION_MAJOR;
}
