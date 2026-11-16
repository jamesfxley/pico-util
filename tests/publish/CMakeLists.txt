# Two threads exercise publication under concurrency.
house_cxx_add_module_test(pico_util publish
    SOURCES published_test.cpp
    LIBRARIES Threads::Threads
)

# The counter-wrap test runs on one thread, so ThreadSanitizer can find nothing
# in it, and under ThreadSanitizer its 2^31 + 2^32 stores take minutes rather
# than seconds. The default and address builds still run it (CR-2, CR-3, CR-8).
if(NOT PICO_UTIL_SANITIZE STREQUAL "thread")
    # The source option puts -O2 after -Og on the compile line, so it wins.
    # This keeps the stores practical in every other build type.
    house_cxx_add_module_test(pico_util publish
        SOURCES published_wrap_test.cpp
        NAME pico_util_publish_wrap_tests
    )
    set_property(SOURCE published_wrap_test.cpp APPEND
        PROPERTY COMPILE_OPTIONS -O2)
endif()

# Compile-failure probes check the strategy and payload type (CR-1, CR-7).
pico_util_refusal_probe(pico_util_publish_probe pico_util::publish)
pico_util_refusal(pico_util_refuses_published_without_strategy
    pico_util::publish 1 "template argument")
pico_util_refusal(pico_util_refuses_published_non_trivial_payload
    pico_util::publish 2 "copied through memcpy")
