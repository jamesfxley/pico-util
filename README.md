# pico-util

[![CI](https://github.com/jamesfxley/pico-util/actions/workflows/ci.yml/badge.svg)](https://github.com/jamesfxley/pico-util/actions/workflows/ci.yml)

Header-only C++20 utilities shared by the pico-* family for the Raspberry Pi
Pico (RP2040/RP2350), free of any Pico SDK, RTOS or network-stack dependency.
The same headers compile for a host test build and for a cross-compiled
firmware image.

The library is the base layer of the family. A utility belongs here when two or
more repositories have each written their own version of it.

| Module | Target | What it holds |
|---|---|---|
| `bytes` | `pico_util::bytes` | Big- and little-endian integer access over byte spans, and `as_bytes` |
| `heap` | `pico_util::heap` | `HeapWatermark`, which measures heap growth after initialisation |
| `containers` | `pico_util::containers` | `FixedString`, `FlatMap`, `RingBuffer` and its wrap arithmetic. The containers have fixed capacity, and an insert into a full one fails instead of evicting an element |
| `publish` | `pico_util::publish` | `Published<T, Strategy>`, which carries a value between an interrupt handler and mainline in either direction |
| `testing` | `pico_util::testing` | An allocation-counting harness for showing that a path allocates nothing. The umbrella header does not include it |
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

`HeapWatermark` is a proxy. It reports steady-state heap growth and its size.
[The usage guide](docs/usage-guide.md#what-the-number-means-and-what-it-does-not)
says what a reading of 0 can and cannot mean.

## Layout

[docs/architecture.md](docs/architecture.md) lists every header, with one
module to a directory, and states what belongs in this library.

## Building & tests

The pico family development image has every tool that the gates need, including
the Pico SDK. `.devcontainer/devcontainer.json` builds it from `infra/`, so open
the repository in its dev container to use it. The `infra/` git submodule
([pico-tooling](https://github.com/jamesfxley/pico-tooling)) carries the gates,
the CMake helpers and the pinned guides. Clone with
`git clone --recurse-submodules`, or run `git submodule update --init --recursive`
after a plain clone.

```sh
python3 dev.py check          # every gate; a change is done when this is green
python3 dev.py build          # clean build and test of every enabled tree
python3 dev.py lint           # clang-tidy; blocking
```

Host tests exercise the portable contracts. `tests/firmware/` compiles and
links the headers for the target. `tests/device/` runs publication and SPSC
handoffs between mainline and a timer interrupt. Device-test output follows the
[pinned device convention](infra/guides/device.md#device-tests).

Cross-compiling the headers and device-test image, in the image that sets
`PICO_SDK_PATH`:

```sh
python3 dev.py build --tree firmware
```

## Documentation

- [Documentation index](docs/README.md): what each document is for.
- [Orientation](docs/orientation.md): how the repository is laid out, and the
  traps it contains.
- [Contributing](CONTRIBUTING.md): how a change is made and when it is done.
- [Technical specification](docs/technical-specification.md): the normative
  document, with the requirements, their pass criteria and the traceability
  matrix.
- [Architecture](docs/architecture.md): the header-only structure, the two
  byte-order families, the mallinfo proxy, the two libc spellings, what belongs
  in the library and the limits that the caller owns.
- [Usage guide](docs/usage-guide.md): linking the library and using each module,
  covering byte order, publication strategy, `Result` and its error convention,
  the fixed-capacity containers, the heap watermark and the allocation harness.
- [Standing deviations](docs/deviations.md): scope-wide rule suppressions and
  their review dates.
- [Releases](https://github.com/jamesfxley/pico-util/releases): what changed in
  each release and what to change in your code when an API broke.

## Releases

Versioning follows
[EN-06.1](infra/house/standard/engineering/06-release-and-versioning.md). The
notes on each [release](https://github.com/jamesfxley/pico-util/releases) record
what changed and, for a break, what to change in your own code (EN-06.3).

The test suite checks the advertised version in `pico_util.hpp` against
`project(VERSION ...)`, so the two cannot drift (EN-06.2).

## AI-assisted development

This project is developed with AI assistance. Design decisions, review and
acceptance are human; an AI agent writes and refactors code under direction and
against a written engineering standard. Every change passes the repository's own
gates (build, tests, formatter, linter and deviation counts) before it lands.

This section is a disclosure and gives no warranty. The terms in
[LICENSE](LICENSE) apply unchanged, including its disclaimer of warranties.

## Development configuration

`house.toml` refers to `infra/family.toml`. The pinned family supplies the
gates, the build trees, the CI preparation and the list of synced files. The
product dependency pins are in `CMakeLists.txt`. `AGENTS.md` points to
`infra/agents/AGENTS.md`.

House writes the root tool files, the CI workflow and
`.devcontainer/devcontainer.json`. `python3 -m infra.pico_tooling sync` writes
the CMake bootstrap. This repository owns `tests/.clang-tidy`, which layers on
the root `.clang-tidy`. The family-freshness gate warns when the pinned
infrastructure is behind the latest release and still passes. If it cannot read
the release tags, its result is unavailable evidence and not a pass.
