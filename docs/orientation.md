# pico-util

Header-only C++20 utilities shared by the pico-* family, free of any Pico SDK,
RTOS or network-stack dependency. The same headers serve host tests and
cross-compiled firmware images. Part of the pico-* family (pico-logging,
pico-tooling as the `infra/` submodule).

Orientation for anyone working in this repository: how to build and gate it,
how it is laid out, and the repository's invariants and traps.

Engineering standards, cited by rule ID (`EN-`/`CXX-`):
<https://github.com/jamesfxley/engineering-standard>

## Commands

```sh
python3 dev.py check            # every gate that applies; the one to run
python3 dev.py check --list     # name what would run, and why anything would not
python3 dev.py check --group lint   # one group
```

`dev.py check` is the entry point in every repository of the family, and its
report is the answer to "have the gates run?" (`EN-01.8`).

Individual gates:

```sh
python3 dev.py build [--tree NAME]   # clean build + GoogleTest suite (build/, build-NAME/)
python3 dev.py format [--check]      # clang-format at the pinned major version
python3 dev.py lint                  # clang-tidy over the compile db; BLOCKING
python3 dev.py gate <name>           # one gate, e.g. deviations, footprint, consumer
```

Lint reads existing build trees. Run `python3 dev.py build` first; the CI
lint job does that in `prepare`.

`python3 dev.py check --list` prints every gate with its group and whether it
applies. The footprint budgets are declared in `pico.toml`, their one
authoritative home.

The `firmware` tree is built only where `PICO_SDK_PATH` names an SDK, as the
family image does. `HOUSE_MATRIX_BOARD`, else `PICO_BOARD`, selects `pico2_w`
(the default) or `pico_w`. The `sanitize` tree builds the host tree as a Debug
build with AddressSanitizer and UBSan (`PICO_UTIL_SANITIZE`) and runs its
tests with `ASAN_OPTIONS=detect_leaks=1`; it is built only when
`HOUSE_MATRIX_SANITIZE=ON`, which the CI sanitizer leg sets:

```sh
HOUSE_MATRIX_SANITIZE=ON python3 dev.py check --group build
```

The cross build is the family's one `build-firmware/` tree. The SDK is
initialised there, but neither probe links an SDK library: the probe's one stub
stands in for the C library's syscalls, so what it proves is that the headers
stand alone:

```sh
python3 dev.py build --tree firmware
```

## Layout

- `src/bytes/` — `pico_util::bytes`: big- and little-endian integer access over
  byte spans, and `as_bytes`. Header only, so the target is INTERFACE and
  carries nothing but its headers.
- `src/heap/` — `pico_util::heap`: `HeapWatermark`. Same shape.
- `src/containers/` — `pico_util::containers`: `FixedString`, `FlatMap`,
  `RingBuffer` and the wrap arithmetic behind it. Same shape.
- `src/publish/` — `pico_util::publish`: `Published<T, Strategy>`. Same shape.
- `src/testing/` — `pico_util::testing`: the allocation-counting harness. In
  the export set but **not** linked by `pico_util::core` and not reached by
  the umbrella.
- `src/result/` — `pico_util::result`: `Result<T, E>` and `Callback`. Same
  shape.
- `src/include/pico_util/pico_util.hpp` — the umbrella, belonging to no
  module (`CXX-13.1`) and installed by `pico_util::core`, which links every
  module so a consumer needs one target.
- `tests/<module>/` — the host suite, mirroring `src/` (`CXX-13.2`), so what a
  test covers is readable from its path and `src/` stays exactly the shippable
  surface.
- `tests/firmware/header_probe.cpp` — AR-2's cross-compiled probe. Nothing runs
  it; it exists so the linker has to resolve what the headers reach.
- `tests/firmware/testing_probe.cpp` — the same for the allocation harness,
  which the umbrella and so the header probe never reach.
- `tests/consumer/` — a separate CMake project built against the *installed*
  package (AR-3).
- `docs/` — normative spec (requirement IDs `AR/BR/CR/DR/FR/MR/OR`), deviation register.

## Conventions

### House policy

The rules shared by every repository that takes its gates from pico-tooling are
in the pinned guides, starting with the
[engineering standard](../infra/house/guides/engineering-standard/SKILL.md).

### Repo specifics

- **A utility earns its place here** by the admission test in
  [architecture.md](architecture.md): independent implementations in two or
  more repositories, and nothing beyond the standard library and the platform
  libc. Anything else stays in the repository that owns it.
- **The two byte-order families differ in where the bounds check lives, not in
  what they decode.** `read_be_at<Offset, T>` takes a static-extent span and
  makes an over-run a substitution failure (DR-2); `read_be<T>(bytes, offset)`
  takes a dynamic extent and checks nothing, because its callers have already
  validated the length (DR-3). The constraint refuses `std::dynamic_extent`
  outright — it is `SIZE_MAX`, so a bound checked against it passes for every
  offset and reads as protection that is not there.
- **`Result`'s error type is a parameter and stays one.** `ok()` compares
  against `E{}`, so a consumer's error enum must have zero as its success
  enumerator; that convention is FR-1, not folklore. Do not add a library-wide
  error enum here — merging two libraries' taxonomies is what the
  parameterisation exists to avoid. A `static_assert` refuses any `T` and `E`
  that convert to each other, `Result<E, E>` included; architecture.md says
  why.
- **`RingBuffer`'s `Counter` has no default, on purpose.** Both policies
  compile for any `T` and a single-context ring used across two contexts passes
  every single-threaded host test it is given; the defect shows up only on the
  target and only under preemption. Do not add a default to tidy the
  declaration site — the verbosity *is* the check (BR-5).
- **`RingBuffer` is an element ring, and `FlatMap` a caller-keyed map.** Do not
  bend either to fit a byte stream or a generation-tagged slot table;
  architecture.md says why pico-http's `TxRing` and `SlotTable` are neither.
- **`Published` has no default strategy, and must not get one.** The axis is
  which side can preempt which: a seqlock reader spins, which terminates only
  where the writer is the interrupt handler (pico-bno085's edge latch); a
  slot-ring reader never spins, which is what an interrupt-context reader needs
  (pico-time's wall clock). Both compile for any `T` and both pass a
  single-threaded host test, so the wrong choice surfaces as a hang in an ISR
  or a torn read on target and nowhere else (CR-1).
- **The slot ring's bound is its writer contract, not its retry count.** A
  writer storing in a tight loop laps a four-slot ring inside one read and no
  depth survives that. The suite has one case inside the contract and one
  outside it; the outside case asserts that the reader returns and that nothing
  it reports as validated is torn, and counts exhausted reads without asserting
  on them. Do not "fix" exhaustion by adding retries: it is the writer's
  contract failing, and the error is how the reader says so.
- **The counter re-read takes an acquire *fence*, not an acquire load.** An
  acquire load stops later accesses moving earlier; this needs the word loads
  not to move *after* the re-read, which is the other direction. Getting it
  wrong makes the validation pass over a torn read.
- **`allocation_operators.ipp` goes in exactly one TU per executable.** The
  replaceable allocation functions cannot be `inline`, so a second include is a
  duplicate-symbol link error — which is the loud failure wanted. Never add it
  to the umbrella or either cross probe, and never link `pico_util::testing` from
  `pico_util::core`.
- **`counted_free` is `[[gnu::noinline]]`, and must stay so.** Inlined into a
  replaced `operator delete`, an optimised GCC build pairs its `free` with the
  caller's `operator new` and fails `-Werror=mismatched-new-delete`, so no
  Release build of a harness test compiles.
- **`.ipp`, not `.hpp`, and that is not cosmetic.** The file is a translation
  unit's worth of definitions, so `misc-definitions-in-headers` would fire on
  every one of them — twenty suppressions asserting a premise the file's own
  contract already denies. The extension makes the rule inapplicable rather
  than silenced, and `HeaderFilterRegex: '.*'` means clang-tidy still checks it
  through the translation unit that includes it. Renaming it back costs twenty
  suppressions.
- **The allocation replacement is complete, and that is load-bearing.** All
  twenty forms reach the same malloc/aligned_alloc/free. A partial
  replacement lets a block from one allocator reach another's deallocation
  function, which is an `alloc-dealloc-mismatch` under a sanitizer — and the
  usual workaround is to compile the harness out under sanitizers, losing the
  coverage where it matters most. Do not add an ASan guard; completeness is
  what makes one unnecessary (MR-7).
- **`Callback::bind` takes a non-const member function.** A const one has a
  different pointer type and does not bind.
- **`as_bytes` exists to hold one `reinterpret_cast` at one site.** `unsigned
  char` may alias any object, which is the one reinterpretation the standard
  permits and the rule cannot distinguish from the ones it is right to refuse.
  A `static_assert` pins `std::uint8_t` to a character type, because that is
  what the aliasing argument rests on.
- **`INTERFACE`, so there is no translation unit of this library's own.** The
  version pin therefore lives on the host test target alone. Putting
  `PICO_UTIL_CMAKE_VERSION_*` on `pico_util::core` instead would propagate
  build-time values into every installed consumer.
- **`<cstdlib>` must be seen before `<malloc.h>`**, in an include block above
  the one holding `<malloc.h>`, for the reason
  [architecture](architecture.md#include-order-is-load-bearing) gives.
- **`PICO_UTIL_HAVE_MALLINFO*` are feature flags, not values.** They must be
  testable by `#if`, which a `constexpr` cannot be. That is what the two
  `cppcoreguidelines-macro-usage` suppressions are for.
- **`measuring()` is decided by the build, never by a reading**, for the reason
  [architecture](architecture.md#heapwatermark) gives.
  `pico_util_heap_fresh_measuring` catches a reading-based one, and is linked
  `--as-needed` so the C++ runtime's exception pool is not on its heap before
  `main()`.
- **MR-1 is a proxy and the spec says so.** Net growth cannot see an
  allocate/free pair that nets to zero within one window. Do not reword the
  documentation into a claim of per-allocation detection.
- **`tests/consumer/` is in `.lintignore`**, with its reason: it is a separate
  project built against the installed package, so this tree's compile database
  does not cover it and linting it would run clang-tidy on invented flags
  (`CXX-00.6`). It is not a suppression — it is compiled under
  warnings-as-errors by that project. The cross probes under `tests/firmware/`
  are linted through the firmware tree's compile database, which is why
  `python3 dev.py check --group lint` builds that tree as well as the host one.

- **The error taxonomy belongs to the consumer, and this library ships only the
  carrier.** `Result<T, E>` takes the caller's `E` precisely so no library has
  to adopt another's vocabulary. The family rule this serves: a library owns
  the taxonomy of the failures *it* can produce, and must not re-export a
  dependency's error type at its own public API. Adding a `PicoUtilError` here
  would break that rule from the bottom.
