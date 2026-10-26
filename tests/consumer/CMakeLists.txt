cmake_minimum_required(VERSION 3.26)

# A firmware configure names an SDK. The SDK is imported before project() so
# its toolchain file applies, and the consumer is then a firmware program with
# its own source: the host smoke includes the throwing allocation operators,
# which need exceptions that a freestanding image does not have.
if(PICO_SDK_PATH)
    include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
    project(pico_util_consumer_smoke LANGUAGES C CXX ASM)
    pico_sdk_init()
    # The SDK toolchain confines find_package to its sysroot; the installed
    # prefix is outside it.
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
    set(PICO_UTIL_CONSUMER_SOURCE firmware_smoke.cxx)
else()
    project(pico_util_consumer_smoke LANGUAGES CXX)
    set(PICO_UTIL_CONSUMER_SOURCE consumer_smoke.cxx)
endif()

# Stands in for a downstream project consuming an installed pico-util. Its
# purpose is to prove the package is reachable by find_package and that the
# headers it exports are the ones a consumer needs -- the contract that fails
# only outside the source tree, which is why an in-tree test cannot cover it.
#
# This translation unit compiles only against an installed package, so it has
# no entry in the merged compile database and the repo-wide clang-tidy sweep
# must skip it. The root .lintignore is what does that; the .cxx extension does
# not, since the lint gate enumerates .cxx alongside .cpp and .cc.
set(PICO_UTIL_CONSUMER_VERSION "" CACHE STRING "Requested package version")
find_package(pico_util ${PICO_UTIL_CONSUMER_VERSION} REQUIRED)

add_executable(consumer_smoke ${PICO_UTIL_CONSUMER_SOURCE})

# Every exported target is named, not only the aggregate. `pico_util::core`
# pulls most of them in transitively, so a module dropped from the export set
# would still resolve here and the smoke would report nothing -- which is the
# one contract this executable exists to prove.
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

# Warnings-as-errors, which is what the root .lintignore's rationale claims of
# every path it names: this translation unit is excluded from the repo-wide
# clang-tidy sweep because it has no entry in the merged compile database, not
# because it is exempt from the gates. Without this it was the one tracked
# source under neither lint nor -Werror, and it is the worked example a
# downstream project copies.
#
# Spelled out rather than house_cxx_add_strict_target(): this is a standalone
# project configured against nothing but an installed pico_util prefix, so
# House's helpers are not on its module path and requiring them here would
# make the consumer test depend on something a real consumer would not have.
#
# On firmware the SDK compiles its own sources into this target, so the
# options apply to the consumer's source alone.
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
set_source_files_properties(${PICO_UTIL_CONSUMER_SOURCE} PROPERTIES
    COMPILE_OPTIONS "${PICO_UTIL_CONSUMER_WARNINGS}")
