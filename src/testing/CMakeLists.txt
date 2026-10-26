# The allocation-counting harness (MR-5..MR-7, MR-9), exported with the package.
#
# Its consumers are this repository's own suite and the test trees of the
# sibling repositories, which is why it ships rather than staying under tests/.
#
# Deliberately NOT linked by pico_util::core and NOT reached by the umbrella:
# allocation_operators.ipp defines the replaceable allocation functions, which
# cannot be inline, so it belongs to exactly one translation unit per test
# executable and must never arrive through an umbrella include.
add_library(pico_util_testing INTERFACE)
add_library(pico_util::testing ALIAS pico_util_testing)

target_sources(pico_util_testing INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
    FILES
        include/pico_util/testing/allocation_counter.hpp
        include/pico_util/testing/allocation_operators.ipp
)

target_include_directories(pico_util_testing INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(pico_util_testing INTERFACE cxx_std_20)
