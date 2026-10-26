# Links pico_util::publish, the module it mirrors, rather than the umbrella.
# Threads::Threads because a publication protocol that is only ever exercised
# single-threaded is a protocol nothing has tested.
house_cxx_add_module_test(pico_util publish
    SOURCES published_test.cpp
    LIBRARIES Threads::Threads
)

# The counter-wrap case makes 2^31 + 2^32 stores. -O2 as a source option, which
# comes after the strict target's Debug -Og on the compile line, keeps it to
# seconds in every build type, sanitized included.
house_cxx_add_module_test(pico_util publish
    SOURCES published_wrap_test.cpp
    NAME pico_util_publish_wrap_tests
)
set_property(SOURCE published_wrap_test.cpp APPEND PROPERTY COMPILE_OPTIONS -O2)

# CR-1 and CR-7 as build failures; see tests/CMakeLists.txt.
pico_util_refusal_probe(pico_util_publish_probe pico_util::publish)
pico_util_refusal(pico_util_refuses_published_without_strategy
    pico_util::publish 1 "template argument")
pico_util_refusal(pico_util_refuses_published_non_trivial_payload
    pico_util::publish 2 "copied through memcpy")
