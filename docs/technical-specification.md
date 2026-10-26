# pico-util Technical Specification

**Status:** Normative.

This specification defines the requirements for pico-util. Every requirement
has an ID and is mapped to a test in the traceability matrix.

---

## 1. Scope and targets

| | |
|---|---|
| Targets | Host (glibc), and the cross build (newlib) for RP2040 (Cortex-M0+) and RP2350 (Cortex-M33) |
| Contents | Header-only utilities shared by the pico-* family and free of any SDK dependency: heap-growth observability, wire-format byte access, fallible-return and callback primitives, fixed-capacity containers and the ring wrap arithmetic beneath them, cross-context value publication, and an allocation-counting test harness; the installed CMake package a consumer finds them through, and the version constants it can check them by |
| Out of scope | Anything requiring the Pico SDK, an RTOS, a network stack, or dynamic allocation of its own |

**API stability:** the public API follows semantic versioning; breaking changes
at a major boundary, or a minor boundary below 1.0 (EN-06.1).

A utility belongs here when two or more pico-* repositories have each
implemented it independently, it depends on nothing but
the C++ standard library and the platform libc, and it carries no policy of its
own.

Independent implementation, rather than a shared consumer, is what the first
condition asks for. A primitive each repository wrote for itself is already
being maintained more than once; waiting until two of them link the same header
would mean the duplication has to be resolved before it can be counted.

## 2. Constraints and assumptions

The facts that drive every requirement below.

- **The Pico SDK already owns the allocator.** `pico_malloc` wraps
  `malloc`/`free` and `pico_cxx_options` overrides `operator new`/`delete`. A
  second per-allocation hook cannot be linked alongside them, so any allocation
  observability this library offers must be derived from what the C library
  already reports rather than from interception.
- **The two target libcs report the heap differently.** glibc 2.33 deprecated
  `mallinfo()` in favour of `mallinfo2()`, whose fields are `size_t` rather than
  `int`. newlib — the firmware libc — provides only `mallinfo()`.
- **A platform may provide neither.** `<malloc.h>` is not guaranteed to exist.
- **Consumers are single-threaded on the path that reads these utilities.**
  `mallinfo` is not thread safe, and this library does not serialise it. The
  publication utilities are the exception and say so: they exist precisely to
  carry a value between two contexts.
- **The target is 32-bit.** A value wider than a word cannot be stored or loaded
  atomically, and a 64-bit atomic operation on it is not lock-free — the Pico SDK
  implements `__atomic_load_8` as a global spinlock with interrupts disabled.

## 3. Resource budgets

Header-only: this library contributes no translation unit, no static storage
and no initialisation order of its own to a consumer's image. The `testing`
module is the exception, and is not part of that image: it replaces the global
allocation functions and keeps its count in function-local statics, which is
what a harness counting a whole program's allocations has to do, and it is
linked only into a consumer's test executables. Every container's
storage is a `std::array` member sized at instantiation and held by the consumer,
so a container's cost appears in whatever holds it and nowhere else. `HeapWatermark`
holds one `std::size_t` and one `bool`, held by the
consumer.

**Normative gate.** No utility in this library may allocate, the `testing`
harness aside, whose subject is the allocator. Enforced by the
host suite: a representative operation of every shipped utility runs under an
armed allocation guard and counts zero. BR-6 states the guarantee and its row
states which operations the subset reaches; this paragraph does not promise
exhaustiveness the harness does not have.

The cross-compiled header probe is bounded at <= 8 KiB of static RAM and
<= 32 KiB of flash (every `LOAD` section), gated by the footprint gate.
Static RAM counts `ALLOC` sections except `LOAD, READONLY` sections with
equal VMA and LMA, `.heap` and `.stack*` reservations (pico-tooling:AR-11).
Both halves, because a header that starts carrying static state shows up in
one and a header that starts carrying tables shows up in the other; newlib's
malloc and stdio structures dominate the RAM figure, so the budget is set to
catch a step change rather than to police a few hundred bytes.

## 4. Architecture and portability requirements (AR)

| ID | Requirement |
|---|---|
| AR-1 | Every utility shall be header-only and shall depend on nothing beyond the C++ standard library and the platform libc — no Pico SDK, no RTOS, no network stack. |
| AR-2 | The same headers shall compile for the host and for the cross build without conditional compilation at the consumer's call site. |
| AR-3 | The installed package shall be reachable by `find_package(pico_util)` and shall export every header a consumer needs. |

## 5. Memory and allocation requirements (MR)

| ID | Requirement |
|---|---|
| MR-1 | `HeapWatermark` shall report net C-heap growth since an explicitly captured baseline, so a consumer can gate a "zero post-init heap" claim on a falsifiable measurement rather than an assertion. |
| MR-2 | `HeapWatermark` shall read the heap through `mallinfo2()` where the platform provides it and `mallinfo()` otherwise, and shall report 0 where it provides neither, rather than failing to compile. |
| MR-3 | `arm()` shall capture the current usage as the baseline and may be called more than once; each call replaces the previous baseline rather than accumulating against it. |
| MR-4 | `growth_bytes()` shall clamp a net shrink to 0 rather than underflowing, and shall report 0 before `arm()` has been called. |
| MR-5 | The library shall provide an allocation-counting test harness that replaces **every** global allocation and deallocation function — scalar, array, sized, nothrow and over-aligned — so no form is left with the implementation's own allocator. |
| MR-6 | The harness shall count only while explicitly armed, and shall arm and disarm by scope, so a test framework's own allocations fall outside the measurement. A guard constructed inside a live one shall abort rather than silently disarming the enclosing scope, since a nested guard would otherwise end the outer measurement at its own scope exit and report a count for a window the outer test did not choose. |
| MR-7 | The replacement being complete, the harness shall remain valid under a sanitizer that intercepts `malloc` and `free`, and shall not be compiled out under one. A partial replacement lets a block from one allocator reach another's deallocation function, which is the defect that otherwise forces the harness to be disabled exactly where it would be most useful. |
| MR-8 | `HeapWatermark::armed()` shall report whether a baseline has been captured, and `HeapWatermark::measuring()` whether the heap can be read at all, so a consumer can distinguish "nothing grew" from "there is no baseline" and from "there is no reading". Without them a forgotten `arm()`, or a platform whose every reading is 0, reads as a clean run, which is the one wrong answer a growth proxy must not give. |
| MR-9 | `AllocationGuard::count()` shall remain readable after the guard disarms and until the next guard arms, so a check written after the guarded scope reads the count that scope produced. Most spellings of the assertion sit outside the scope, so a count cleared at scope exit would make the harness unusable in the shape its consumers actually write. |

**MR-5..MR-7 are what make a no-allocation claim falsifiable.** MR-1's watermark
answers the coarse question on a running image; the harness answers the exact one
in a test. Its own first consumer is this library's containers, whose BR-6 is
established by it.

**MR-1's limit is part of the requirement.** Net growth cannot see an
allocate/free pair that nets to zero bytes within one observation window. It is
a proxy, and the specification says so rather than claiming detection it does
not have. What it does catch — and report the size of — is steady-state growth,
which is the failure mode a long-running image actually has.

There is a second limit, and it is a host one. `uordblks` counts glibc's main
arena, so an mmap-backed allocation above the threshold is invisible to the
proxy there. Newlib has one arena, so the limit does not reach the target;
summing arenas is not portably available in any case.

## 6. Observability requirements (OR)

| ID | Requirement |
|---|---|
| OR-1 | The advertised version constants shall equal the version the build system holds. |

## 7. Decoding and encoding requirements (DR)

| ID | Requirement |
|---|---|
| DR-1 | The library shall read and write 16-, 32- and 64-bit integers, signed and unsigned, in both big-endian and little-endian representations, independently of the host's byte order. |
| DR-2 | The compile-time-offset accessors shall take a static-extent span and shall constrain the offset against that extent, so a field running past the end of the buffer is a build error rather than a runtime check. A dynamic-extent span carries no size the compiler can check and shall not bind to them at all. |
| DR-3 | The runtime-offset accessors shall take a dynamic-extent span and shall not bounds-check the offset. The caller's own length validation is the precondition, and this requirement states that rather than implying a check the code does not perform. |
| DR-4 | Every accessor shall be `constexpr`, so a wire layout can be decoded or built at compile time. |
| DR-5 | Viewing text as the bytes a transport speaks shall be available without copying, and the reinterpretation it requires shall exist at exactly one site in the library. |

**Why both families exist.** A fixed protocol header has a size known at compile
time, and DR-2 turns an over-run in one into a build error. A framing layer
walking a buffer whose length it has already validated does not, and DR-3 refuses
to charge it for a second check it cannot fail. Offering only the checked family
would push that caller back to hand-written shifts, which is the duplication this
module exists to end.

## 8. Functional requirements (FR) — fallible returns and callbacks

| ID | Requirement |
|---|---|
| FR-1 | `Result<T, E>` shall hold either a value or an error and never both, shall be parameterised on the caller's error type so the taxonomy stays in the library that owns the failures, and shall treat the value-initialised `E{}` as success. `T` and `E` shall not be the same type, nor convertible to each other. |
| FR-2 | `Result<T, E>` shall convert implicitly from a value and from an error, so a fallible function returns either without naming `Result` at the return site. |
| FR-3 | `Result<void, E>` shall carry success or an error and nothing more, and shall default-construct to success. |
| FR-4 | `Result<T, E>::value_or` shall return the caller's fallback on an errored result rather than the default-constructed value, which means nothing. |
| FR-5 | `Callback<Ret(Args...)>` shall be a function pointer plus an opaque context pointer — no allocation, no virtual dispatch. A `Callback<void(Args...)>` shall default-construct empty and be safe to invoke, as a no-op. A `Callback` with any other `Ret` shall have no unbound state: no default constructor, and a null target refused in a build with assertions enabled. An unbound callback returning a value could only return `Ret{}`, which for a `Result` is success — an unset sink reporting that the data went through. |
| FR-6 | `Result<T, E>` shall be `[[nodiscard]]`, so discarding the only report a failed operation makes is a diagnostic rather than a statement that compiles. The attribute is on the class, which covers every function returning one including those written later; a caller that does not want the result casts to `void`. |
| FR-7 | `Result<T, E>`'s error type shall be constrained to an enumeration or an integer, so an unsupported vocabulary is a diagnostic at the declaration rather than an error deeper in the template. The constraint is part of the contract: a consumer with a struct error type is refused, and is refused where it can be read. |
| FR-8 | `Result<T, E>` shall be trivially copyable whenever `T` is, and shall cost the value plus a flat discriminator. This is what lets it cross a HAL boundary without allocation and be returned in registers, and every consumer that returns one from a hot path depends on it. A `T` that is not trivially copyable makes a `Result` that is not, which is the value's cost rather than the container's. |
| FR-9 | Reading the value of an errored `Result<T, E>` shall be a caller error, asserted in a build with assertions enabled. As BR-10, the mechanism is `assert` and `NDEBUG` removes it; the guarantee is that the mistake is caught where it is made during development, not that the read is checked in a shipped image. |
| FR-10 | `Callback<Ret(Args...)>` shall provide a `bind` that forms one from an object and a non-const member function, and shall own neither. The object outlives the callback or the callback is not invoked; the library holds two raw pointers and cannot check either. A const member function has a different pointer type and does not bind. |
| FR-11 | Constructing a failure from `E{}` shall be a caller error, asserted in a build with assertions enabled. `E{}` is the success state, so the result would be an `ok()` `Result<T, E>` holding a value nobody computed — a fabricated success that passes every later check. `Result<void, E>` is exempt: there `E{}` is a success being propagated, and it is accepted. As FR-9, `NDEBUG` removes the check. |

**Why the error type is a parameter.** A driver's bus failures and a parser's
malformed-input failures are not one vocabulary. Fixing a taxonomy here would
make this specification the document every consuming library has to amend to add
an error code, and would put unrelated enumerators in one another's namespace.
What is shared is the container and the `E{} == success` convention; what varies
is the enum, and it stays where the failures are.

## 9. Boundary and capacity requirements (BR)

| ID | Requirement |
|---|---|
| BR-1 | `FixedString<N>` shall hold at most `N - 1` characters, shall be NUL-terminated at all times including after truncation, and shall report whether an assignment truncated. There shall be no construction path that truncates without saying so: construction from text is a named factory whose name states the loss, not a converting constructor that discards the report. |
| BR-2 | `FixedString<N>::append` shall be all-or-nothing: an append that would not fit shall leave the string unchanged and report failure, never a partial append. |
| BR-3 | `FlatMap<K, V, N>` shall hold at most `N` entries, shall replace the value of a key already present rather than adding a second entry for it, and shall refuse a new key at capacity rather than evicting one. A visitor shall not be able to rewrite a key: two live slots under one key are unreconcilable by `find`, `insert_or_assign` or `erase`. |
| BR-4 | `FlatMap<K, V, N>::erase` shall remove a key and leave every remaining key findable. **An entry shall never move**: a pointer or reference to one stays valid until that entry is itself erased, whatever is inserted or erased in between. Entry order is not part of the contract. A caller holding a pointer across a callback, or storing a value too large to relocate cheaply, depends on this and cannot check it. |
| BR-5 | `RingBuffer<T, N, Counter>` shall hold at most `N` elements in FIFO order and shall refuse a push at capacity rather than overwriting the oldest element. `Counter` shall have no default, so the synchronisation the buffer assumes is named at every declaration. A count driven past `N` by a caller bug shall leave the ring refusing rather than overwriting live elements. |
| BR-6 | No utility in this library shall allocate, the `testing` harness aside, whose subject is the allocator: a container's storage is a `std::array` member sized at instantiation, and every operation runs within it. |
| BR-7 | Every operation that refuses at capacity shall be `[[nodiscard]]`, so discarding the refusal is a diagnostic rather than a statement that compiles. The guarantee below is otherwise available to a caller rather than made to one: the report exists, but nothing obliges the caller to receive it. This is FR-6 applied to capacity. |
| BR-8 | A ring position and a count of ring elements shall not be interchangeable. The wrap arithmetic shall take the capacity as a template parameter, since every caller has it as a compile-time constant, and shall distinguish a position from a count by type, so that a transposed call fails to compile rather than returning a plausible wrong answer — including one written with braced arguments that name neither type. |
| BR-9 | `ring_run<Capacity>(start, length)` shall report the contiguous run readable from `start` without wrapping: the smaller of `length` and the distance from `start` to the end of the storage. A caller copying that many elements from `start` shall never read past the end of the buffer, so a single `memcpy` is correct without the caller re-deriving the bound. |
| BR-10 | `ring_index` and `ring_run` shall assert their preconditions in a build with assertions enabled: an offset larger than the capacity, or a start position at or past the capacity, is a programming error and is caught where it is made rather than producing an index that silently aliases a live element. The mechanism is `assert`, so `NDEBUG` removes it and a violation in a release build is undefined. The guarantee that survives `NDEBUG` is BR-8's, which is a property of the types rather than of a runtime check — which is why BR-8 rather than this requirement is the answer to the transposition hazard. DR-3's reasoning applies: a caller that has already established the bound is not charged for a second check it cannot fail. |
| BR-11 | `ring_index<Capacity>(from, offset)` shall return the position `offset` past `from`, wrapped at `Capacity`. This is the whole purpose of the function and the operation every ring cursor in the family is built from; BR-8 constrains its signature and BR-9 describes `ring_run`, so without this row the arithmetic itself is specified nowhere. |
| BR-12 | `RingBuffer<T, N, Counter>::acquire` shall return a pointer to the next free slot for the caller to fill in place, valid until the matching `commit()` or any other producer-side call, and `nullptr` at capacity. Filling in place is what keeps a whole-element temporary off the producer's stack, which for an interrupt handler is the difference between usable and not. |
| BR-13 | `FixedString<N>::view` shall alias the object's own storage rather than copying, and the view shall be invalidated by the next `assign`, `append` or `clear`. This is the same pointer-lifetime rule BR-4 states for `FlatMap`, and a caller holding a view across a mutation cannot check it. |
| BR-14 | `FlatMap<K, V, N>::erase` shall reset the released value rather than leaving it in place, so a slot that is no longer live does not keep whatever the last value owned alive until the slot is reused. |
| BR-15 | `FlatMap<K, V, N>::for_each` shall allow the visited entry to be erased during the walk. Inserting during a walk is undefined: an insert may claim a slot the walk has already passed or is standing on, and the visitor would then see neither a consistent snapshot nor the full contents. |
| BR-16 | `FlatMap<K, V, N>`'s `try_emplace`, `erase` and `clear` shall reset a value where it lives rather than assigning a temporary over it, so no whole-value temporary is built on the caller's stack. A map exists to hold values too wide to pass by value, and a temporary of one is a stack frame the size of the value on every insert and erase, and the size of the whole table on `clear`. `V`'s default constructor shall be `noexcept`, since a reset that destroys and then constructs has nothing to fall back on if construction throws. |

**Refusal, not eviction.** Every container here refuses at capacity and reports
it. A container that silently evicted would make a caller that must not lose data
unable to tell that it had, and the caller that may drop data can still do so
having been told. This is the same reasoning as `CXX-07.2` — reject, never
truncate — applied to capacity rather than to input.

**Why `Counter` has no default.** Both policies compile for any `T`, and a
single-context ring used across two contexts passes every single-threaded host
test it is given. The defect appears only on the target and only under
preemption, so a policy chosen by omission is one nothing can catch. Naming it at
the declaration is the check.

## 10. Concurrency requirements (CR)

| ID | Requirement |
|---|---|
| CR-1 | `Published<T, Strategy>` shall have no default strategy. Both strategies compile for any `T` and both read correctly under a single-threaded host test, so a strategy selected by omission would be wrong only on the target and only under preemption. |
| CR-2 | The seqlock strategy shall be correct where the writer cannot be preempted by the reader — an interrupt handler writes, mainline reads. A reader shall observe only a value that was published whole, retrying while a write is in progress. |
| CR-3 | The slot-ring strategy shall be correct where the reader can preempt the writer — mainline writes, an interrupt handler reads. The reader shall never spin: it makes a bounded number of attempts and then returns. `load()` shall return a `Result`, and a value it returns as a success shall have been published whole, whatever the writer does; a read that exhausts its attempts shall be reported as `PublishedReadError::EXHAUSTED` rather than returned possibly torn. Exhaustion is unreachable under the writer contract in CR-4, so the error is how a contract violation is reported. |
| CR-4 | With one writer, in mainline context, completing at most `SLOT_COUNT - 2` stores during a read, a slot-ring `load()` shall succeed. That is the writer contract a consumer owes: stores rare relative to a read's duration. A writer that laps the ring within a single read defeats a ring of any depth, so the contract is what makes the bound sufficient, not the retry count. An interrupt-context writer breaks the contract, and is what the seqlock strategy is for. |
| CR-5 | Both strategies shall carry the published value in 32-bit atomic words, so a payload wider than a word needs no wider atomic operation and therefore takes no lock on a 32-bit target. Neither strategy performs a read-modify-write, so on ARMv6-M — which has no exclusive-access instructions and therefore reports `is_always_lock_free` false for every type — the property asserted is that a 32-bit atomic is no wider than the value it holds. |
| CR-6 | `Published<T, Strategy>::published()` shall report whether a value has ever been stored, and shall never become false again once true. A consumer builds its own "is this synchronised" predicate directly on it, so a latch that could clear would make that predicate lie at exactly the moment it matters. |
| CR-7 | `Published<T, Strategy>` shall require `T` to be trivially copyable, enforced at class scope rather than on the store path, so a consumer with an unsupported payload is refused at the declaration rather than at the first call. |
| CR-8 | `Published<T, Strategy>::load()` shall return a value-initialised `T` before the first `store()`, so a reader that runs before the writer gets a defined value rather than whatever the storage held. CR-6 is how the reader tells that value from a published one. |
| CR-9 | `RingBuffer<T, N, SpscCounter>` shall publish each side's progress with a release store and observe the other side's with an acquire load, so the slot contents a producer wrote are visible to a consumer that sees the element, and a slot a consumer read is not reused by a producer that sees it freed. It shall do so without a read-modify-write, so the counter takes no lock and calls no runtime helper on either target: the producer and consumer each advance a count only they write. This is what naming `SpscCounter` buys and the reason BR-5 refuses a default. |
| CR-10 | The slot ring shall hold `SLOT_COUNT = 4` slots, make at most `MAX_READ_ATTEMPTS = 3` read attempts, and accept a read across which fewer than `SLOT_COUNT - 1` stores completed. The three values are public, and a consumer states its own writer contract in terms of them: exhausting a read takes `SLOT_COUNT - 1` stores inside every attempt. Changing any of them changes what that contract has to promise, so they are requirements rather than implementation. |

**The axis is which side preempts which**, and it is why there are two
strategies rather than a best one. A seqlock reader spins until the writer
finishes, which terminates only where the writer cannot be preempted by that
reader. A slot-ring reader never spins, which is what an interrupt handler
needs, and pays for it with a bound that holds only under CR-4's contract.
Choosing wrongly yields a hang in an interrupt handler or a torn read, and
neither appears in a single-threaded host test — which is what CR-1 is for.

## 11. Traceability matrix

Every ID above appears here with the test that establishes it and the criterion
that test applies. Review checks that each ID defined in
this document is cited in code or tests.

| ID | Established by | Criterion |
|---|---|---|
| AR-1 | `src/include/pico_util/pico_util.hpp` — the include set; the host build and the cross-compiled header probe | The umbrella pulls in no SDK, RTOS or network header. What fails when one appears is the host build, which has none on its include path, and the cross probe, which links no SDK library. |
| AR-2 | CI `build` and `firmware` jobs; `tests/firmware/header_probe.cpp`, `tests/firmware/testing_probe.cpp` | The same headers compile under both toolchains with no consumer-side conditionals, and the cross probes link with no SDK library: `header_probe.cpp` through the umbrella, and `testing_probe.cpp` the allocation harness's counting header, proving compilation compatibility of the header, guard and count only; host allocation tests establish allocation behavior. |
| AR-3 | `tests/consumer/` — `consumer_smoke`, built and run by the `consumer` gate | A separate CMake project resolves `find_package(pico_util)` and compiles against the installed headers alone. The `accepts-0-1` case accepts 0.1; `refuses-0-2` refuses 0.2 at configure. |
| MR-1 | `tests/heap/heap_watermark_test.cpp` — `ReportsGrowthHeldAfterArming` | A held 64 KiB block reports growth of at least its own size; releasing it drops the reading below that. |
| MR-2 | `src/heap/include/pico_util/heap/heap_watermark.hpp` — the `PICO_UTIL_HAVE_MALLINFO*` arms; `tests/heap/heap_watermark_test.cpp` skips where `measuring()` is false | On glibc 2.33 or later `heap_watermark_test.cpp` fails to compile unless the header took the `mallinfo2()` arm, and the suite passes there. Newlib's `mallinfo()` arm is compiled and linked by the cross-compiled header probe and not run. The arm for a libc with neither function is compiled nowhere and is review-only. |
| MR-3 | `tests/heap/heap_watermark_test.cpp` — `ArmingLatchesTheBaseline`, `ReArmingRebaselines` | Growth reads 0 immediately after each `arm()`. |
| MR-4 | `tests/heap/heap_watermark_test.cpp` — `NetShrinkClampsToZeroRatherThanUnderflowing`, `ReportsNoGrowthBeforeArming` | Growth reads 0 below the baseline and before arming, never a wrapped value. |
| MR-5 | `tests/testing/allocation_test.cpp` — `TheCounterObservesAnAllocation`, `ArrayAndOverAlignedFormsAreCounted`; ctest `pico_util_testing_every_form_replaced` | A 1 KiB `std::string` moves the count off zero, so a harness whose operators were never linked fails rather than reporting zero for everything; `operator new[]`, called by name, and an `alignas(64)` allocation are each counted; the harness executable defines all twenty replaceable allocation and deallocation functions. The standard's default nothrow and array forms forward to the scalar ones, so a missing form is visible only as a missing symbol. |
| MR-6 | `tests/testing/allocation_test.cpp` — `CountingIsScopedToTheGuard`, `ConcurrentAllocationsHaveAnExactTotal`; `tests/testing/allocation_guard_test.cpp` — `NestingIsRefused`, `AGuardDisarmsSoALaterGuardMayArm` | An allocation inside a guard is counted, an allocation after the guard closes leaves the tally unchanged, and a second guard starts from zero rather than accumulating — all three where the replaced operators are linked, which is the only translation unit whose count can move. A guard disarms on destruction so a later one may arm, asserted on the armed flag. Two joined worker threads contribute exactly 20,000 allocations under one guard. A guard constructed inside a live one aborts on both the Debug and the Release host legs. |
| MR-7 | `tests/testing/allocation_test.cpp` — `EveryFormRoundTripsThroughOneAllocator`, run on the CI sanitizer leg | Each of the eight allocation forms, called by name, is counted once, and each of the twelve deallocation forms releases one of them with no `alloc-dealloc-mismatch` reported; the suite is not compiled out under the sanitizer. |
| MR-8 | `tests/heap/heap_watermark_test.cpp` — `ReportsNoGrowthBeforeArming`, `ArmingLatchesTheBaseline`, `MeasuringAgreesWithTheReading`; the `pico_util_heap_fresh_measuring` test | `armed()` is false before `arm()` and true after, and `growth_bytes()` reports 0 while it is false; with a live block held, `measuring()` true comes with a nonzero reading, on the host leg, and false with a zero one, on the sanitizer leg; and a program arming over a heap it first confirms is empty is still told it is measuring. |
| MR-9 | `tests/testing/allocation_test.cpp` — `TheCountOutlivesTheGuard` | An allocation made inside a guard is still counted after the guard's scope closes. Read in the translation unit that replaces the allocation functions, where a destructor that cleared the count would make the read zero and fail. |
| OR-1 | `tests/core/pico_util_test.cpp` — `VersionMatchesTheBuildSystem` | The header constants equal `PROJECT_VERSION_*` fed in by CMake. |
| DR-1 | `tests/bytes/byte_order_test.cpp` — `RoundTripsEveryWidthInBothOrders`, `TheTwoOrdersAreMirrorImages`, `DecodesTwosComplementNegatives`, `MatchesKnownWireVectors` | Each width round-trips in both orders; the two orders decode the same bytes to mirrored values; a two's-complement field decodes negative; a known big-endian NTP field and a known little-endian SHTP header decode to their documented values. |
| DR-2 | `tests/bytes/byte_order_test.cpp` — `AnOverrunningOffsetIsIllFormed`, `CompileTimeOffsetWritesTheNamedField` | A field ending at the buffer's end is well-formed; one running past it, one wider than the buffer, and a dynamic extent are each rejected by the constraint rather than at run time. |
| DR-3 | `tests/bytes/byte_order_test.cpp` — `RuntimeOffsetReadsTheNamedField` | A non-zero runtime offset reads and writes the field at that offset, in both the big- and the little-endian family, and leaves the bytes either side untouched. That no bounds check is made is review-only. |
| DR-4 | `tests/bytes/byte_order_test.cpp` — `DecodesAtCompileTime` | A `static_assert` reads a literal buffer through each of the four readers and builds buffers through each of the four writers, so any accessor that is not `constexpr` fails the build. |
| DR-5 | `tests/bytes/as_bytes_test.cpp` — `ViewsTextWithoutCopying`, `EmptyTextYieldsAnEmptySpan`, `HighBytesReadAsUnsigned` | The returned span's `data()` equals the source's and its size matches; empty text yields an empty span rather than one over a null pointer; a high byte reads unsigned rather than sign-extending. |
| FR-1 | `tests/result/result_test.cpp` — `HoldsAValueOrAnErrorNeverBoth`, `IsTriviallyCopyableAndCheap`, `ParameterisedOnTheErrorType`, `AcceptsAnIntegralErrorType`; ctest `pico_util_refuses_result_same_type`, `pico_util_refuses_result_convertible_types` | `ok()` and `error()` agree in both states; the type is trivially copyable and costs the value plus a flat code; two unrelated error types instantiate in one translation unit; an integral `E` works alongside a `T` that does not convert to it; one type in both roles, or two that interconvert, fails to compile with the `static_assert`'s message. |
| FR-2 | `tests/result/result_test.cpp` — `ReturnsAValueOrAnErrorWithoutNamingResult` | A function returning `Result<int, E>` compiles with `return 7;` and with `return E::TIMEOUT;`, neither naming `Result`. |
| FR-3 | `tests/result/result_test.cpp` — `VoidResultDefaultsToSuccess` | A default-constructed `Result<void, E>` is `ok()`; one constructed from an error is not. |
| FR-4 | `tests/result/result_test.cpp` — `ValueOrReturnsTheFallbackOnAnError`, `WorksInAConstantExpression` | An errored result yields the caller's fallback, distinguishable from a default-constructed value; an ok one yields the held value; both hold in a constant expression. |
| FR-5 | `tests/result/callback_test.cpp` — `EmptyCallbackIsANoOp`, `AValueReturningCallbackHasNoUnboundState`, `ANullTargetForAValueReturningCallbackIsRefused`, `InvokesAFreeFunctionWithItsContext`, `IsTriviallyCopyableAndPointerSized` | An unbound `Callback<void(int)>` is safe to call; a `Callback<int(int)>` and a `Callback<Result<void, E>(int)>` are not default-constructible, asserted on the trait; a null target handed to a value-returning one aborts, **Debug only** as FR-9; a bound one reaches its context; the type is two pointers and trivially copyable. |
| FR-6 | ctest `pico_util_refuses_discarded_result` | A statement discarding a `Result` fails to compile against the strict target, with a `nodiscard` diagnostic. |
| FR-7 | `tests/result/result_test.cpp` — `ErrorCodeRefusesAStruct`, `AcceptsAnIntegralErrorType` | `ErrorCode` holds for an enumeration and an integer and not for a struct or a floating-point type, asserted on the concept itself, which evaluates without failing the build, and naming a `Result` with a struct `E` is refused; an integral `E` works in a `Result`. |
| FR-8 | `tests/result/result_test.cpp` — `IsTriviallyCopyableAndCheap` | With a trivially copyable `T` the type is trivially copyable and costs the value plus a flat discriminator. |
| FR-9 | `tests/result/result_test.cpp` — `ReadingTheValueOfAFailureIsRefused` | `value()` on a failure aborts naming the precondition. **Debug only**, as BR-10: `NDEBUG` removes the assertion, and the case skips on the Release leg saying so. |
| FR-10 | `tests/result/callback_test.cpp` — `BindReachesTheObject`, `AConstMemberFunctionDoesNotBind`, `IsTriviallyCopyableAndPointerSized` | A bound callback reaches the object's member function; a const member function does not bind, asserted as a requires-expression; the callback remains two pointers. The non-ownership half is review-only: the library holds raw pointers and cannot check either lifetime. |
| FR-11 | `tests/result/result_test.cpp` — `AFailureFromTheSuccessCodeIsRefused` | A `Result<int, E>` constructed from `E{}` aborts naming the fabricated success; a `Result<void, E>` constructed from `E{}` is `ok()`. **Debug only** for the refusal, as FR-9. |
| BR-1 | `tests/containers/fixed_string_test.cpp` — `HoldsCapacityMinusOne`, `TruncationStaysTerminated`, `ViewExcludesTheTerminator`, `TruncatedNamesTheLossItAccepts`, `TextConvertsOnlyThroughTruncated` | A string of exactly `N - 1` characters round-trips and is not reported as truncated; a longer one reports truncation, holds `N - 1` characters and is still NUL-terminated; no text type constructs or converts to a `FixedString`, so the only path from text is `truncated()`, whose name states the loss; `assign()` still reports what it swallowed. |
| BR-2 | `tests/containers/fixed_string_test.cpp` — `AppendIsAllOrNothing` | An append that would not fit returns false and leaves `view()` byte-identical, both for one character over and for text longer than the whole capacity. |
| BR-3 | `tests/containers/flat_map_test.cpp` — `InsertsUpToCapacity`, `DuplicateKeyReplaces`, `NewKeyAtCapacityIsRefused`, `AVisitorCannotRewriteAKey` | A duplicate key leaves `size()` unchanged and replaces the value; a new key at capacity returns nullptr with every existing entry undisturbed; the walk passes the key by const reference and the value by mutable one, asserted inside the visitor, since a mutable-key visitor fails in its body rather than by substitution. |
| BR-4 | `tests/containers/flat_map_test.cpp` — `EraseLeavesTheRestFindable`, `EraseOfAnAbsentKeyReportsIt`, `AnEntryNeverMoves` | After erasing a middle key every other key still resolves, asserted on the set of remaining keys rather than their order; erasing an absent key returns false; a pointer to the last-inserted entry, the one a swap-with-last erase would move, still names it after an unrelated erase and insert. |
| BR-5 | `tests/containers/ring_buffer_test.cpp` — `PreservesFifoAcrossAWrap`, `PushAtCapacityIsRefused`, `SpscRingCarriesEveryValueInOrder`, `BothCounterPoliciesInstantiate`; `AnOverCommittedRingStillRefuses`; the `pico_util_refuses_ring_buffer_without_counter` build-failure test | A ring naming no `Counter` fails to compile. `2N + 1` elements return in input order through a ring of `N`; a push at capacity returns false and `front()` is unchanged; a producer and consumer thread transfer 20000 values with no gap, reorder or duplicate; a ring whose count a stray `commit()` drove past `N` still refuses and still holds its oldest element. |
| BR-6 | `tests/containers/fixed_string_test.cpp` — `StorageIsInline`; `tests/containers/flat_map_test.cpp` — `StorageIsInline`; `tests/containers/ring_buffer_test.cpp` — `BothCounterPoliciesInstantiate`; `tests/testing/allocation_test.cpp` — `NothingInThisLibraryAllocates` | A `FixedString<64>` is at least 64 bytes, and a `FlatMap` and a `RingBuffer` holding a 64-byte element are at least their capacity times it, which a layout holding a pointer to a heap block cannot be; and a representative operation of every shipped utility, `HeapWatermark`'s arming and reading included, counts exactly zero allocations under one armed guard. The subset is representative rather than exhaustive: it does not reach `ring_run`, `FixedString::c_str`, `FixedString::view`, `RingBuffer::front`, `Callback::bind`, `Result::value()`, `Result<void, E>` or the `*_at` byte accessors, and for those the structural guarantee is the `std::array` member each row above establishes. It does reach `try_emplace` and `ring_index`, through `insert_or_assign` and through `push`/`pop`. |
| BR-7 | ctest `pico_util_refuses_discarded_fixed_string_assign`, `…_append`, `…_flat_map_insert_or_assign`, `…_flat_map_try_emplace`, `…_ring_buffer_push`, `…_ring_buffer_acquire` | A statement discarding each of the six fails to compile against the strict target, with a `nodiscard` diagnostic. |
| BR-8 | `tests/containers/ring_index_test.cpp` — `APositionAndACountDoNotConvert` | Neither type converts to the other, nor either to or from `std::size_t`, and `ring_index<8>({a}, {b})` with two `std::size_t` does not compile, so a transposed call is refused whether or not it names the types. |
| BR-9 | `tests/containers/ring_index_test.cpp` — `ReportsTheContiguousRun` | The run from a start position is the smaller of the length wanted and the distance to the end of storage. |
| BR-10 | `tests/containers/ring_index_test.cpp` — `AnArgumentPastItsBoundIsRefused`, `ARunFromAStartPastTheEndIsRefused` | An offset larger than the capacity, and a start at or past the capacity, are refused rather than producing an index. **Debug only**: both cases `GTEST_SKIP` under `NDEBUG`, which the Release leg of the host matrix and the firmware build both define, so this row is established on the Debug leg alone. |
| BR-11 | `tests/containers/ring_index_test.cpp` — `AdvancesAndWraps`, `AdvancingByTheWholeCapacityReturnsTheSamePosition` | Advancing wraps at the capacity, and advancing by the whole capacity returns the same position. |
| BR-12 | `tests/containers/ring_buffer_test.cpp` — `AcquireAndCommitFillInPlace`, `PushAtCapacityIsRefused` | A slot acquired and filled in place is the element `front()` then returns; `acquire()` at capacity returns `nullptr`. |
| BR-13 | `tests/containers/fixed_string_test.cpp` — `ViewExcludesTheTerminator`, `ViewAliasesTheStorage` | The view spans the characters and not the terminator; its data pointer is `c_str()`, and a view taken before an `assign` reads the new contents, which a copying view would not: the case reads through the stale view on purpose, to show the aliasing, as a consumer may not. That a consumer must not read through it afterwards is review-only. |
| BR-14 | `tests/containers/flat_map_test.cpp` — `EraseReleasesWhatTheValueOwned`, `ClearReleasesWhatTheValuesOwned`, `AReleasedSlotIsReused` | A `std::shared_ptr` value's use count returns to one when its entry is erased, and every live value's does when the map is cleared, with nothing inserted afterwards to overwrite a missing reset; an erased slot is available again. |
| BR-15 | `tests/containers/flat_map_test.cpp` — `AVisitorMayEraseTheEntryItIsVisiting` | A visitor erasing every other entry as it visits still visits each live entry exactly once, and the map holds exactly the entries not erased. The insert-during-walk prohibition is review-only: it is undefined behaviour, which a test cannot assert. |
| BR-16 | `src/containers/include/pico_util/containers/flat_map.hpp` — `reset_value`; ctest `pico_util_refuses_flat_map_throwing_value`, `pico_util_flat_map_stack`; `tests/containers/flat_map_test.cpp` — `AValueNestedInItsHolderIsAccepted`; `tests/containers/fixed_string_test.cpp` — `TheMutatorsDoNotThrow` | A `V` whose default constructor is not `noexcept` fails to compile once the map is used; compiled `-O2 -fstack-usage` on the host, `try_emplace`, `erase` and `clear` over a 4 KiB aggregate value each use a frame under 1 KiB; a nested value type declared in the class holding the map is accepted and value-initialised; `FixedString`'s mutators are `noexcept` so a value holding one is not refused. |
| CR-1 | `tests/publish/published_test.cpp` — `BothStrategiesInstantiate`; ctest `pico_util_refuses_published_without_strategy` | Both named strategies instantiate, and a declaration naming no strategy fails to compile for want of a template argument. |
| CR-2 | `tests/publish/published_test.cpp` — `SeqlockReaderNeverObservesATornValue`; `tests/publish/published_wrap_test.cpp` — `ReturnsThePublishedValueAfterTheCounterWraps` | Under a concurrent writer publishing values whose halves are complements, every value a reader observes has intact halves, and the final read is the last value stored; after 2^31 stores, when the sequence counter is zero again, a read returns the value stored. |
| CR-3 | `tests/publish/published_test.cpp` — `SlotRingReaderNeverSpinsAndNeverValidatesATornValue`, `SlotReadValidatesOnlyBelowALap`; `tests/publish/published_wrap_test.cpp` — `ReturnsThePublishedValueAfterTheCounterWraps` | Under an unthrottled writer — deliberately outside the CR-4 contract — every `load()` returns, and no 64-word value it reports as a success has mixed words; exhausted reads are counted and not asserted on. Whether a lap lands inside a read in that case depends on timing, so the validation predicate is pinned separately at its boundaries, the counter's wrap included, where a predicate off by one fails the build; after 2^32 stores, when the store counter is zero again, a read succeeds with the value stored. That the fence makes a validated read sound on a weakly ordered core is review-only: the host is x86. |
| CR-4 | `tests/publish/published_test.cpp` — `SlotReadValidatesOnlyBelowALap`, `SlotRingReaderNeverTearsWithinTheWriterContract` | A read across zero, one or two completed stores — up to `SLOT_COUNT - 2` — validates, so it succeeds on its first attempt. Under a concurrent writer storing at a rate rare next to a read, no validated value is torn. A read descheduled for longer than three stores can still exhaust on a loaded host, so the concurrent case skips an exhausted read rather than asserting success; the success clause is held by the predicate. |
| CR-5 | `tests/publish/published_test.cpp` — `PublicationUsesThirtyTwoBitAtomicsOnly`, `CarriesAStructWiderThanAWord`, `CarriesATypeThatIsNotAWholeNumberOfWords`; `tests/firmware/header_probe.cpp`, built for both `pico_w` and `pico2_w` | A 32-bit atomic is always lock-free on the host, and each strategy aligns to a 32-bit word, so no wider atomic is present — asserted on alignment rather than on size, because a `std::atomic<std::uint64_t>` substituted for the word array leaves `sizeof` unchanged and raises `alignof` from 4 to 8. A payload that is not a whole number of words round-trips through both strategies. The probe is what establishes the ARMv6-M half, where the host assertion does not apply. |
| CR-6 | `tests/publish/published_test.cpp` — `ReportsNothingPublishedBeforeTheFirstStore`, `RoundTripsAValueThroughBothStrategies` | `published()` is false before the first store, true after it, and still true after a later store. Monotonicity beyond that is review-only: neither strategy has a path that clears the flag. |
| CR-7 | ctest `pico_util_refuses_published_non_trivial_payload` | Declaring a `Published` of a type that is not trivially copyable fails to compile with the class-scope `static_assert`'s message. |
| CR-8 | `tests/publish/published_test.cpp` — `ReportsNothingPublishedBeforeTheFirstStore`, `ReturnsTheValueInitialisedTBeforeTheFirstStore`; `tests/publish/published_wrap_test.cpp` — `ReturnsThePublishedValueAfterTheCounterWraps` | A load before any store returns `T{}` from both strategies, for a `T` with a default member initialiser, where `T{}` and the all-bits-zero value the storage starts as differ; a load after the counter wraps to zero does not. |
| CR-9 | `tests/containers/ring_buffer_test.cpp` — `SpscRingCarriesEveryValueInOrder`; `tests/firmware/header_probe.cpp`, which instantiates `SpscCounter` and links freestanding for both `pico_w` and `pico2_w` | Every value pushed by a producer thread is observed in order by a consumer thread; the counter links on both boards with no atomic runtime, so a read-modify-write reintroduced on ARMv6-M fails the `pico_w` link. The ordering itself is review-only: a host test cannot distinguish acquire/release from relaxed on x86. |
| CR-10 | `tests/publish/published_test.cpp` — `SlotReadValidatesOnlyBelowALap` | `SLOT_COUNT` is 4 and `MAX_READ_ATTEMPTS` is 3, asserted against the header; with `SLOT_COUNT` slots a read across zero, one or two completed stores validates and one across three or more does not, including across the counter's wrap. |
