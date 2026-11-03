// SPDX-License-Identifier: MIT
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "hardware/timer.h"
#include "pico.h"
#include "pico/platform/common.h"
#include "pico/stdio.h"
#include "pico/time.h"
#include "pico_family/device_test.hpp"
#include "pico_util/containers/ring_buffer.hpp"
#include "pico_util/publish/published.hpp"

namespace {

constexpr std::uint32_t TRANSFERS = 4096;
constexpr std::uint32_t DEADLINE_MS = 5000;
constexpr std::int64_t TIMER_INTERVAL_US = 100;
constexpr std::int64_t STORE_TIMER_INTERVAL_US = 10;
constexpr std::int64_t SPSC_TIMER_INTERVAL_US = 10;
constexpr std::uint32_t PHASE_SEED = 0x243F6A88;

// Instruction-weighted statistical coverage model for GCC 14.2.1, Release,
// pico2_w, clean image. Re-derive for another compiler/profile/board. g
// excludes the marker stores and counts instructions before operation entry and
// after its exit; s counts the shortest successful operation path, with loop
// iterations expanded. ROM memcpy/memset bodies are omitted, so s is a lower
// bound and q=g/(g+s) an upper bound in this model. N is the first integer with
// q^N < 1e-9. This calculation assumes dispersed, independent
// instruction-weighted landings; fixed-seed phase variation does not prove
// independence or cycle uniformity. CR-2: set 0x10000c56, call 0x10000c5c,
// clear 0x10000c62; g=3 (add, bl, mov). load 0x100001e0..0x1000026c: 51
// instructions plus 63 extra iterations of 0x10000224..0x1000022e (4 each),
// s>=303. q<=3/306=0.009803922; q^4=9.238454e-9, q^5=9.057308e-11: N=5.
constexpr std::uint32_t SEQLOCK_MIN_LANDINGS = 5;
// CR-3: set 0x100010cc, call 0x100010d0, clear 0x100010d6;
// g=2 (bl at 0x100010d0, mov at 0x100010d4). store 0x10000408..0x10000476: 39
// instructions plus 63 extra iterations of 0x1000044e..0x10000458 (4 each),
// s>=291. q<=2/293=0.006825939; q^4=2.170948e-9, q^5=1.481876e-11: N=5.
constexpr std::uint32_t SLOT_RING_MIN_LANDINGS = 5;
// CR-9: set 0x10000e98, operation 0x10000e9c..0x10000efe,
// clear 0x10000f04; g=1 (mov at 0x10000f02). s>=473: 5 setup + 32*14
// instructions at 0x10000eaa..0x10000ed2 + 15 post-loop + 5 in counter.load at
// 0x10000a8c..0x10000a98; omit inactive predicated instructions.
// q<=1/474=0.002109705; q^3=9.389987e-9, q^4=1.981010e-11: N=4.
constexpr std::uint32_t SPSC_MIN_LANDINGS = 4;

void vary_phase(std::uint32_t& phase, std::uint32_t pause_mask) {
    phase = phase * 1664525U + 1013904223U;
    busy_wait_us_32((phase >> 16U) & pause_mask);
}

struct Payload {
    std::array<std::uint32_t, 64> words{};
};

void fill_payload(Payload& value, std::uint32_t sequence) {
    for (std::size_t index = 0; index < value.words.size(); ++index) {
        value.words[index] = index % 2 == 0 ? sequence : ~sequence;
    }
}

[[nodiscard]] bool whole(const Payload& value) {
    for (std::size_t index = 0; index < value.words.size(); ++index) {
        if (value.words[index] !=
            (index % 2 == 0 ? value.words.front() : ~value.words.front())) {
            return false;
        }
    }
    return true;
}

// pico/time.h documents repeating timer callbacks in the default alarm pool's
// IRQ on this core, with user_data retained until cancellation. pico/platform.h
// supplies __get_current_exception(): reject any inline mainline callback.
// All coordination uses acquire/release to keep it separate from the protocol
// under test. Each case runs once; its static context and stack-owned timer
// outlive cancellation. Large publication and ring storage stays off the stack.
struct Coverage {
    // Sequential consistency brackets the tested operation on both sides.
    std::atomic<bool> in_progress{false};
    // Only this case's IRQ writes the count, so load/store needs no RMW or
    // atomic runtime lock on ARMv6-M. Release/acquire publishes the count.
    std::atomic<std::uint32_t> landings{0};

    void record_interrupt() {
        if (__get_current_exception() != 0 &&
            in_progress.load(std::memory_order_seq_cst)) {
            landings.store(landings.load(std::memory_order_acquire) + 1,
                           std::memory_order_release);
        }
    }
};

struct CaseResult {
    bool passed = false;
    std::string_view detail = "timer unavailable";
};

[[nodiscard]] CaseResult case_result(bool valid, const Coverage& coverage,
                                     std::uint32_t minimum_landings) {
    if (coverage.landings.load(std::memory_order_acquire) < minimum_landings) {
        return {.passed = false, .detail = "too few interrupted windows"};
    }
    return {.passed = valid,
            .detail = valid ? "" : "payload or progress check failed"};
}

struct SeqlockState {
    Coverage coverage;
    Payload next;  // Used only by the producer after timer setup.
    pico_util::Published<
        Payload, pico_util::PublishStrategy::SEQLOCK_WRITER_NOT_PREEMPTIBLE>
        published;
    std::atomic<std::uint32_t> completed{0};
    std::atomic<bool> irq_only{true};
};

bool seqlock_irq(repeating_timer_t* timer) {
    auto& state = *static_cast<SeqlockState*>(timer->user_data);
    state.coverage.record_interrupt();
    if (__get_current_exception() == 0) {
        state.irq_only.store(false, std::memory_order_release);
    }
    const auto sequence = state.completed.load(std::memory_order_acquire) + 1;
    fill_payload(state.next, sequence);
    state.published.store(state.next);
    state.completed.store(sequence, std::memory_order_release);
    return sequence < TRANSFERS;
}

// CR-2: IRQ writes, mainline repeatedly reads multi-word complementary fields.
[[nodiscard]] CaseResult seqlock_case() {
    static SeqlockState state;
    fill_payload(state.next, 0);
    state.published.store(state.next);
    repeating_timer_t timer{};
    const auto deadline = make_timeout_time_ms(DEADLINE_MS);
    if (!add_repeating_timer_us(TIMER_INTERVAL_US, seqlock_irq, &state,
                                &timer)) {
        return {};
    }
    bool valid = true;
    std::uint32_t phase = PHASE_SEED;
    std::uint32_t reads = 0;
    while (state.completed.load(std::memory_order_acquire) < TRANSFERS &&
           !time_reached(deadline)) {
        state.coverage.in_progress.store(true, std::memory_order_seq_cst);
        const auto value = state.published.load();
        state.coverage.in_progress.store(false, std::memory_order_seq_cst);
        valid = whole(value) && value.words.front() <= TRANSFERS && valid;
        ++reads;
        vary_phase(phase, 7U);
    }
    static_cast<void>(cancel_repeating_timer(&timer));
    const auto final = state.published.load();
    const bool valid_final =
        valid && reads != 0 && whole(final) &&
        state.completed.load(std::memory_order_acquire) == TRANSFERS &&
        final.words.front() == TRANSFERS &&
        state.irq_only.load(std::memory_order_acquire);
    return case_result(valid_final, state.coverage, SEQLOCK_MIN_LANDINGS);
}

struct SlotRingState {
    Coverage coverage;
    Payload next;  // Used only by the producer after timer setup.
    pico_util::Published<
        Payload, pico_util::PublishStrategy::SLOT_RING_READER_MAY_PREEMPT>
        published;
    std::atomic<std::uint32_t> observed{0};
    std::atomic<bool> valid{true};
};

bool slot_ring_irq(repeating_timer_t* timer) {
    auto& state = *static_cast<SlotRingState*>(timer->user_data);
    state.coverage.record_interrupt();
    const auto result = state.published.load();
    if (__get_current_exception() == 0 || !result.ok()) {
        state.valid.store(false, std::memory_order_release);
        return false;
    }
    const auto value = result.value_or(Payload{});
    if (!whole(value) || value.words.front() > TRANSFERS) {
        state.valid.store(false, std::memory_order_release);
        return false;
    }
    state.observed.store(value.words.front(), std::memory_order_release);
    return true;
}

// CR-3, CR-4: mainline is the only writer. During an IRQ read it is suspended,
// so zero stores complete during that read, satisfying the writer contract.
// pico/time.h:801-816 documents IRQ callbacks and positive delays between
// callback completion and the next start. Consecutive stores with a short delay
// offer store brackets; their IRQ landing threshold is statistical coverage.
[[nodiscard]] CaseResult slot_ring_case() {
    static SlotRingState state;
    fill_payload(state.next, 0);
    state.published.store(state.next);
    repeating_timer_t timer{};
    const auto deadline = make_timeout_time_ms(DEADLINE_MS);
    if (!add_repeating_timer_us(STORE_TIMER_INTERVAL_US, slot_ring_irq, &state,
                                &timer)) {
        return {};
    }
    std::uint32_t phase = PHASE_SEED;
    std::uint32_t sequence = 1;
    for (; sequence <= TRANSFERS; ++sequence) {
        fill_payload(state.next, sequence);
        state.coverage.in_progress.store(true, std::memory_order_seq_cst);
        state.published.store(state.next);
        state.coverage.in_progress.store(false, std::memory_order_seq_cst);
        vary_phase(phase, 7U);
        if (!state.valid.load(std::memory_order_acquire) ||
            time_reached(deadline)) {
            break;
        }
    }
    while (state.observed.load(std::memory_order_acquire) != TRANSFERS &&
           state.valid.load(std::memory_order_acquire) &&
           !time_reached(deadline)) {
        tight_loop_contents();
    }
    static_cast<void>(cancel_repeating_timer(&timer));
    const bool valid =
        sequence == TRANSFERS + 1 &&
        state.observed.load(std::memory_order_acquire) == TRANSFERS &&
        state.valid.load(std::memory_order_acquire);
    return case_result(valid, state.coverage, SLOT_RING_MIN_LANDINGS);
}

struct SpscState {
    Coverage coverage;
    Payload next;  // Used only by the producer after timer setup.
    pico_util::RingBuffer<Payload, 7, pico_util::SpscCounter> ring;
    std::atomic<std::uint32_t> queued{0};
    std::atomic<bool> irq_only{true};
};

bool spsc_irq(repeating_timer_t* timer) {
    auto& state = *static_cast<SpscState*>(timer->user_data);
    state.coverage.record_interrupt();
    if (__get_current_exception() == 0) {
        state.irq_only.store(false, std::memory_order_release);
    }
    auto queued = state.queued.load(std::memory_order_acquire);
    // One attempt per IRQ: refusals retain the sequence number for retry.
    fill_payload(state.next, queued + 1);
    if (state.ring.push(state.next)) {
        ++queued;
    }
    state.queued.store(queued, std::memory_order_release);
    return queued < TRANSFERS;
}

// CR-9: IRQ producer, mainline consumer; 4096 payloads wrap seven slots.
[[nodiscard]] CaseResult spsc_case() {
    static SpscState state;
    repeating_timer_t timer{};
    const auto deadline = make_timeout_time_ms(DEADLINE_MS);
    if (!add_repeating_timer_us(SPSC_TIMER_INTERVAL_US, spsc_irq, &state,
                                &timer)) {
        return {};
    }
    std::uint32_t received = 0;
    std::uint32_t phase = PHASE_SEED;
    bool valid = true;
    while (received < TRANSFERS && !time_reached(deadline)) {
        const auto* value = state.ring.front();
        if (value != nullptr) {
            state.coverage.in_progress.store(true, std::memory_order_seq_cst);
            ++received;
            valid = whole(*value) && value->words.front() == received && valid;
            state.ring.pop();
            state.coverage.in_progress.store(false, std::memory_order_seq_cst);
            vary_phase(phase, 63U);
        }
    }
    static_cast<void>(cancel_repeating_timer(&timer));
    const bool valid_final =
        valid && received == TRANSFERS && state.ring.empty() &&
        state.queued.load(std::memory_order_acquire) == received &&
        state.irq_only.load(std::memory_order_acquire);
    return case_result(valid_final, state.coverage, SPSC_MIN_LANDINGS);
}

}  // namespace

int main() {
    stdio_init_all();
    pico_family::device_test::Run run;
    const auto seqlock = seqlock_case();
    run.check("CR-2", "seqlock-irq-writer-publishes-whole-values",
              seqlock.passed, seqlock.detail);
    const auto slot_ring = slot_ring_case();
    run.check("CR-3", "slot-ring-irq-reader-publishes-whole-values",
              slot_ring.passed, slot_ring.detail);
    const auto spsc = spsc_case();
    run.check("CR-9", "spsc-irq-handoff-preserves-sequence", spsc.passed,
              spsc.detail);
    run.finish();
}
