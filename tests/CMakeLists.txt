# The test tree mirrors src/ (CXX-13.2), so src/ is exactly the shippable
# surface: an install, footprint, lint or coverage rule over it needs no test
# exclusion. One directory per module, added after src/ so every
# pico_util::<module> target the tests link already exists.

# The containers suite runs the SPSC ring under two real threads: the count is
# the only value both contexts touch, and a single-threaded test cannot tell
# a correct pairing from a missing one.
find_package(Threads REQUIRED)

# Guarantees that exist as a build failure. Each module's refusal_probe.cpp
# compiles cleanly as written and is part of every build; a PROBE=<n> copy of
# it enables one refused statement, is built only by its test, and passes only
# on the diagnostic that refusal names. The probes compile against the strict
# target, so a discarded [[nodiscard]] value is an error rather than a warning.
function(pico_util_refusal_probe NAME MODULE)
    add_library(${NAME} OBJECT refusal_probe.cpp)
    target_link_libraries(${NAME} PRIVATE pico_util_strict ${MODULE})
endfunction()

function(pico_util_refusal NAME MODULE PROBE EXPECT)
    add_library(${NAME} OBJECT EXCLUDE_FROM_ALL refusal_probe.cpp)
    target_link_libraries(${NAME} PRIVATE pico_util_strict ${MODULE})
    target_compile_definitions(${NAME} PRIVATE PROBE=${PROBE})
    # Built only by its test, and never an analysis context: a compile database
    # that lists it would let target order pick a refused variant as the unit's
    # command, so the normal object above is the one the analyser reads.
    set_target_properties(${NAME} PROPERTIES EXPORT_COMPILE_COMMANDS OFF)
    add_test(NAME ${NAME}
        COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR}
                -DTARGET=${NAME} "-DEXPECT=${EXPECT}"
                -P ${PROJECT_SOURCE_DIR}/tests/expect_refusal.cmake)
    # Each test drives the one build tree; two at once contend for it.
    set_tests_properties(${NAME} PROPERTIES RESOURCE_LOCK pico_util_build_tree)
endfunction()

add_subdirectory(bytes)
add_subdirectory(containers)
add_subdirectory(core)
add_subdirectory(heap)
add_subdirectory(publish)
add_subdirectory(result)
add_subdirectory(testing)

# The documents' examples, compiled from the documents (EN-03.5).
add_subdirectory(docs)
