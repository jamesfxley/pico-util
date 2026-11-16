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

`dev.py check` is the entry point in every repository of the family. Its report
shows whether the gates have run (`EN-01.8`).

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
applies. The footprint budgets are declared only in `pico.toml`.

The `firmware` tree is built only where `PICO_SDK_PATH` names an SDK, as the
family image does. `HOUSE_MATRIX_BOARD`, else `PICO_BOARD`, selects `pico2_w`
(the default) or `pico_w`. The `sanitize` tree builds the host tree as a Debug
build with the sanitiser `HOUSE_MATRIX_SANITIZE` names (`PICO_UTIL_SANITIZE`):
`address` for AddressSanitizer and UBSan, `thread` for ThreadSanitizer and
UBSan. It is built only when that variable is set, as the CI sanitiser legs
do for each value:

```sh
HOUSE_MATRIX_SANITIZE=address python3 dev.py check --group build
HOUSE_MATRIX_SANITIZE=thread python3 dev.py check --group build
```

The cross build uses the family's single `build-firmware/` tree. The SDK is
initialised there, but neither probe links an SDK library. The probe's one stub
stands in for the C library's syscalls, so the probe shows that the headers
stand alone:

```sh
python3 dev.py build --tree firmware
```

## Layout

- `src/bytes/`: `pico_util::bytes`, with big- and little-endian integer access
  over byte spans, and `as_bytes`. The module is header-only, so its target is
  INTERFACE and carries only its headers.
- `src/heap/`: `pico_util::heap`, with `HeapWatermark`. Its target has the same
  form.
- `src/containers/`: `pico_util::containers`, with `FixedString`, `FlatMap`,
  `RingBuffer` and the wrap arithmetic behind it. Its target has the same form.
- `src/publish/`: `pico_util::publish`, with `Published<T, Strategy>`. Its
  target has the same form.
- `src/testing/`: `pico_util::testing`, the allocation-counting harness. It is
  in the export set. `pico_util::core` does not link it and the umbrella header
  does not reach it.
- `src/result/`: `pico_util::result`, with `Result<T, E>` and `Callback`. Its
  target has the same form.
- `src/include/pico_util/pico_util.hpp`: the umbrella header. It belongs to no
  module (CXX-13.1) and is installed by `pico_util::core`, which links every
  module so that a consumer needs one target.
- `tests/<module>/`: the host suite, which mirrors `src/` (CXX-13.2). A test's
  path shows what it covers, and `src/` holds exactly the shippable surface.
- `tests/firmware/header_probe.cpp`: the cross-compiled probe. Nothing runs it.
  It exists so that the linker has to resolve what the headers reach (AR-2).
- `tests/firmware/testing_probe.cpp`: the same probe for the allocation harness,
  which the umbrella header, and so the header probe, never reaches.
- `tests/consumer/`: a separate CMake project built against the installed
  package (AR-3).
- `docs/`: the normative specification, the deviation register and the other
  documents listed in [README.md](README.md).

## Conventions

### House policy

The pinned guides hold the rules shared by every repository that takes its gates
from pico-tooling. Start with the
[engineering standard](../infra/house/guides/engineering-standard/SKILL.md).

### Repo specifics

Each item states a rule for a maintainer, with its reason or a link to the
reason.

- A utility earns its place here by the admission test in
  [architecture.md](architecture.md#what-belongs-in-this-library). Anything else
  stays in the repository that owns it (AR-1).
- Do not add a library-wide error enum. `Result` takes the caller's error type,
  and its zero enumerator must be the success state (FR-1). See
  [architecture.md](architecture.md#result-and-the-error-type).
- Do not give `RingBuffer`'s `Counter` a default (BR-5). The note after the BR
  table in [technical-specification.md](technical-specification.md) gives the
  reason.
- Do not give `Published` a default strategy (CR-1), and do not add retries to
  the slot ring to avoid `EXHAUSTED`. The bound is the writer contract (CR-4).
  See [architecture.md](architecture.md#publication-strategies).
- Do not bend `RingBuffer` or `FlatMap` to fit a byte stream or a
  generation-tagged slot table. See
  [architecture.md](architecture.md#ring-buffer-and-map).
- Do not reword the documentation of `HeapWatermark` into a claim of
  per-allocation detection. The limit of the proxy is part of MR-1.
- `allocation_operators.ipp` goes in exactly one translation unit per
  executable (MR-5). Do not add it to the umbrella header or to either cross
  probe, and do not link `pico_util::testing` from `pico_util::core`.
- `allocation_operators.ipp` is `.ipp` and not `.hpp` because it holds a translation unit's worth
  of definitions. `misc-definitions-in-headers` would fire on each of its twenty
  definitions, and `HeaderFilterRegex: '.*'` still lets clang-tidy check the file
  through the translation unit that includes it. Renaming it to `.hpp` costs
  twenty suppressions.
- `counted_free` is `[[gnu::noinline]]` and must stay so. Inlined into a
  replaced `operator delete`, an optimised GCC build pairs its `free` with the
  caller's `operator new` and fails with `-Werror=mismatched-new-delete`, so no
  Release build of a harness test compiles.
- The replacement is complete so that no sanitiser build needs the harness
  compiled out (MR-7). Do not add an ASan guard.
- The version check lives on the host test target alone, because
  `PICO_UTIL_CMAKE_VERSION_*` on `pico_util::core` would propagate build-time
  values into every installed consumer (OR-1).
- `<cstdlib>` must come before `<malloc.h>`, in an include block above the one
  that holds `<malloc.h>`. See [architecture.md](architecture.md#include-order).
- `measuring()` is decided by the build and never by a reading (MR-8).
  `pico_util_heap_fresh_measuring` catches an implementation that uses a reading.
  It is linked `--as-needed` so that the C++ runtime's exception pool is not on
  its heap before `main()`.
- `tests/consumer/` is in `.lintignore`. It is a separate project built against
  the installed package (AR-3), so this tree's compile database does not cover
  it, and linting it would run clang-tidy on invented flags (CXX-00.6). The
  listing is not a suppression, because that project compiles it with warnings
  as errors. The cross probes under `tests/firmware/` are linted through the
  firmware tree's compile database (AR-2), which is why
  `python3 dev.py check --group lint` builds that tree as well as the host one.
