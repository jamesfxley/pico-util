// SPDX-License-Identifier: MIT
#pragma once

// The umbrella re-exports every module but testing.
// IWYU pragma: begin_exports
#include "pico_util/bytes/as_bytes.hpp"
#include "pico_util/bytes/byte_order.hpp"
#include "pico_util/containers/fixed_string.hpp"
#include "pico_util/containers/flat_map.hpp"
#include "pico_util/containers/ring_buffer.hpp"
#include "pico_util/containers/ring_index.hpp"
#include "pico_util/heap/heap_watermark.hpp"
#include "pico_util/publish/published.hpp"
#include "pico_util/result/callback.hpp"
#include "pico_util/result/result.hpp"

// IWYU pragma: end_exports

/// \file pico_util.hpp
/// The pico-util umbrella header: the shared, dependency-free utilities the
/// pico-* family builds on.
///
/// AR-1: header-only, depending on nothing beyond the C++ standard library and
/// the platform libc. The include set is the requirement — no Pico SDK, no
/// RTOS, no network stack — so the same headers serve host tests and firmware
/// images (AR-2).

namespace pico_util {

/// \name Library version
/// Mirrors `project(pico_util VERSION ...)`. A host test compares these against
/// the version the build system holds, so a header that drifts from the package
/// version fails the suite rather than misreporting itself to a consumer.
/// \{

/// Major version component.
inline constexpr int VERSION_MAJOR = 0;
/// Minor version component.
inline constexpr int VERSION_MINOR = 1;
/// Patch version component.
inline constexpr int VERSION_PATCH = 0;

/// \}

}  // namespace pico_util
