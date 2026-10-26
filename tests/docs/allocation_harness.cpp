// SPDX-License-Identifier: MIT
// The allocation harness as a consumer's test would use it, with a driver
// declared to stand in for the code under test.
#include <gtest/gtest.h>

struct MyDriver {
    void init();
    void service();
};

#include "allocation_harness.inc"
