# The harness's first consumer is this library's own containers: proving they
# allocate nothing (BR-6) is what makes MR-5's counter a gate rather than a
# facility.
#
# Its own executable, because the operator replacement is program-wide --
# hosting it alongside tests that allocate freely would make every one of them
# depend on the counter's armed state.
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

# The guard's own contract, in a translation unit without the replaced operators.
# Separate because the replacement is program-wide: with it in place the static
# analyzer follows `operator new` into GoogleTest's matchers and reports a leak
# in gtest, which is neither this library's defect nor suppressible at a site it
# owns. See the file's own note.
house_cxx_add_module_test(pico_util testing
    NAME pico_util_testing_guard_tests
    SOURCES allocation_guard_test.cpp
)

# MR-5 by symbol: see the script for why no behavioural case can see a missing
# form.
add_test(NAME pico_util_testing_every_form_replaced
    COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_NM}
            -DEXECUTABLE=$<TARGET_FILE:pico_util_testing_tests>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/every_form_replaced.cmake)
