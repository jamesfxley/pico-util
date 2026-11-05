# The counter is exercised on utility allocation checks (BR-6, MR-5).
# A separate executable keeps the program-wide replacement out of other tests.
find_package(Threads REQUIRED)

house_cxx_add_module_test(pico_util testing
    SOURCES allocation_test.cpp
    LIBRARIES Threads::Threads
        pico_util::bytes
        pico_util::containers
        pico_util::heap
        pico_util::publish
        pico_util::result
)

# Guard death tests omit replaced operators to avoid an analyser leak report
# inside GoogleTest. allocation_guard_test.cpp gives the reason.
house_cxx_add_module_test(pico_util testing
    NAME pico_util_testing_guard_tests
    SOURCES allocation_guard_test.cpp
)

# Symbol checks detect missing replacements that default forwarding can hide
# (MR-5).
add_test(NAME pico_util_testing_every_form_replaced
    COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_NM}
            -DEXECUTABLE=$<TARGET_FILE:pico_util_testing_tests>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/every_form_replaced.cmake)
