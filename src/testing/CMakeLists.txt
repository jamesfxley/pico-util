# The shipped harness serves sibling test trees (MR-5..MR-7, MR-9).
# The operator definitions in allocation_operators.ipp must not arrive through
# an umbrella include.
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
