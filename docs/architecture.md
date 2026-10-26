# pico-util Architecture

How the library is put together. What it guarantees is
[technical-specification.md](technical-specification.md); this file describes
the implementation and is not normative.

## Shape

One `INTERFACE` target per module, and an umbrella target that links every
module except `testing`.
There is no compiled translation unit and no library archive: a consumer links a
target for its include directory and its C++20 requirement, and nothing else
crosses the link.

| File | Target | Holds |
|---|---|---|
| `pico_util.hpp` | `pico_util::core` | Umbrella header and the advertised version constants |
| `pico_util/bytes/byte_order.hpp` | `pico_util::bytes` | `read_be`/`read_le`/`write_be`/`write_le` and their compile-time-offset counterparts |
| `pico_util/bytes/as_bytes.hpp` | `pico_util::bytes` | `as_bytes` |
| `pico_util/heap/heap_watermark.hpp` | `pico_util::heap` | `HeapWatermark` |
| `pico_util/result/result.hpp` | `pico_util::result` | `Result<T, E>` and `Result<void, E>` |
| `pico_util/result/callback.hpp` | `pico_util::result` | `Callback<Ret(Args...)>` |
| `pico_util/containers/fixed_string.hpp` | `pico_util::containers` | `FixedString<N>` |
| `pico_util/containers/flat_map.hpp` | `pico_util::containers` | `FlatMap<K, V, N>` |
| `pico_util/containers/ring_buffer.hpp` | `pico_util::containers` | `RingBuffer<T, N, Counter>` and its two counter policies |
| `pico_util/containers/ring_index.hpp` | `pico_util::containers` | `ring_index<Capacity>`, `ring_run<Capacity>`, `RingPosition`, `RingCount` |
| `pico_util/publish/published.hpp` | `pico_util::publish` | `Published<T, Strategy>`, both strategies |
| `pico_util/testing/allocation_counter.hpp` | `pico_util::testing` | `AllocationGuard` and the counting bodies |
| `pico_util/testing/allocation_operators.ipp` | `pico_util::testing` | The twenty replaced allocation functions. `.ipp` because it is a translation unit, not a header |

`pico_util::core` links every shipped module **except `testing`**, so one include
and one link reach the whole library. A consumer that wants only one module may link it by name instead;
the module targets are exported for exactly that.

The umbrella's include set *is* AR-1: the C++ standard library and the platform
libc, and nothing else. No Pico SDK, no RTOS, no network stack. That constraint
is what lets the same headers serve host tests and firmware images without
conditional compilation at a consumer's call site (AR-2), and it is why this
repository exists separately rather than as a corner of one of its consumers.

What belongs here at all is set out below, under *What belongs in this
library*.

## `HeapWatermark`

The problem it solves is that a "zero post-init heap" claim is an assertion
until something measures it, and the obvious instrument is unavailable: the
Pico SDK already wraps `malloc`/`free` and overrides `operator new`/`delete`
through `pico_malloc` and `pico_cxx_options`, so a second per-allocation hook
is a link conflict rather than an observation.

What is available is the allocator's own accounting. `uordblks` is bytes
currently in use, read through `mallinfo2()` where the C library provides it and
`mallinfo()` otherwise (MR-2), so the difference between an armed baseline and a
later reading is net growth. `arm()` captures the baseline — repeatedly if the caller
wants, each call replacing the last rather than accumulating — and
`growth_bytes()` reports the difference, clamped at zero so a net shrink reads
as no growth rather than underflowing an unsigned type.

**The proxy's limit is part of the requirement, not a caveat about it.** Net
growth cannot see an allocate/free pair that nets to zero bytes inside one
observation window. What it does catch, and report the size of, is
steady-state growth — which is the failure mode a long-running image actually
has. The specification says so rather than claiming detection the instrument
does not have.

Before `arm()`, `growth_bytes()` reports 0, and `armed()` exists so a consumer
can tell that zero from a measured one. `measuring()` does the same for a
build whose every reading is 0 -- no `mallinfo`, or AddressSanitizer's allocator
-- and is decided by the build rather than by a reading, because a working heap
with nothing on it also reads 0.

## Two byte-order families

`byte_order.hpp` offers each accessor twice, and the difference is where the
bounds check lives rather than what it decodes.

The compile-time-offset family — `read_be_at<Offset, T>` and its three siblings —
takes a `std::span` of *static* extent and constrains the read to lie within
it, as `Offset <= Extent && sizeof(T) <= Extent - Offset`: never as
`Offset + sizeof(T) <= Extent`, whose sum wraps for an `Offset` near the type's
maximum and would admit an out-of-range read. An over-run is a substitution failure, so it is a build
error and it is visible to a `requires`-expression, which is what makes DR-2
testable without a runtime path to exercise. The constraint also refuses a
dynamic extent outright: `std::dynamic_extent` is `SIZE_MAX`, so a bound checked
against it would pass for every offset and read as protection that is not there.

The runtime-offset family takes a dynamic extent and checks nothing. Its callers
are framing layers that have already validated a length, and charging them for a
second check they cannot fail would push them back to hand-written shifts — which
is the duplication the module exists to end. DR-3 states the absence of the check
so that it is a documented contract rather than an omission.

Both families share one implementation: the compile-time one forwards to the
runtime one with `Offset` as the argument, because a static-extent span converts
implicitly to a dynamic-extent one. Only the constraint differs.

`WireInteger` excludes `bool` and the character types deliberately. A single byte
has no byte order, so reading one through an endian accessor says the opposite of
what it does.

## The error type is a parameter, the container is not

`Result<T, E>` is the shared half of a decision whose other half is not shared.
The container -- holds a value or an error, never both; trivially copyable
whenever its value is;
`value_or` for the checked read -- is the same everywhere. The taxonomy is not:
a driver's bus failures and a parser's malformed-input failures are separate
vocabularies, and merging them would put unrelated enumerators in one another's
namespace and make this repository the one every consumer amends to add an error
code.

`ok()` compares against `E{}`, so an error enum's zero enumerator must be its
success state. That convention is the whole coupling between the container and
the taxonomy, and it is stated in FR-1 rather than left to be discovered.

The value and error types must not convert to each other. `Result<E, E>` would
give the value and error constructors one signature, which is ill-formed, with
the diagnostic pointing inside this header at an overload the caller never
wrote. A pair convertible either way is worse: `Result<std::uint8_t, int>` built
from `5` compiles, binds the error constructor by conversion rank and yields a
failure, with no diagnostic anywhere. A `static_assert` refuses any such pair
and names the fix.

`Callback` sits beside `Result` because the two are one decision seen from the
two directions a seam is crossed: a fallible call returns a `Result`, and a
notification the caller cannot return through goes out as a `Callback`.

## The ring buffer core, and the ring it does not serve

`RingBuffer` is the container a transmit queue and a log sink both are. Its wrap
arithmetic is internal and invisible to a caller. It detects a full ring by an
explicit count rather than a reserved slot, which recovers the slot and keeps a
power-of-two constraint off the capacity. Synchronisation is the one thing a
caller chooses, and it is the `Counter` parameter.

The policy is not leaky because the count is the only value both sides share:
`head_` is written only by the producer and `tail_` only by the consumer, so
nothing else needs to carry a publication. `acquire()`/`commit()`
is the other half: it lets a caller fill a slot in place — a formatted log line,
a cargo payload — rather than through a temporary.

A zero-copy byte transmit ring does **not** fit and is deliberately not forced
to. It
is not a ring of elements but a byte stream with three regions and two
independent cursors: bytes are reclassified from unsent to in-flight by
`mark_sent()` and released only by `ack()`, and `peek()` returns a contiguous run
so a network stack can write it without copying. Sitting that on an element ring
would mean
the core exposing raw index arithmetic and contiguous-run computation that
nothing else uses, which is the definition of a leaky core. Its share is
`ring_index.hpp` — the arithmetic alone, which both `RingBuffer` and that ring
use.

`FlatMap` serves fixed-capacity lookup by a caller-supplied key: a linear
scan and explicit refusal at capacity. A table that mints generation-tagged
handles instead is a slot allocator, rather than a map.

## Two publication strategies, and the axis between them

An interrupt timestamp is written by a handler and read in mainline. A
mainline seqlock reader can wait for that writer because the handler completes
without being preempted by its reader. A clock offset written in mainline and
read in an interrupt has the opposite constraint: a spinning interrupt reader
would wait for its own suspended writer. A slot ring bounds its read attempts
instead.

Neither is the better one, so `Published` offers both and refuses to pick. The
primary template is declared and not defined: a declaration naming no strategy
does not compile. That is deliberate friction, because both strategies compile
for any `T` and both pass a single-threaded host test, so a wrong choice is
invisible until it is a hang in an interrupt handler or a torn read on target.

**The slot ring's bound rests on its writer contract, not on its retry count.**
The reader tolerates the writer advancing by up to `SLOT_COUNT - 2` stores —
the writer fills the slot *after* the one it last published, so it takes
`SLOT_COUNT - 1` advances to reach the slot being read. Equality would be
needlessly strict. But a writer storing in a tight loop laps a four-slot ring
inside a single read, and no ring of any depth survives arbitrary laps. So the
contract is stated on the enumerator and
required by CR-4. What holds unconditionally is that the reader returns, and
that what it returns as a success is a whole value: a read that exhausts its
attempts is reported as an error rather than handed back possibly torn (CR-3).
The predicate itself is a function pinned at its boundaries by a test that does
not depend on timing, because whether a concurrent writer laps a read part-way
through does, and a concurrent case alone cannot prove the arithmetic right.

Reporting exhaustion through `Result` is the one place a module here depends on
another: `publish` links `result`.

The re-read of the counter takes an acquire **fence**, not an acquire load. An
acquire load stops later accesses from moving earlier; this needs the opposite —
the word loads must not move after the counter re-read. Getting it wrong yields
a validation that passes over a torn read.

Both strategies carry the payload as 32-bit atomic words and `memcpy` through a
properly aligned local. That is what keeps a 64-bit payload off `__atomic_load_8`,
which the SDK implements as a global spinlock with interrupts disabled — the
opposite of what either strategy is for. Because the value is assembled in a
local, an over-aligned `T` is fine: the word array's alignment never reaches it.

## The harness that is a header and a translation unit

`allocation_operators.ipp` defines the replaceable allocation functions, and the
standard requires exactly one definition of each in a program. They cannot be
`inline`, so the header is a translation unit wearing a header's clothes: include
it in **one** translation unit per test executable. A second include is a
duplicate-symbol link error, which is the loud failure this arrangement wants — a
quiet one would mean two counters and a test passing by accident.

That is why it is outside the umbrella and outside both cross probes, and why
`pico_util::core` does not link `pico_util::testing`. Reaching those definitions
through an umbrella include would put them into every translation unit that
included `pico_util.hpp`. The counting header, `allocation_counter.hpp`, is
inline and harmless anywhere; the umbrella never reaches it either, so it has a
cross probe of its own, `tests/firmware/testing_probe.cpp`.

The replacement is complete: all twenty forms, each reaching the same
`malloc`, `aligned_alloc` and `free`. A partial replacement can return a block
through another allocator's
freeing function. Complete replacement keeps all twenty forms consistent.

It ships rather than staying under `tests/` because its consumers are other
repositories' test trees: a library claiming that a path allocates nothing
links it to prove the claim.

## Allocation

Nothing here allocates, apart from the `testing` harness, whose subject is the
allocator and which no utility links. Every utility is header-only and depends on nothing
beyond the C++ standard library and the platform libc, which is what lets a
constrained consumer link one without inheriting a policy along with it.

`HeapWatermark` is about allocation without performing any: it observes the
heap through what the C library already reports, for the reason the
[`HeapWatermark`](#heapwatermark) section gives, and allocates nothing itself.

## Two spellings of one call

glibc 2.33 added `mallinfo2()`, whose fields are `size_t`, and deprecated
`mallinfo()`, whose fields are `int`. newlib — the firmware libc, and the one
that matters here — has only the older spelling. Both are kept, selected by
preprocessor feature detection, with the deprecated one used solely where
`mallinfo2` is absent. A platform providing neither compiles: `in_use_bytes()`
reports 0 and growth is always 0, rather than the header failing to compile.

The detection flags are macros rather than `constexpr` constants because they
have to be testable by `#if`, which a constant cannot be. That is the whole of
the `cppcoreguidelines-macro-usage` suppression on them. Thread safety, which
`mallinfo` lacks, is left to the caller; see
[Limits that are the caller's](#limits-that-are-the-callers-not-this-librarys).

## Include order is load-bearing

`<cstdlib>` must be seen before `<malloc.h>`. newlib declares the malloc family
with `_NOTHROW` in `<stdlib.h>` and without it in `<malloc.h>`; clang models
that attribute as an exception specification and rejects whichever declaration
adds one to an existing declaration.

The `<cstdlib>` include therefore sits in the first block, under a comment
saying why, not beside `<malloc.h>` where it belongs by subject — down
there the include sort ranks the C header above it and silently reinstates the
bad order.

## The version pin

`VERSION_MAJOR`/`MINOR`/`PATCH` in the umbrella header and
`project(pico_util VERSION ...)` are two records of one fact. The test target
receives the CMake values as compile definitions and compares the header's
constants against *those* — never against a literal copied out of the header,
which would only ever compare the header with itself.

An `INTERFACE` library has no translation unit, so there is no firmware-side
`static_assert` to match: putting the definitions on the `INTERFACE` target
instead would propagate build-time values into every installed consumer.

## What belongs in this library

Every module here has the same thing in common: two or more repositories were
already maintaining a version of it. The admission test is two conditions at
once.

**Two or more repositories have each implemented it independently.** Not that
two consume it — a primitive each repository wrote for itself is already being
maintained more than once, and waiting for two of them to link the same header
would mean resolving the duplication before it could be counted. A utility one
repository invented and nobody else reached for belongs in that repository,
where it can change without a release here.

**It can be written under AR-1** — the C++ standard library and the platform
libc, nothing else. Anything reaching for the Pico SDK, an RTOS or a network
stack fails this and is somebody else's utility.

Both conditions matter. The first keeps this from becoming a drawer; the second
is what makes the same headers serve host tests and firmware images, which is
the only reason a shared utility repository is cheaper than duplication.

## What is deliberately not here

There is no `NonCopyable` base for the four deleted special members a class
declares to forbid copying and moving. Deleting them in place is clearer than an
inherited base with its empty-base and `final` interactions, and the saving is
four lines a site.

## Limits that are the caller's, not this library's

**Thread safety.** `mallinfo` is not thread safe and this library does not
serialise it; the single-threaded caller is the consumer's contract. Serialising
it here would mean choosing a lock, and there is no lock available under AR-1
that is also correct on the target — a `std::mutex` on firmware is not the
primitive a consumer with an RTOS or a second core actually wants, and a
spinlock is an SDK dependency. Leaving the contract with the caller keeps the
choice where the information is.

**Non-main arenas.** The host reading misses what glibc keeps outside its main
arena, a limit the specification records. Making the host reading exact would
mean summing arenas, which glibc does not expose portably.

## Why there is no compiled surface

The library is `INTERFACE` and header-only, and adding a translation unit would
change what it costs a consumer: an object to link, static storage, and an
initialisation order this library currently contributes nothing to. A utility
that genuinely needs a compiled TU is a signal it belongs somewhere else, not
that this repository should grow one.

`allocation_operators.ipp` is the one thing that looks like an exception and is
not. It *becomes* part of a consumer's translation unit by design — the
replaceable allocation functions cannot be `inline` — but this library still
compiles nothing: the file is included by the consumer's own test executable, in
exactly one translation unit, and reaches neither the umbrella nor either cross
probe. Its `.ipp` extension says so.
