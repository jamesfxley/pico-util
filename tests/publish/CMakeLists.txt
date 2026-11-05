# Two threads exercise publication under concurrency.
house_cxx_add_module_test(pico_util publish
    SOURCES published_test.cpp
    LIBRARIES Threads::Threads
)

# The source option puts -O2 after -Og on the compile line, so it wins. This
# keeps the counter-wrap test's 2^31 + 2^32 stores practical in every build
# type, including sanitised builds.
house_cxx_add_module_test(pico_util publish
    SOURCES published_wrap_test.cpp
    NAME pico_util_publish_wrap_tests
)
set_property(SOURCE published_wrap_test.cpp APPEND PROPERTY COMPILE_OPTIONS -O2)

# Compile-failure probes check the strategy and payload type (CR-1, CR-7).
pico_util_refusal_probe(pico_util_publish_probe pico_util::publish)
pico_util_refusal(pico_util_refuses_published_without_strategy
    pico_util::publish 1 "template argument")
pico_util_refusal(pico_util_refuses_published_non_trivial_payload
    pico_util::publish 2 "copied through memcpy")
