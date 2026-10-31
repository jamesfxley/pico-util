# Using pico-util

Header-only utilities shared by the pico-* family. Nothing here depends on the
Pico SDK, an RTOS or a network stack (AR-1), so the same headers serve the host
tests and the cross build without a conditional at the call site (AR-2).

Six modules, each an `INTERFACE` target of its own; the
[README](../README.md) lists them. `pico_util::core` links all of them except
`testing`, so one include and one link reach the library. Link a module by name
instead if you want only that one.

## Linking it

The [README](../README.md)'s Quick start shows plain `FetchContent` and
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

Two families, differing in where the bounds check lives rather than in what they
decode. Prefer the compile-time one wherever the buffer's size is known when you
write the call, which for a fixed protocol header it usually is:

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

This is a decision, not a menu, and there is no default because both choices
compile and both pass a single-threaded test. Ask which side can preempt which:

- **The interrupt handler writes and mainline reads** — an edge timestamp, a
  captured sample. Use `SEQLOCK_WRITER_NOT_PREEMPTIBLE`. The reader may spin, and
  that terminates because the handler runs to completion.
- **Mainline writes and the interrupt handler reads** — a clock offset, a
  configuration value. Use `SLOT_RING_READER_MAY_PREEMPT`. The reader never
  spins, which is what an interrupt handler needs; in exchange you owe it the
  writer contract, which is that stores are rare next to a read's duration. A
  writer storing in a tight loop laps the ring and no depth of ring survives it.
  `load()` returns a `Result`: a read that could not be validated is reported
  as `PublishedReadError::EXHAUSTED` rather than returned, so a torn value never
  reaches you as a success. Under the contract it does not happen, which makes
  it a writer bug to report rather than an event to retry around:

  <!-- example: slot_ring_read -->
  ```cpp
  const auto offset = clock.load();
  if (!offset.ok()) {
      return offset.error();  // the writer broke its contract
  }
  use(offset.value());
  ```

Choosing the first where the second belongs hangs an interrupt handler. Choosing
the second where the first belongs breaks its writer contract: an interrupt
handler that writes can lap the ring during every attempt of a mainline read,
and `load()` then reports `PublishedReadError::EXHAUSTED` where it should have
returned a value. Neither mistake shows in a single-threaded host test.

## Errors: `E{}` means success

`Result<T, E>` takes your error type, not one of this library's. `ok()` compares
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

`T` and `E` must not convert to each other, and a `static_assert` refuses the
pair that does. One type in both roles gives the two constructors the same
signature; merely *convertible* types — `Result<std::uint8_t, int>`, say — give
them different signatures that compete for the same argument, and overload
resolution then picks by conversion rank rather than by intent, so `return 5;`
would quietly build a *failure* carrying 5. A scoped enum converts in neither
direction, which is why it is the error type this expects.

## Capacity: the refusal is not optional

Every container here refuses at capacity rather than evicting, and every
operation that can refuse is `[[nodiscard]]`, so ignoring the refusal draws a
diagnostic, and a build with warnings as errors fails:

<!-- example: capacity_refusal -->
```cpp
pico_util::FixedString<8> header;
if (!header.assign(value)) {
    // `value` did not fit. Decide here; the string holds the prefix.
}

// Deliberately accepting the loss says so by name:
const auto label = pico_util::FixedString<16>::truncated("pico");

// And where the refusal genuinely does not matter, cast it away at the site:
static_cast<void>(ring.push(sample));
```

There is no converting constructor: `FixedString<N> s{text}` would truncate with
nothing at the site saying so and no way to recover the fact afterwards.

## The allocation harness: one translation unit

`pico_util::testing` is not reached by the umbrella and is not linked by
`pico_util::core`. Link it explicitly, and include the operator definitions in
**exactly one** translation unit of each test executable:

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

Arm the guard around the statements under test and nowhere else: GoogleTest
allocates freely while reporting. Give the harness its own executable, because
the replacement is program-wide.

**One guard at a time.** There is a single counter behind them, so a second
guard inside a live one would zero the outer tally and disarm the outer scope
when it went out of scope — leaving the rest of that scope uncounted and its
`EXPECT_EQ(count(), 0)` true for the wrong reason. Nesting aborts rather than
producing that green test.

## Measuring post-init heap growth

`HeapWatermark` answers one question: has the C heap grown since the point at
which initialisation finished? That turns a "zero post-init heap" claim into a
measurement rather than an assertion (MR-1).

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

`arm()` may be called again at any time; each call replaces the baseline rather
than accumulating against it (MR-3), which is what you want when a run has
several phases and each is measured separately.

`armed()` and `measuring()` exist because `growth_bytes()` returns 0 in three
different situations, and only one is a result: nothing grew. The others are
that there is no baseline to grow from, and that the heap cannot be read here at
all. Report all three, or a forgotten `arm()` or a platform with no reading
passes as a clean run. `measuring()` is `static constexpr`, so it can be
checked at compile time, and an empty heap does not make it false.

## A one-off reading

`in_use_bytes()` is `static` — it reports the heap as it stands, with no
baseline involved:

<!-- example: heap_in_use -->
```cpp
const std::size_t now = pico_util::HeapWatermark::in_use_bytes();
```

Use it for a one-off reading. Use an armed instance whenever the question is
about change over time, which is almost always the interesting one.

## What the number means, and what it does not

It is `uordblks` from `mallinfo2()`, or from `mallinfo()` where that is all the
libc has (MR-2) — bytes currently allocated on the C heap —
differenced against the baseline and clamped at 0, so a net shrink reads as 0
rather than wrapping (MR-4).

It is a proxy, and its limit is part of the requirement:

- **An allocate/free pair that nets to zero between two readings is invisible.**
  What it does catch, and report the size of, is steady-state growth — the
  failure mode a long-running image actually has.
- **It cannot attribute growth.** Per-allocation tracing is absent (FW-1).
- **It is not thread safe.** `mallinfo` is not, and this library does not
  serialise it. Reading from one thread is the caller's contract, stated in
  the specification's scope rather than by a requirement of its own.
- **On the host, a large allocation may not appear.** `uordblks` counts glibc's
  main arena only, so an mmap-backed allocation above the threshold is invisible
  to this proxy. Not a firmware concern: newlib has one arena.
- **On a platform with no `mallinfo`, every reading is 0.** The header compiles
  and the calls succeed rather than failing to build (MR-2), so check `armed()`
  and `measuring()` before concluding a run was clean. So is every reading
  under a sanitizer, whose allocator `mallinfo` does not see.

## Include order is load-bearing

If you include `<malloc.h>` yourself, include `<cstdlib>` first, in an include
block above the one holding `<malloc.h>` and with an `// IWYU pragma: keep`, so
neither the include sort nor an unused-include pass undoes the order.

arm-none-eabi newlib declares the malloc family with `_NOTHROW` in `<stdlib.h>`
and without it in `<malloc.h>`. Clang models that attribute as an exception
specification: a later declaration that *drops* it is accepted, one that *adds*
it is a fatal error that aborts parsing of the translation unit. GCC merges
either order silently, so only clang-based analysis sees it — which means
clang-tidy fails on a file that compiled cleanly.

## Version constants

The advertised version equals the version the build system holds (OR-1), which
this repository's own suite pins. A consumer checks the constants against the
major and, below 1.0, minor it was written for, so an incompatible build fails
at
compile time rather than at the first changed interface:

<!-- example: version_check -->
```cpp
static_assert(pico_util::VERSION_MAJOR == 0 && pico_util::VERSION_MINOR == 1,
              "written against pico_util 0.1");
```

The literal is the point: it is the consumer's statement of what it depends on.
The build system's own version macros are a compile definition on this
repository's test target and never reach a consumer.
