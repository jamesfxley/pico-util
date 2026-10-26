# Fixed-capacity containers (BR-1..BR-16, and CR-9 for the SPSC counter).
# Header-only, like every module here, so the target is INTERFACE and carries
# only its headers.
add_library(pico_util_containers INTERFACE)
add_library(pico_util::containers ALIAS pico_util_containers)

target_sources(pico_util_containers INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
    FILES
        include/pico_util/containers/fixed_string.hpp
        include/pico_util/containers/flat_map.hpp
        include/pico_util/containers/ring_buffer.hpp
        include/pico_util/containers/ring_index.hpp
)

target_include_directories(pico_util_containers INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(pico_util_containers INTERFACE cxx_std_20)
