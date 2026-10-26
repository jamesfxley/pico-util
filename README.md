# pico-util

[![CI](https://github.com/jamesfxley/pico-util/actions/workflows/ci.yml/badge.svg)](https://github.com/jamesfxley/pico-util/actions/workflows/ci.yml)

Header-only C++20 utilities shared by the pico-* family for the Raspberry Pi
Pico (RP2040/RP2350), free of any Pico SDK, RTOS or network-stack dependency.
The same headers compile for a host test build and for a cross-compiled
firmware image.

The library is the family's base layer: a utility lands here when two or more
repositories have each written their own version of it.

| Module | Target | What it holds |
|---|---|---|
| `bytes` | `pico_util::bytes` | Big- and little-endian integer access over byte spans, and `as_bytes` |
| `heap` | `pico_util::heap` | `HeapWatermark`, the post-init heap-growth proxy |
| `containers` | `pico_util::containers` | `FixedString`, `FlatMap`, `RingBuffer` and its wrap arithmetic — fixed-capacity, refusing rather than evicting |
| `publish` | `pico_util::publish` | `Published<T, Strategy>` — carrying a value between an interrupt handler and mainline, either direction |
| `testing` | `pico_util::testing` | An allocation-counting harness, for proving a path allocates nothing. Not reached by the umbrella |
| `result` | `pico_util::result` | `Result<T, E>` for fallible returns, and `Callback` for non-owning notification |

## Quick start

Via FetchContent:

```cmake
include(FetchContent)
FetchContent_Declare(pico_util
    GIT_REPOSITORY https://github.com/jamesfxley/pico-util.git
    GIT_TAG v0.1.0)
FetchContent_MakeAvailable(pico_util)

target_link_libraries(my_target PRIVATE pico_util::core)
```

Or against an installed package:

```cmake
find_package(pico_util REQUIRED)
target_link_libraries(my_target PRIVATE pico_util::core)
```

Then:

<!-- example: readme_heap_watermark -->
```cpp
#include "pico_util/pico_util.hpp"

// Armed once initialisation is complete, read whenever you report status.
void on_init_complete(pico_util::HeapWatermark& watermark) { watermark.arm(); }

// A growth of 0 is a clean run only when the watermark is armed and the heap
// can be read here, so report all three.
struct HeapStatus {
    bool armed;
    bool measuring;
    std::size_t growth_bytes;
};

HeapStatus heap_status(const pico_util::HeapWatermark& watermark) {
    return {.armed = watermark.armed(),
            .measuring = pico_util::HeapWatermark::measuring(),
            .growth_bytes = watermark.growth_bytes()};
}
```

`HeapWatermark` is a proxy that catches, and reports the size of, steady-state
growth; [the usage guide](docs/usage-guide.md#what-the-number-means-and-what-it-does-not)
says what a reading of 0 can and cannot mean.

## Layout

Every header, one module to a directory, is listed in
[docs/architecture.md](docs/architecture.md), which also states what belongs in
this library at all.

## Building & tests

Every tool the gates need, the Pico SDK included, is in the pico family
development image that `.devcontainer/devcontainer.json` builds from `infra/`;
open the repository in its dev container to use that image. The `infra/` git
submodule ([pico-tooling](https://github.com/jamesfxley/pico-tooling)) carries
the gates, the CMake helpers and the pinned guides; clone with
`git clone --recurse-submodules` or run
`git submodule update --init --recursive` after a plain clone.

```sh
python3 dev.py check          # every gate; a change is done when this is green
python3 dev.py build          # clean build and test of every enabled tree
python3 dev.py lint           # clang-tidy; blocking
```

Cross-compiling the headers for the target, in the image that sets
`PICO_SDK_PATH`:

```sh
python3 dev.py build --tree firmware
```

## Documentation

- [Documentation index](docs/README.md) — what each document is for
- [Orientation](docs/orientation.md) — how the repository is laid out, and the
  traps it has already paid for
- [Contributing](CONTRIBUTING.md) — how a change is made and when it is done
- [Technical specification](docs/technical-specification.md) — normative;
  requirement IDs `AR/BR/CR/DR/FR/MR/OR`, their pass criteria and the traceability matrix
- [Architecture](docs/architecture.md) — the header-only shape, the two
  byte-order families, the mallinfo proxy, the two libc spellings, what belongs
  here at all, and the limits that are the caller's
- [Usage guide](docs/usage-guide.md) — linking the library, and using each
  module: byte order, publication strategy, `Result` and its error convention, the
  fixed-capacity containers, the heap watermark and the allocation harness
- [Standing deviations](docs/deviations.md) — scope-wide rule suppressions and
  their review dates
- [Releases](https://github.com/jamesfxley/pico-util/releases) — what changed per
  release, and what to change in your code when an API broke

## Releases

Versioning is semver: breaking changes at a major boundary, or a minor boundary
below 1.0. The notes on
each [release](https://github.com/jamesfxley/pico-util/releases) record what
changed and, for a break, what to change in your own code (`EN-06.3`).

The advertised version in `pico_util.hpp` is checked against
`project(VERSION ...)` by the test suite, so the two records cannot drift.

## AI-assisted development

This project is developed with AI assistance. Design decisions, review and
acceptance are human; an AI agent writes and refactors code under direction and
against a written engineering standard. Every change passes the repository's own
gates — build, tests, formatter, linter, deviation counts — before it lands.

This section is disclosure, not a warranty. The terms in [LICENSE](LICENSE)
apply unchanged, including its disclaimer of warranties.

## Development configuration

`house.toml` refers to `infra/family.toml`; the pinned family supplies gates,
trees, CI preparation and synced file selections. Product dependency pins live
in `CMakeLists.txt`. `AGENTS.md` routes to `infra/agents/AGENTS.md`.
House renders root tool files, the workflow and `.devcontainer/devcontainer.json`;
`python3 -m infra.pico_tooling sync` writes the bootstrap. Tests own their
registered `.clang-tidy` overlay. Infrastructure freshness reports adoption as a
warning and passes; unreadable release tags are unavailable evidence.
