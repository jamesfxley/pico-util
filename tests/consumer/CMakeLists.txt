cmake_minimum_required(VERSION 3.26)

# The SDK is imported before project() to select its toolchain. Firmware uses
# a separate source because the host allocation replacements need exceptions,
# which a freestanding image does not have.
if(PICO_SDK_PATH)
    include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
    project(pico_util_consumer_smoke LANGUAGES C CXX ASM)
    pico_sdk_init()
    # The SDK toolchain confines find_package to its sysroot. The installed
    # prefix is outside it.
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
    set(PICO_UTIL_CONSUMER_SOURCE firmware_smoke.cxx)
else()
    project(pico_util_consumer_smoke LANGUAGES CXX)
    set(PICO_UTIL_CONSUMER_SOURCE consumer_smoke.cxx)
endif()

# Only an out-of-tree consumer can detect missing installed headers.
# The merged compile database has no entry for this unit, so .lintignore
# must exclude it. The .cxx extension is still enumerated by the lint gate.
set(PICO_UTIL_CONSUMER_VERSION "" CACHE STRING "Requested package version")
find_package(pico_util ${PICO_UTIL_CONSUMER_VERSION} REQUIRED)

add_executable(consumer_smoke ${PICO_UTIL_CONSUMER_SOURCE})

# Naming every module detects exports that the umbrella could hide transitively.
target_link_libraries(consumer_smoke PRIVATE
    pico_util::core
    pico_util::containers
    pico_util::bytes
    pico_util::heap
    pico_util::publish
    pico_util::result
    pico_util::testing
)

if(PICO_SDK_PATH)
    target_link_libraries(consumer_smoke PRIVATE pico_stdlib)
    pico_add_extra_outputs(consumer_smoke)
endif()

target_compile_features(consumer_smoke PRIVATE cxx_std_20)

# Warnings fail here because .lintignore excludes these units from clang-tidy.
# House's CMake helpers, such as house_cxx_add_strict_target(), are unavailable
# in this standalone installed-package consumer, so strict options are stated
# directly.
set(PICO_UTIL_CONSUMER_WARNINGS
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Wold-style-cast
    -Wformat=2
    -Werror
)
# The SDK compiles its own sources into the firmware target, so these options
# apply to the consumer source alone.
set_source_files_properties(${PICO_UTIL_CONSUMER_SOURCE} PROPERTIES
    COMPILE_OPTIONS "${PICO_UTIL_CONSUMER_WARNINGS}")
