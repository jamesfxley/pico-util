// SPDX-License-Identifier: MIT
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

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

struct Payload {
    std::array<std::uint32_t, 64> words{};
};

[[nodiscard]] Payload payload(std::uint32_t sequence) {
    Payload value;
    for (std::size_t index = 0; index < value.words.size(); ++index) {
        value.words[index] = index % 2 == 0 ? sequence : ~sequence;
    }
    return value;
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
// under test. The timer and its stack-owned context outlive cancellation.
struct SeqlockState {
    pico_util::Published<
        Payload, pico_util::PublishStrategy::SEQLOCK_WRITER_NOT_PREEMPTIBLE>
        published;
    std::atomic<std::uint32_t> completed{0};
    std::atomic<bool> irq_only{true};
};

bool seqlock_irq(repeating_timer_t* timer) {
    auto& state = *static_cast<SeqlockState*>(timer->user_data);
    if (__get_current_exception() == 0) {
        state.irq_only.store(false, std::memory_order_release);
    }
    const auto sequence = state.completed.load(std::memory_order_acquire) + 1;
    state.published.store(payload(sequence));
    state.completed.store(sequence, std::memory_order_release);
    return sequence < TRANSFERS;
}

// CR-2: IRQ writes, mainline repeatedly reads multi-word complementary fields.
[[nodiscard]] bool seqlock_case() {
    SeqlockState state;
    state.published.store(payload(0));
    repeating_timer_t timer{};
    const auto deadline = make_timeout_time_ms(DEADLINE_MS);
    if (!add_repeating_timer_us(TIMER_INTERVAL_US, seqlock_irq, &state,
                                &timer)) {
        return false;
    }
    bool valid = true;
    std::uint32_t reads = 0;
    while (state.completed.load(std::memory_order_acquire) < TRANSFERS &&
           !time_reached(deadline)) {
        const auto value = state.published.load();
        valid = whole(value) && value.words.front() <= TRANSFERS && valid;
        ++reads;
    }
    static_cast<void>(cancel_repeating_timer(&timer));
    const auto final = state.published.load();
    return valid && reads != 0 && whole(final) &&
           state.completed.load(std::memory_order_acquire) == TRANSFERS &&
           final.words.front() == TRANSFERS &&
           state.irq_only.load(std::memory_order_acquire);
}

struct SlotRingState {
    pico_util::Published<
        Payload, pico_util::PublishStrategy::SLOT_RING_READER_MAY_PREEMPT>
        published;
    std::atomic<std::uint32_t> observed{0};
    std::atomic<bool> valid{true};
};

bool slot_ring_irq(repeating_timer_t* timer) {
    auto& state = *static_cast<SlotRingState*>(timer->user_data);
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
// Acknowledge every store before advancing; the IRQ may also preempt store().
[[nodiscard]] bool slot_ring_case() {
    SlotRingState state;
    state.published.store(payload(0));
    repeating_timer_t timer{};
    const auto deadline = make_timeout_time_ms(DEADLINE_MS);
    if (!add_repeating_timer_us(TIMER_INTERVAL_US, slot_ring_irq, &state,
                                &timer)) {
        return false;
    }
    std::uint32_t sequence = 1;
    for (; sequence <= TRANSFERS; ++sequence) {
        state.published.store(payload(sequence));
        while (state.observed.load(std::memory_order_acquire) != sequence &&
               state.valid.load(std::memory_order_acquire) &&
               !time_reached(deadline)) {
            tight_loop_contents();
        }
        if (!state.valid.load(std::memory_order_acquire) ||
            time_reached(deadline)) {
            break;
        }
    }
    static_cast<void>(cancel_repeating_timer(&timer));
    return sequence == TRANSFERS + 1 &&
           state.observed.load(std::memory_order_acquire) == TRANSFERS &&
           state.valid.load(std::memory_order_acquire);
}

struct SpscState {
    pico_util::RingBuffer<Payload, 7, pico_util::SpscCounter> ring;
    std::atomic<std::uint32_t> queued{0};
    std::atomic<bool> irq_only{true};
};

bool spsc_irq(repeating_timer_t* timer) {
    auto& state = *static_cast<SpscState*>(timer->user_data);
    if (__get_current_exception() == 0) {
        state.irq_only.store(false, std::memory_order_release);
    }
    auto queued = state.queued.load(std::memory_order_acquire);
    // Bounded burst fills the ring and exercises refusal without dropping a
    // sequence number: only successful pushes advance the producer.
    for (std::size_t attempt = 0;
         attempt <= decltype(state.ring)::CAPACITY && queued < TRANSFERS;
         ++attempt) {
        if (!state.ring.push(payload(queued + 1))) {
            break;
        }
        ++queued;
    }
    state.queued.store(queued, std::memory_order_release);
    return queued < TRANSFERS;
}

// CR-9: IRQ producer, mainline consumer; 4096 payloads wrap seven slots.
[[nodiscard]] bool spsc_case() {
    SpscState state;
    repeating_timer_t timer{};
    const auto deadline = make_timeout_time_ms(DEADLINE_MS);
    if (!add_repeating_timer_us(TIMER_INTERVAL_US, spsc_irq, &state, &timer)) {
        return false;
    }
    std::uint32_t received = 0;
    bool valid = true;
    while (received < TRANSFERS && !time_reached(deadline)) {
        const auto* value = state.ring.front();
        if (value != nullptr) {
            ++received;
            valid = whole(*value) && value->words.front() == received && valid;
            state.ring.pop();
        }
    }
    static_cast<void>(cancel_repeating_timer(&timer));
    return valid && received == TRANSFERS && state.ring.empty() &&
           state.queued.load(std::memory_order_acquire) == received &&
           state.irq_only.load(std::memory_order_acquire);
}

}  // namespace

int main() {
    stdio_init_all();
    pico_family::device_test::Run run;
    run.check("CR-2", "seqlock-irq-writer-publishes-whole-values",
              seqlock_case(), "whole reads and final completed store");
    run.check("CR-3", "slot-ring-irq-reader-publishes-whole-values",
              slot_ring_case(), "IRQ reads succeed under the writer contract");
    run.check("CR-9", "spsc-irq-handoff-preserves-sequence", spsc_case(),
              "queued payloads arrive intact exactly once in order");
    run.finish();
}
