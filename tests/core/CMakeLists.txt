# The test builds against the umbrella header alone, so it checks the public
# surface and not one header in isolation.
house_cxx_add_module_test(pico_util core
    SOURCES pico_util_test.cpp
)

# The host test compares header constants with the CMake version, not copied
# literals. Definitions on core would leak build-time values into consumers.
house_cxx_version_definitions(pico_util_core_tests pico_util PRIVATE)
