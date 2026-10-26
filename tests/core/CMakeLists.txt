# Built against the umbrella header alone, so it stays a check on the public
# surface rather than on one header in isolation.
house_cxx_add_module_test(pico_util core
    SOURCES pico_util_test.cpp
)

# The umbrella header's version constants are hand-maintained; feeding the
# project() version in here is what makes the test able to catch drift between
# the two rather than comparing the header against a literal copied from it.
#
# Host-side only: this library is header-only, so it has no translation unit of
# its own to static_assert from. Putting the definitions on pico_util::core
# instead would propagate build-time values into every installed consumer.
house_cxx_version_definitions(pico_util_core_tests pico_util PRIVATE)
