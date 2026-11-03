pico_family_add_device_test(pico_util_device_tests
    PACKAGE pico_util
    SOURCES main.cpp
    LIBRARIES pico_util::publish pico_util::containers)
