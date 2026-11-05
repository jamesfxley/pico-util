# Tests are added after src/ so their module targets already exist. Keeping
# tests outside src/ means rules over shipped sources need no test exclusions.

# Two real threads exercise the SPSC handoff.
find_package(Threads REQUIRED)

# Each probe builds normally. PROBE selects one statement that must fail to
# compile. Each refusal test passes only on the diagnostic named by EXPECT, so
# an unrelated failure does not count. Strict flags turn a discarded nodiscard
# result into an error.
function(pico_util_refusal_probe NAME MODULE)
    add_library(${NAME} OBJECT refusal_probe.cpp)
    target_link_libraries(${NAME} PRIVATE pico_util_strict ${MODULE})
endfunction()

function(pico_util_refusal NAME MODULE PROBE EXPECT)
    add_library(${NAME} OBJECT EXCLUDE_FROM_ALL refusal_probe.cpp)
    target_link_libraries(${NAME} PRIVATE pico_util_strict ${MODULE})
    target_compile_definitions(${NAME} PRIVATE PROBE=${PROBE})
    # The failing variant is excluded from the compile database so target order
    # cannot select it as the analyser's command.
    set_target_properties(${NAME} PROPERTIES EXPORT_COMPILE_COMMANDS OFF)
    add_test(NAME ${NAME}
        COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR}
                -DTARGET=${NAME} "-DEXPECT=${EXPECT}"
                -P ${PROJECT_SOURCE_DIR}/tests/expect_refusal.cmake)
    # Serial execution avoids contention on the shared build tree.
    set_tests_properties(${NAME} PROPERTIES RESOURCE_LOCK pico_util_build_tree)
endfunction()

add_subdirectory(bytes)
add_subdirectory(containers)
add_subdirectory(core)
add_subdirectory(heap)
add_subdirectory(publish)
add_subdirectory(result)
add_subdirectory(testing)

# The examples in the documents, compiled from the documents.
add_subdirectory(docs)
