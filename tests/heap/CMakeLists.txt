house_cxx_add_module_test(pico_util heap
    SOURCES heap_watermark_test.cpp
)

# A framework-free executable observes an empty heap (MR-8).
# Explicit --as-needed avoids loading the unused C++ runtime's exception pool
# before main(), independently of the toolchain's default.
add_executable(pico_util_heap_fresh fresh_heap_measuring.cpp)
target_link_libraries(pico_util_heap_fresh PRIVATE pico_util_strict pico_util::heap)
target_link_options(pico_util_heap_fresh PRIVATE LINKER:--as-needed)
add_test(NAME pico_util_heap_fresh_measuring COMMAND pico_util_heap_fresh)
