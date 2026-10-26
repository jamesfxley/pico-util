cmake_minimum_required(VERSION 3.26)

include(cmake/pico_family_bootstrap.cmake)
pico_family_bootstrap(PICO_UTIL)

project(pico_util VERSION 0.1.0 LANGUAGES C CXX ASM)

# No firmware-conditional code and no SDK dependency: the same headers serve
# host and firmware builds. The SDK is initialised only so that the cross tree
# is the family's one build-firmware/ tree; nothing the library ships links an
# SDK library, and only the usage guide's firmware example, compiled under
# tests/firmware/, reads an SDK header.
house_cxx_common_settings(${PICO_UTIL_BUILD_FIRMWARE})
pico_family_init_sdk(${PICO_UTIL_BUILD_FIRMWARE})

# -Werror at target scope: nothing third-party compiles into these targets, so
# every diagnostic is actionable, and no VENDOR_SOURCES keyword is needed here.
# FREESTANDING follows CMAKE_CROSSCOMPILING, which is the helper's default.
house_cxx_add_strict_target(pico_util_strict)

# Declares PICO_UTIL_SANITIZE. Before the targets, and build-wide rather than
# target-scoped, so fetched GoogleTest is instrumented too -- ASan's
# container-overflow detector needs both sides of a std::string boundary.
house_cxx_enable_sanitizers(PICO_UTIL)

# Every knob this build has, declared before anything is added: a subdirectory
# reading an option that has not been declared yet reads an empty string.
house_cxx_declare_tests_option(PICO_UTIL)

# AR-2's cross-compiled probe. Nothing runs it; it exists so the linker has to
# resolve everything the umbrella header reaches, on a freestanding target.
# GoogleTest is not involved, so this is independent of PICO_UTIL_ENABLE_TESTS
# -- which is off when cross-compiling, which is the only time this is on.
pico_family_declare_probe_option(PICO_UTIL)

# Install/export only when this is the top-level project: as a subproject the
# parent owns the export set. A parent that does export this sets the cache
# variable ON before adding it, which option() then honours.
house_cxx_declare_install_option(PICO_UTIL)

add_subdirectory(src)

if(PICO_UTIL_ENABLE_TESTS)
    house_cxx_enable_gtest()
    add_subdirectory(tests)
endif()

if(PICO_UTIL_ENABLE_FIRMWARE_PROBE)
    add_subdirectory(tests/firmware)
endif()

if(PICO_UTIL_ENABLE_INSTALL)
    # pico_util_strict is exported because install(EXPORT) will not export a
    # target whose link interface names one outside the set. A consumer never
    # links pico_util::strict -- it carries this repo's warning flags, not part
    # of the shipped surface.
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
