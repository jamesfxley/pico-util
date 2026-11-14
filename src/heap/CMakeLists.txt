# Heap-growth observation after initialisation (MR-1..MR-4, MR-8).
add_library(pico_util_heap INTERFACE)
add_library(pico_util::heap ALIAS pico_util_heap)

target_sources(pico_util_heap INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
    FILES
        include/pico_util/heap/heap_watermark.hpp
)

target_include_directories(pico_util_heap INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(pico_util_heap INTERFACE cxx_std_20)
