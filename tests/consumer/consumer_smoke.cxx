// SPDX-License-Identifier: MIT
// AR-3: consumer-side view of the installed package. Nothing here reaches into
// the source tree: every header below must arrive through the installed
// pico_util::core INTERFACE target, resolved by find_package, so a header the
// package fails to ship is a compile error in this test rather than a discovery
// made by the first downstream project.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

#include "pico_util/pico_util.hpp"
#include "pico_util/testing/allocation_counter.hpp"

// MR-5: deliberately NOT reached through the umbrella. This is the one
// translation unit of this executable, which is what the header requires.
#include "pico_util/testing/allocation_operators.ipp"

// Result is parameterised on the consumer's own error type; this stands in for
// one a downstream project would define.
enum class SmokeError : unsigned char { NONE = 0, FAILED };

int main() {
    // The umbrella must carry the version constants and reach HeapWatermark.
    std::printf("pico_util %d.%d.%d\n", pico_util::VERSION_MAJOR,
                pico_util::VERSION_MINOR, pico_util::VERSION_PATCH);

    pico_util::HeapWatermark watermark;
    if (watermark.armed() || watermark.growth_bytes() != 0U) {
        return 1;
    }

    watermark.arm();
    if (!watermark.armed()) {
        return 1;
    }

    // DR-1..DR-5: the bytes module has to arrive through the umbrella too, and
    // be usable from a consumer's own translation unit.
    std::array<std::uint8_t, 4> field{};
    pico_util::write_be_at<0, std::uint32_t>(std::span<std::uint8_t, 4>{field},
                                             0xCAFEF00D);
    if (pico_util::read_be<std::uint32_t>(
            std::span<const std::uint8_t>{field}) != 0xCAFEF00DU) {
        return 1;
    }
    if (pico_util::as_bytes("pico").size() != 4U) {
        return 1;
    }

    // FR-1..FR-11: the result module through the umbrella too.
    const pico_util::Result<int, SmokeError> good = 11;
    const pico_util::Result<int, SmokeError> bad = SmokeError::FAILED;
    if (!good.ok() || good.value() != 11 || bad.ok() ||
        bad.value_or(-1) != -1) {
        return 1;
    }
    const pico_util::Callback<void(int)> unbound;
    unbound(4);
    if (static_cast<bool>(unbound)) {
        return 1;
    }

    // BR-1..BR-16: the containers module through the umbrella too.
    auto label = pico_util::FixedString<16>::truncated("pico");
    pico_util::FlatMap<int, int, 4> table;
    if (table.insert_or_assign(1, 10) == nullptr) {
        return 1;
    }
    pico_util::RingBuffer<int, 4, pico_util::SingleContextCounter> ring;
    if (!ring.push(5)) {
        return 1;
    }
    if (label.view() != "pico" || table.value_or(1, -1) != 10 ||
        ring.size() != 1U) {
        return 1;
    }
    // ring_index.hpp is exported and pico-http depends on it, so the umbrella's
    // reach has to cover it here too.
    const pico_util::RingPosition next = pico_util::ring_index<4>(
        pico_util::RingPosition{3}, pico_util::RingCount{2});
    if (next.index != 1U || pico_util::ring_run<4>(pico_util::RingPosition{3},
                                                   pico_util::RingCount{2})
                                    .elements != 1U) {
        return 1;
    }

    // CR-1..CR-10: the publish module through the umbrella too.
    pico_util::Published<
        std::uint64_t,
        pico_util::PublishStrategy::SEQLOCK_WRITER_NOT_PREEMPTIBLE>
        edge;
    if (edge.published()) {
        return 1;
    }
    edge.store(0x0123456789ABCDEF);
    if (!edge.published() || edge.load() != 0x0123456789ABCDEFU) {
        return 1;
    }

    // MR-5, MR-6: the harness has to survive installation, since its consumers
    // are sibling repositories' test trees rather than this one's.
    {
        const pico_util::testing::AllocationGuard counted;
        const std::vector<char> probe(256, 'y');
        if (pico_util::testing::AllocationGuard::count() == 0U) {
            return 1;
        }
    }

    // MR-1's proxy has to be usable from a consumer's own translation unit, not
    // only from inside this repository's tests.
    const std::vector<char> block(std::size_t{64} * 1024, 'x');
    std::printf("growth=%zu in_use=%zu\n", watermark.growth_bytes(),
                pico_util::HeapWatermark::in_use_bytes());
    return 0;
}
