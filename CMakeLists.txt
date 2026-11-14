cmake_minimum_required(VERSION 3.26)

include(cmake/pico_family_bootstrap.cmake)
pico_family_bootstrap(PICO_UTIL)

project(pico_util VERSION 0.1.0 LANGUAGES C CXX ASM)

# The SDK is initialised to share the family's firmware tree. Shipped targets
# link no SDK library. Firmware examples and device tests use it separately.
house_cxx_common_settings(${PICO_UTIL_BUILD_FIRMWARE})
pico_family_init_sdk(${PICO_UTIL_BUILD_FIRMWARE})

# Device images compile SDK sources, so -Werror applies only to owned sources.
# FREESTANDING follows the helper's CMAKE_CROSSCOMPILING default.
house_cxx_add_strict_target(pico_util_strict
    VENDOR_SOURCES ${PICO_UTIL_BUILD_FIRMWARE})

# Sanitisers apply before all targets, including GoogleTest: ASan's
# container-overflow detector needs both sides of a std::string boundary.
house_cxx_enable_sanitizers(PICO_UTIL)

# Options are declared before subdirectories read them. An undeclared option
# would read as an empty string.
house_cxx_declare_tests_option(PICO_UTIL)

# The probe forces the linker to resolve the umbrella on a freestanding
# target (AR-2). Nothing runs it.
# It is independent of GoogleTest and PICO_UTIL_ENABLE_TESTS, which is off in
# cross builds.
pico_family_declare_probe_option(PICO_UTIL)

# Installation defaults on only when this is the top-level project. A parent
# owns subproject exports and can enable this option before adding the project.
house_cxx_declare_install_option(PICO_UTIL)

add_subdirectory(src)

if(PICO_UTIL_ENABLE_TESTS)
    house_cxx_enable_gtest()
    add_subdirectory(tests)
endif()

if(PICO_UTIL_ENABLE_FIRMWARE_PROBE)
    add_subdirectory(tests/firmware)
endif()

if(PROJECT_IS_TOP_LEVEL AND PICO_UTIL_BUILD_FIRMWARE)
    add_subdirectory(tests/device)
endif()

if(PICO_UTIL_ENABLE_INSTALL)
    # install(EXPORT) needs every target named in an exported link interface,
    # including pico_util_strict. Consumers never link it directly, and its
    # warning flags are not part of the shipped interface.
    house_cxx_install_package(
        PACKAGE pico_util VERSION ${PROJECT_VERSION}
        NAMESPACE pico_util
        TARGETS
            pico_util_strict
            pico_util_bytes
            pico_util_containers
            pico_util_core
            pico_util_heap
            pico_util_publish
            pico_util_result
            pico_util_testing
    )
endif()
