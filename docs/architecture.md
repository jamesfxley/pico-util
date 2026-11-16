# pico-util Architecture

This document describes the implementation and is not normative. The guarantees
are in [technical-specification.md](technical-specification.md).

## Shape

The library has one `INTERFACE` target per module and an umbrella target that
links every module except `testing`. There is no compiled translation unit and
no library archive. A consumer links a target for its include directory and its
C++20 requirement, and nothing else crosses the link.

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
| `pico_util/testing/allocation_operators.ipp` | `pico_util::testing` | The twenty replaced allocation functions. The extension `.ipp` marks it as a translation unit and not a header |

`pico_util::core` links every shipped module except `testing`, so one include
and one link reach the whole library. A consumer that wants only one module may
link it by name. The module targets are exported for that purpose.

The include set of the umbrella header is the C++ standard library and the
platform libc, and nothing else, with no Pico SDK, RTOS or network stack (AR-1).
This is what lets the same headers serve host tests and firmware images without
conditional compilation at a consumer's call site (AR-2). It is also why this
repository exists separately and is not a corner of one of its consumers.

The section [What belongs in this library](#what-belongs-in-this-library) sets
out what the library admits.

## `HeapWatermark`

A claim of zero heap use after initialisation is an assertion until something
measures it. The obvious instrument is unavailable. The Pico SDK already wraps
`malloc` and `free` and overrides `operator new` and `delete` through
`pico_malloc` and `pico_cxx_options`, so a second per-allocation hook would be a
link conflict and not an observation.

The allocator's own accounting is available. `uordblks` is the number of bytes
currently in use. The library reads it through `mallinfo2()` where the C library
provides it and through `mallinfo()` otherwise (MR-2). The difference between an
armed baseline and a later reading is the net growth. `arm()` captures the
baseline. The caller can call it repeatedly, and each call replaces the previous
baseline and does not accumulate. `growth_bytes()` reports the difference,
clamped at zero so that a net shrink reads as no growth and an unsigned type
does not underflow.

The limit of the proxy is part of the requirement, and the note after the MR
table in the specification states it.

Before `arm()`, `growth_bytes()` returns 0, and `armed()` lets a consumer tell
that zero from a measured one. `measuring()` does the same for a build whose
every reading is 0, either because there is no `mallinfo` or because
AddressSanitizer's allocator is in use. The build decides its value and a
reading does not, because a working heap with nothing on it also reads 0.

## Two byte-order families

`byte_order.hpp` offers each accessor twice. The two families decode in the same
way and differ in where the bounds check lives.

The compile-time-offset family, `read_be_at<Offset, T>` and its three siblings,
takes a `std::span` of static extent and constrains the read to lie within it.
The constraint is `Offset <= Extent && sizeof(T) <= Extent - Offset`. It is not
`Offset + sizeof(T) <= Extent`, because that sum wraps for an `Offset` near the
maximum of the type and would admit an out-of-range read. An overrun is a
substitution failure, so it is a build error, and a `requires`-expression can
see it. That makes DR-2 testable without a runtime path to exercise. The
constraint also rejects a dynamic extent outright. `std::dynamic_extent` equals
`SIZE_MAX`, so a bound checked against it would pass for every offset and look
like protection that is not there.

The runtime-offset family takes a dynamic extent and checks nothing. Its callers
are framing layers that have already validated a length. Charging them for a
second check that cannot fail would push them back to hand-written shifts, which
is the duplication that the module exists to end. DR-3 states that the check is
absent, so the absence is a documented contract and not an omission.

Both families share one implementation. The compile-time family forwards to the
runtime family with `Offset` as the argument, because a static-extent span
converts implicitly to a dynamic-extent span. Only the constraint differs.

`as_bytes.hpp` is separate so a parser using only the wire accessors does not
also include `<string_view>`. It centralises the cast needed to send text to a
byte-oriented transport.

`WireInteger` excludes `bool` and the character types. A single byte has no byte
order, so reading one through an endian accessor would suggest a byte order that
does not exist.

## `Result` and the error type

`Result<T, E>` shares the container and leaves the error taxonomy to each
library. The container holds a value or an error and never both, is trivially
copyable whenever its value is, and offers `value_or` for a checked read. It is
the same everywhere. The taxonomy is not. A driver's bus failures and a parser's
malformed-input failures are separate vocabularies. Merging them would put
unrelated enumerators in one another's namespace and make this repository the
one that every consumer amends to add an error code.

`ok()` compares against `E{}`, so the zero enumerator of an error enum must be
its success state. That convention is the whole coupling between the container
and the taxonomy, and FR-1 states it so that nobody has to discover it.

`ErrorCode` admits only flat enums and integers. A rich error type would make
`Result` expensive to copy across the seams it serves.

The value and error types must not convert to each other. `Result<E, E>` would
give the value constructor and the error constructor one signature, which is
ill-formed, and the diagnostic would point inside this header at an overload
that the caller never wrote. A pair that converts in either direction is worse.
`Result<std::uint8_t, int>` built from `5` compiles, binds the error constructor
by conversion rank and yields a failure, with no diagnostic. A `static_assert`
makes any such pair fail to compile and names the fix. A constraint on the
constructors cannot do this: whatever stops the error constructor from claiming
the argument also removes the only way to report an error.

The family rule is that a library owns the taxonomy of the failures it can
produce and must not re-export a dependency's error type at its own public API.
A `PicoUtilError` in this library would break that rule from the bottom.

`Callback` sits beside `Result` because both cross a seam. A fallible call
returns a `Result`, and a notification that the caller cannot return through
goes out as a `Callback`.

## `FixedString`

`FixedString::append` is all-or-nothing because a partial append could leave a
message that looks complete but is not.

## Ring buffer and map

`RingBuffer` is the container that a transmit queue and a log sink both are. Its
wrap arithmetic is internal and invisible to a caller. It detects a full ring by
an explicit count and not by a reserved slot, which recovers the slot and keeps
a power-of-two constraint off the capacity. Synchronisation is the one thing a
caller chooses, through the `Counter` parameter.

`SpscCounter` keeps two counts, `produced_` and `consumed_`, and the element
count is their difference. A single shared count would need a read-modify-write
from both sides. ARMv6-M has no exclusive-access instructions, so on RP2040 a
`fetch_add` calls the SDK's atomic runtime, which takes a spinlock with
interrupts disabled, and an interrupt-context producer would take that lock on
every push. With one writer per count, each side advances its own count with a
plain load and store, which both architectures do natively.

The policy does not leak, because the count is the only value that both sides
share. Only the producer writes `head_` and only the consumer writes `tail_`, so
nothing else needs to carry a publication. `acquire()` and `commit()` let a
caller fill a slot in place, such as a formatted log line or a cargo payload,
and avoid a temporary.

A zero-copy byte transmit ring does not fit and is not forced to. It is a byte
stream with three regions and two independent cursors, and not a ring of
elements. `mark_sent()` reclassifies bytes from unsent to in-flight, only
`ack()` releases them, and `peek()` returns a contiguous run so that a network
stack can write it without copying. Fitting that onto an element ring would make
the core expose raw index arithmetic and contiguous-run computation that nothing
else uses, which is a leaky core. That ring shares `ring_index.hpp`, the
arithmetic alone, which both `RingBuffer` and the byte ring use.

The wrap arithmetic compares and resets and does not use a modulo or a
power-of-two mask. A division is expensive on a target without one, and a mask
would make "a power of two" a property of the container and not of the sizing
setting that chose the capacity. A position, a count and a capacity are all
element counts of the same type, so three adjacent `std::size_t` parameters
could be transposed and the call would compile and return a plausible wrong
answer. The capacity is therefore a template parameter, because every caller
knows it at compile time, and the position and the count are distinct types
(BR-8).

`FlatMap` serves fixed-capacity lookup by a caller-supplied key. It uses a
linear scan and an insert into a full map fails. At the single-digit to
low-double-digit sizes of the family's tables, a linear scan is smaller and
faster than a hash and needs no hashing support from the key. Entries stay in
their slots: packing them on erase would invalidate unrelated pointers held
across callbacks and relocate values that can be kilobytes wide.

A table that allocates generation-tagged handles is a slot allocator. Folding
that stale-handle contract into a map would add generation semantics it does
not use.

Live entries need not be contiguous, so `for_each` uses a callback rather than
a span over holes. Separate key and value references prevent key mutation
from leaving two entries under one key. The visitor is taken by value and
called repeatedly; forwarding it could move from it on the first call.

## Publication strategies

An interrupt timestamp is written by a handler and read in mainline. A mainline
seqlock reader can wait for that writer, because the handler completes without
being preempted by its reader. A clock offset written in mainline and read in an
interrupt has the opposite constraint. A spinning interrupt reader would wait
for its own suspended writer, so a slot ring bounds its read attempts instead.

Neither strategy is better, so `Published` offers both and does not pick one.
The primary template is declared and not defined, and a declaration that names
no strategy does not compile. The friction is intended. Both strategies compile
for any `T` and both pass a single-threaded host test, so a wrong choice stays
invisible until it appears as a hang in an interrupt handler or a torn read on
the target.

The seqlock writer brackets its store with a counter that is odd during the
write and even between writes. The reader retries when the count is odd or
changes, because either means a write was in progress during the read.

The slot ring publishes a store counter and not an alternating index. An index
can return to the value that the reader first saw, with a write to that same
slot in between, and the check would then pass over a torn read. The store
counter repeats only after 2^32 stores. Within a read during which fewer than
2^32 stores land, an unchanged counter therefore means that no store landed.

The bound of the slot ring rests on its writer contract and not on its retry
count. The reader tolerates the writer advancing by up to `SLOT_COUNT - 2`
stores, and each extra slot adds one store of tolerance. The writer fills the
slot after the one it last published, so it takes `SLOT_COUNT - 1` advances to
reach the slot being read, and equality would be too strict a test. A writer
that stores in a tight loop laps a four-slot ring inside a single read, though,
and no ring of any depth survives arbitrary laps. The contract is therefore
stated on the enumerator and required by CR-4. What holds unconditionally is
that the reader returns and that a value it returns as a success is whole. A
read that exhausts its attempts is returned as an error and not handed back
possibly torn (CR-3). The validation predicate is a function that a test checks
at its boundaries without depending on timing. Whether a concurrent writer laps
a read part-way through does depend on timing, so a concurrent case alone cannot
prove the arithmetic right.

Reporting exhaustion through `Result` is the one place where a module here
depends on another: `publish` links `result`.

Both strategies order their words without standalone fences. The writer
stores each word with release and the reader loads each word with acquire. A
reader that loads a word from a later store therefore sees a counter write made
before that store when it re-reads the counter, and rejects the read. An
acquire load of the counter alone would not do this: it stops later accesses
moving earlier, but the validation needs the word loads not to move after the
counter re-read. ThreadSanitizer does not model standalone fences, and GCC warns
about them under `-fsanitize=thread` (`-Wtsan`), which fails this build.

The cost falls on ARMv6-M (RP2040), where each word access carries its own
`dmb`: a store or a read attempt executes N + 2 barriers for an N-word value,
where fences needed 3. ARMv8-M (RP2350) uses its load-acquire and
store-release instructions and executes no `dmb`.

Both strategies carry the payload as 32-bit atomic words and `memcpy` through a
properly aligned local. This keeps a 64-bit payload off `__atomic_load_8`, which
the SDK implements as a global spinlock with interrupts disabled, the opposite
of what either strategy is for. The value is assembled in a local, so an
over-aligned `T` works, because the alignment of the word array never reaches
it.

## Allocation harness

`allocation_operators.ipp` defines the replaceable allocation functions, and the
standard requires exactly one definition of each in a program. They cannot be
`inline`, so the file is a translation unit in the form of a header. Include it
in one translation unit per test executable. A second include is a
duplicate-symbol link error. That loud failure is intended, because a quiet one
would mean two counters and a test that passed by accident.

For this reason the file is outside the umbrella and outside both cross probes,
and `pico_util::core` does not link `pico_util::testing`. Reaching the
definitions through an umbrella include would put them into every translation
unit that included `pico_util.hpp`. The counting header,
`allocation_counter.hpp`, is inline and harmless anywhere. The umbrella never
reaches it either, so it has a cross probe of its own,
`tests/firmware/testing_probe.cpp`.

The replacement is complete. All twenty forms reach the same `malloc`,
`aligned_alloc` and `free`. A partial replacement could return a block through
another allocator's freeing function, and complete replacement keeps all twenty
forms consistent.

A replaced `operator new` takes no context argument, so the counter state has to
be reachable from a free function. Function-local statics give that without a
namespace-variable suppression.

The harness ships in the library and not under `tests/` because its consumers
are the test trees of other repositories. A library that claims a path allocates
nothing links the harness to prove the claim.

## Allocation

Nothing in the library allocates, except the `testing` harness, whose subject is
the allocator and which no utility links. Every utility is header-only and
depends on nothing beyond the C++ standard library and the platform libc, so a
constrained consumer can link one without inheriting a policy.

`HeapWatermark` is about allocation and performs none. It observes the heap
through what the C library already reports, for the reason given in the
[`HeapWatermark`](#heapwatermark) section, and allocates nothing itself.

## mallinfo and mallinfo2

glibc 2.33 added `mallinfo2()`, whose fields are `size_t`, and deprecated
`mallinfo()`, whose fields are `int`. newlib is the firmware libc and has only
the older spelling. The header keeps both, selects between them by preprocessor
feature detection, and uses the deprecated one only where `mallinfo2` is
absent. A platform that provides neither still compiles. `in_use_bytes()`
returns 0 and growth is always 0, and the header does not fail to compile.

The detection flags are macros and not `constexpr` constants because they have
to be testable by `#if`, which a constant cannot be. That is the whole reason
for the `cppcoreguidelines-macro-usage` suppression on them. The caller owns
thread safety, which `mallinfo` lacks. See [Limits owned by the
caller](#limits-owned-by-the-caller).

## Include order

`<cstdlib>` must come before `<malloc.h>`. newlib declares the malloc family
with `_NOTHROW` in `<stdlib.h>` and without it in `<malloc.h>`. Clang models
that attribute as an exception specification and rejects whichever declaration
adds one to an existing declaration.

The `<cstdlib>` include therefore sits in the first block, under a comment that
says why, and not beside `<malloc.h>`, where it would belong by subject. Beside
`<malloc.h>`, the include sort would rank the C header above it and silently
restore the bad order.

## The version pin

`VERSION_MAJOR`, `VERSION_MINOR` and `VERSION_PATCH` in the umbrella header and
`project(pico_util VERSION ...)` record one fact twice. The test target receives
the CMake values as compile definitions and compares the constants of the header
against those. A literal copied from the header would compare the header with
itself.

An `INTERFACE` library has no translation unit, so there is no firmware-side
`static_assert` to match. Putting the definitions on the `INTERFACE` target
would propagate build-time values into every installed consumer.

## What belongs in this library

Every module here has one thing in common: two or more repositories were already
maintaining a version of it. The admission test is two conditions at once.

First, two or more repositories have each implemented the utility
independently. The test is not that two repositories consume it. A primitive
that each repository wrote for itself is already maintained more than once, and
waiting until two of them link the same header would delay resolving the
duplication until it could be counted. A utility that one repository invented
and nobody else reached for belongs in that repository, where it can change
without a release here.

Second, the utility can be written under AR-1, with the C++ standard library and
the platform libc and nothing else. A utility that needs the Pico SDK, an RTOS
or a network stack fails this condition and belongs to another project.

The first condition keeps the library from becoming a drawer. The second is what
lets the same headers serve host tests and firmware images, which is the only
reason a shared utility repository is cheaper than duplication.

## What is deliberately not here

There is no `NonCopyable` base for the four deleted special members that a class
declares to forbid copying and moving. Deleting them in place is clearer than an
inherited base with its empty-base and `final` interactions, and an inherited
base saves four lines a site.

## Limits owned by the caller

Thread safety belongs to the caller. `mallinfo` is not thread safe and this
library does not serialise it. The consumer's contract is to call it from a
single thread. Serialising it here would mean choosing a lock, and no lock is
available under AR-1 that is also correct on the target. A `std::mutex` on
firmware is not the primitive that a consumer with an RTOS or a second core
wants, and a spinlock is an SDK dependency. Leaving the contract with the caller
keeps the choice where the information is.

The host reading has a second limit. It misses what glibc keeps outside its
main arena, which the specification records. Making the host reading exact
would mean summing arenas, which glibc does not expose portably.

## No compiled surface

The library is `INTERFACE` and header-only. A translation unit would change what
the library costs a consumer by adding an object to link, static storage and an
initialisation order that the library currently contributes nothing to. A
utility that needs a compiled translation unit is a signal that it belongs
somewhere else.

`allocation_operators.ipp` looks like an exception and is not. It becomes part
of a consumer's translation unit by design, because the replaceable allocation
functions cannot be `inline`. The library still compiles nothing. The consumer's
own test executable includes the file in exactly one translation unit, and it
reaches neither the umbrella nor either cross probe. The extension `.ipp`
marks the file as a translation unit, and
[orientation.md](orientation.md#repo-specifics) gives the reason it is not
`.hpp`.
