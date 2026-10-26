# Links pico_util::heap, the module it mirrors, rather than the umbrella: the
# test tree mirrors src/ so that what a test covers is readable from its path.
house_cxx_add_module_test(pico_util heap
    SOURCES heap_watermark_test.cpp
)

# MR-8 over an empty heap, which only a program with no test framework has.
# Linked --as-needed: the case uses nothing from the C++ runtime, and loading it
# anyway puts its exception pool on the heap before main(), which the case would
# then refuse as a heap that was not empty. Stated here rather than inherited
# from whichever linker default the toolchain ships.
add_executable(pico_util_heap_fresh fresh_heap_measuring.cpp)
target_link_libraries(pico_util_heap_fresh PRIVATE pico_util_strict pico_util::heap)
target_link_options(pico_util_heap_fresh PRIVATE LINKER:--as-needed)
add_test(NAME pico_util_heap_fresh_measuring COMMAND pico_util_heap_fresh)
