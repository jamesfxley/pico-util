# Threads::Threads supports the SPSC concurrency test.
house_cxx_add_module_test(pico_util containers
    SOURCES fixed_string_test.cpp flat_map_test.cpp ring_buffer_test.cpp
            ring_index_test.cpp
    LIBRARIES Threads::Threads
)

# The source option puts -O2 after -Og on the compile line, so it wins. The
# probe checks frames below 1 KiB for a 4 KiB value (BR-16).
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

# Compile-failure probes cover discarded results (BR-7) and throwing values
# (BR-16).
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
