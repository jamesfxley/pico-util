// SPDX-License-Identifier: MIT
// The umbrella compiles under analysis. Its version is checked against CMake.
#include "pico_util/pico_util.hpp"

#include <gtest/gtest.h>

// misc-include-cleaner needs the direct include. The installed-consumer smoke
// test checks that HeapWatermark is reachable through the umbrella alone
// (AR-3).
#include "pico_util/heap/heap_watermark.hpp"

namespace {

// CMake supplies the expected version. Copying header literals would hide drift
// (OR-1).
TEST(PicoUtil, VersionMatchesTheBuildSystem) {
    EXPECT_EQ(pico_util::VERSION_MAJOR, PICO_UTIL_CMAKE_VERSION_MAJOR);
    EXPECT_EQ(pico_util::VERSION_MINOR, PICO_UTIL_CMAKE_VERSION_MINOR);
    EXPECT_EQ(pico_util::VERSION_PATCH, PICO_UTIL_CMAKE_VERSION_PATCH);
}

// A utility is instantiated here. Direct includes prevent testing umbrella
// reach. The installed consumer checks that reach separately (AR-3).
TEST(PicoUtil, UmbrellaHeaderCompilesAndItsUtilitiesAreUsable) {
    pico_util::HeapWatermark watermark;
    EXPECT_FALSE(watermark.armed());
    watermark.arm();
    EXPECT_TRUE(watermark.armed());
}

}  // namespace
