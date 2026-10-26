# Links pico_util::containers, the module it mirrors, rather than the umbrella:
# the test tree mirrors src/ so that what a test covers is readable from its
# path. Threads::Threads for the SPSC counter's concurrency test.
house_cxx_add_module_test(pico_util containers
    SOURCES fixed_string_test.cpp flat_map_test.cpp ring_buffer_test.cpp
            ring_index_test.cpp
    LIBRARIES Threads::Threads
)

# BR-16's stack clause, measured: the probe is compiled at -O2 with
# -fstack-usage (as source options, after the strict target's Debug -Og) and a
# test holds every frame under 1 KiB against a 4 KiB value.
add_library(pico_util_flat_map_stack_probe OBJECT flat_map_stack_probe.cpp)
target_link_libraries(pico_util_flat_map_stack_probe
    PRIVATE pico_util_strict pico_util::containers)
set_property(SOURCE flat_map_stack_probe.cpp APPEND PROPERTY
    COMPILE_OPTIONS -O2 -fstack-usage)
add_test(NAME pico_util_flat_map_stack
    COMMAND ${CMAKE_COMMAND}
            -DSU_DIR=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/pico_util_flat_map_stack_probe.dir
            -DLIMIT=1024
            -P ${CMAKE_CURRENT_SOURCE_DIR}/flat_map_stack.cmake)

# BR-7 and BR-16's noexcept refusal as build failures; see tests/CMakeLists.txt.
pico_util_refusal_probe(pico_util_containers_probe pico_util::containers)
foreach(PICO_UTIL_PROBE_CASE
        "1;fixed_string_assign" "2;fixed_string_append"
        "3;flat_map_insert_or_assign" "4;flat_map_try_emplace"
        "5;ring_buffer_push" "6;ring_buffer_acquire")
    list(GET PICO_UTIL_PROBE_CASE 0 PICO_UTIL_PROBE_N)
    list(GET PICO_UTIL_PROBE_CASE 1 PICO_UTIL_PROBE_WHAT)
    pico_util_refusal(pico_util_refuses_discarded_${PICO_UTIL_PROBE_WHAT}
        pico_util::containers ${PICO_UTIL_PROBE_N} "nodiscard")
endforeach()
pico_util_refusal(pico_util_refuses_flat_map_throwing_value
    pico_util::containers 7 "constructor to be noexcept")
pico_util_refusal(pico_util_refuses_ring_buffer_without_counter
    pico_util::containers 8 "template argument")
