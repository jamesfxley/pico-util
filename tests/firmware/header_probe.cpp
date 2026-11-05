// SPDX-License-Identifier: MIT
// The same headers compile and link freestanding without consumer conditionals
// (AR-2). Linking catches hosted facilities that inclusion alone cannot.
// Nothing runs it.
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

// A consumer-owned error type instantiates Result.
enum class ProbeError : unsigned char { NONE = 0, FAILED };

int main() {
    pico_util::HeapWatermark watermark;
    watermark.arm();

    // Instantiation exercises each module for the footprint gate. Inclusion
    // alone does not.
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

    // Local containers keep the probe's own storage out of the static-RAM
    // budget.
    unbound(1);

    auto label = pico_util::FixedString<32>::truncated("probe");
    // The result is discarded on purpose. The probe instantiates and links the
    // headers freestanding and does not check what they report.
    static_cast<void>(label.append("!"));
    pico_util::FlatMap<std::uint8_t, std::uint32_t, 4> table;
    static_cast<void>(table.insert_or_assign(1, 100));
    pico_util::RingBuffer<std::uint32_t, 4, pico_util::SingleContextCounter>
        ring;
    static_cast<void>(ring.push(7));
    // The counter links on both boards (CR-9). An ARMv6-M read-modify-write
    // would need an unavailable atomic runtime. Calling both sides and reset
    // emits every member to catch that regression.
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
    // The value is returned so that the call cannot be optimised away before
    // the linker has had to resolve it.
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
