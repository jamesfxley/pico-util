// SPDX-License-Identifier: MIT
// Compiles the umbrella header as a translation unit so the full public surface
// is exercised by the analysis toolchain, and pins the advertised version to
// the values the build system carries.
#include "pico_util/pico_util.hpp"

#include <gtest/gtest.h>

// Named again for misc-include-cleaner, which requires the header providing a
// used symbol to be included directly. The umbrella above is what is under
// test; that it compiles as a translation unit of its own is the point, and
// AR-3's consumer smoke test proves a downstream project reaches HeapWatermark
// through the umbrella alone.
#include "pico_util/heap/heap_watermark.hpp"

namespace {

// OR-1: the advertised version and project(pico_util VERSION ...) are two
// records of one fact, compared here against the value CMake holds rather than
// against a literal copied out of the header -- which would only ever compare
// the header with itself.
TEST(PicoUtil, VersionMatchesTheBuildSystem) {
    EXPECT_EQ(pico_util::VERSION_MAJOR, PICO_UTIL_CMAKE_VERSION_MAJOR);
    EXPECT_EQ(pico_util::VERSION_MINOR, PICO_UTIL_CMAKE_VERSION_MINOR);
    EXPECT_EQ(pico_util::VERSION_PATCH, PICO_UTIL_CMAKE_VERSION_PATCH);
}

// One utility, reached and used, so the umbrella is a compiled translation unit
// rather than a header nothing instantiates.
//
// Not the "umbrella alone reaches every utility" claim, which this file cannot
// make: misc-include-cleaner requires the header providing a used symbol to be
// included directly, so every entity named here drags in its own include and
// the umbrella stops being what resolved it. AR-3's consumer smoke is where
// that claim lives -- it names all ten through pico_util.hpp and nothing else,
// against an *installed* package, which is the only place the reach is real.
TEST(PicoUtil, UmbrellaHeaderCompilesAndItsUtilitiesAreUsable) {
    pico_util::HeapWatermark watermark;
    EXPECT_FALSE(watermark.armed());
    watermark.arm();
    EXPECT_TRUE(watermark.armed());
}

}  // namespace
