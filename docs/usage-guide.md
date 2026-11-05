# Using pico-util

The library is a set of header-only utilities shared by the pico-* family.
Nothing in it depends on the Pico SDK, an RTOS or a network stack (AR-1), so the
same headers serve the host tests and the cross build without a conditional at
the call site (AR-2).

There are six modules, each an `INTERFACE` target of its own. The
[README](../README.md) lists them. `pico_util::core` links all of them except
`testing`, so one include and one link reach the library. To use only one
module, link it by name instead.

## Linking it

The Quick start in the [README](../README.md) shows plain `FetchContent` and
`find_package`. A family project declares product pins directly in CMake with
`pico_family_dependency()` followed by `pico_family_make_available()`.

```cmake
target_link_libraries(my_app PRIVATE pico_util::core)
```

The umbrella header pulls in everything except the testing harness:

<!-- example: umbrella_include -->
```cpp
#include <pico_util/pico_util.hpp>
```

## Choosing a byte-order family

The two families decode in the same way and differ in where the bounds check
lives. Prefer the compile-time family wherever the size of the buffer is known
when you write the call, as it usually is for a fixed protocol header:

<!-- example: byte_order -->
```cpp
// Bound checked by the type: reading past the end does not compile.
const std::span<const std::uint8_t, 4> header{bytes};
const auto length = pico_util::read_le_at<0, std::uint16_t>(header);

// Runtime offset, dynamic extent, no bounds check. The caller has already
// validated the length -- that is the precondition, and it is not checked here.
const auto field = pico_util::read_be<std::uint32_t>(payload, offset);
```

## Choosing a publication strategy

There is no default strategy, because both choices compile and both pass a
single-threaded test. Decide which side can preempt which:

- If the interrupt handler writes and mainline reads, as for an edge timestamp
  or a captured sample, use `SEQLOCK_WRITER_NOT_PREEMPTIBLE`. The reader may
  spin, and that terminates because the handler runs to completion.
- If mainline writes and the interrupt handler reads, as for a clock offset or a
  configuration value, use `SLOT_RING_READER_MAY_PREEMPT`. The reader never
  spins, which an interrupt handler needs. In exchange you owe the writer
  contract, which is that stores are rare compared with the duration of a read.
  A writer that stores in a tight loop laps the ring, and no depth of ring
  survives that. `load()` returns a `Result`. A read that could not be validated
  is returned as `PublishedReadError::EXHAUSTED`, so a torn value never reaches
  you as a success. Under the contract this does not happen, so the error is a
  writer bug to report and not an event to retry:

  <!-- example: slot_ring_read -->
  ```cpp
  const auto offset = clock.load();
  if (!offset.ok()) {
      return offset.error();  // the writer broke its contract
  }
  use(offset.value());
  ```

Choosing the seqlock where the slot ring belongs hangs an interrupt handler.
Choosing the slot ring where the seqlock belongs breaks the writer contract. An
interrupt handler that writes can lap the ring during every attempt of a
mainline read, and `load()` then returns `PublishedReadError::EXHAUSTED` where
it should have returned a value. Neither mistake shows in a single-threaded host
test.

## Error types

`Result<T, E>` takes your error type and does not provide one. `ok()` compares
against `E{}`, so the zero enumerator of your enum must be its success state:

<!-- example: result_error -->
```cpp
enum class BusError : std::uint8_t { NONE = 0, TIMEOUT, NACK };

pico_util::Result<int, BusError> read(bool timed_out) {
    if (timed_out) {
        return BusError::TIMEOUT;   // neither return names Result
    }
    return 7;
}
```

`T` and `E` must not convert to each other, and a `static_assert` makes a pair
that does, such as `Result<std::uint8_t, int>`, fail to compile. A scoped enum
converts in neither direction, so it is the error type that `Result` expects.
[architecture.md](architecture.md#result-and-the-error-type) gives the reason.

## Callbacks

`Callback::bind` does not accept a `const` member function. To bind one, you can
drop its `const`, or wrap the call in a free function that takes the context
pointer and construct the `Callback` from that.

## Capacity

Every container here fails an insert at capacity and does not evict an element.
Every operation that can fail this way is `[[nodiscard]]`, so ignoring the
result draws a diagnostic, and a build with warnings as errors fails:

<!-- example: capacity_refusal -->
```cpp
pico_util::FixedString<8> header;
if (!header.assign(value)) {
    // `value` did not fit. Decide here; the string holds the prefix.
}

// Deliberately accepting the loss says so by name:
const auto label = pico_util::FixedString<16>::truncated("pico");

// And where a failed push genuinely does not matter, cast it away at the site:
static_cast<void>(ring.push(sample));
```

If you must not lose data, you can retry a full-ring push.

There is no converting constructor. `FixedString<N> s{text}` would truncate with
nothing at the call site to say so and no way to recover the fact afterwards.

## Allocation harness

The umbrella header does not reach `pico_util::testing` and `pico_util::core`
does not link it. Link it explicitly, and include the operator definitions in
exactly one translation unit of each test executable:

<!-- example: allocation_harness -->
```cpp
#include "pico_util/testing/allocation_counter.hpp"
// One TU per executable. A second include is a duplicate-symbol link error.
#include "pico_util/testing/allocation_operators.ipp"

TEST(MyDriver, ServiceAllocatesNothing) {
    MyDriver driver;
    driver.init();
    const pico_util::testing::AllocationGuard guard;
    driver.service();
    EXPECT_EQ(pico_util::testing::AllocationGuard::count(), 0U);
}
```

Arm the guard around the statements under test and nowhere else, because
GoogleTest allocates freely while reporting. Give the harness its own
executable, because the replacement applies to the whole program.

Only one guard can be live at a time. There is a single counter behind them, so
a second guard inside a live one would zero the outer tally and, when it went
out of scope, disarm the outer scope. The rest of that scope would go uncounted
and its `EXPECT_EQ(count(), 0)` would hold for the wrong reason. A nested guard
aborts the program and does not produce that passing test.

## Measuring heap growth after initialisation

`HeapWatermark` answers one question: has the C heap grown since initialisation
finished? That turns a claim of zero heap use after initialisation into a
measurement and not an assertion (MR-1).

Hold one in static storage, arm it once your own initialisation is complete,
and read it whenever you report status:

<!-- example: firmware/heap_firmware -->
```cpp
#include <cstddef>

#include <pico_util/pico_util.hpp>

#include "pico/stdlib.h"

int main() {
    stdio_init_all();
    my_driver_init();  // allocations here are the baseline, not growth

    // Static storage, which is also where a footprint gate can see it.
    static pico_util::HeapWatermark heap;
    heap.arm();

    for (;;) {
        service();
        // however you report status
        report_heap(heap.armed(), pico_util::HeapWatermark::measuring(),
                    heap.growth_bytes());
    }
}
```

`arm()` may be called again at any time. Each call replaces the baseline and
does not accumulate against it (MR-3), which suits a run with several phases
that are measured separately.

`armed()` and `measuring()` exist because `growth_bytes()` returns 0 in three
situations, and only the first is a result. In the first, nothing grew. In the
second, there is no baseline to grow from. In the third, the heap cannot be read
here at all. Report all three, or a forgotten `arm()` or a platform with no
reading passes as a clean run. `measuring()` is `static constexpr`, so it can be
checked at compile time, and an empty heap does not make it false.

## A one-off reading

`in_use_bytes()` is `static`. It reports the heap as it stands, with no
baseline:

<!-- example: heap_in_use -->
```cpp
const std::size_t now = pico_util::HeapWatermark::in_use_bytes();
```

Use it for a one-off reading. Use an armed instance when the question is about
change over time, which is usually the more useful question.

## What the number means, and what it does not

The number is `uordblks`, the bytes currently allocated on the C heap, from
`mallinfo2()` or from `mallinfo()` where that is all the libc has (MR-2). It is
differenced against the baseline and clamped at 0, so a net shrink reads as 0
and does not wrap (MR-4).

It is a proxy, and its limit is part of the requirement:

- An allocation and free that net to zero between two readings is invisible. It
  does catch steady-state growth and reports its size, which is the failure mode
  of a long-running image.
- It cannot attribute growth. Per-allocation tracing is absent (FW-1).
- It is not thread safe. `mallinfo` is not, and this library does not serialise
  it. Reading from one thread is the caller's contract, stated in the scope of
  the specification and not by a requirement of its own.
- On the host, a large allocation might not appear. `uordblks` counts the main
  arena of glibc only, so an mmap-backed allocation above the threshold is
  invisible to this proxy. This does not affect firmware, because newlib has one
  arena.
- On a platform with no `mallinfo`, every reading is 0. The header compiles and
  the calls succeed (MR-2), so check `armed()` and `measuring()` before
  concluding that a run was clean. Every reading is also 0 under a sanitiser,
  whose allocator `mallinfo` does not see.

## Include order

If you include `<malloc.h>` yourself, include `<cstdlib>` first, in an include
block above the one that holds `<malloc.h>` and with an `// IWYU pragma: keep`,
so that neither the include sort nor an unused-include pass undoes the order.

arm-none-eabi newlib declares the malloc family with `_NOTHROW` in `<stdlib.h>`
and without it in `<malloc.h>`. Clang models that attribute as an exception
specification. A later declaration that drops it is accepted, and one that adds
it is a fatal error that aborts parsing of the translation unit. GCC merges
either order silently, so only clang-based analysis sees the problem, and
clang-tidy then fails on a file that compiled cleanly.

## Version constants

The advertised version equals the version that the build system holds, and this
repository's own suite checks that (OR-1). A consumer checks the constants
against the major version, and below 1.0 the minor version, that it was written
for. An incompatible build then fails at compile time and not at the first
changed interface:

<!-- example: version_check -->
```cpp
static_assert(pico_util::VERSION_MAJOR == 0 && pico_util::VERSION_MINOR == 1,
              "written against pico_util 0.1");
```

The literal is the consumer's statement of what it depends on. The version
macros of the build system are a compile definition on this repository's test
target and never reach a consumer.
