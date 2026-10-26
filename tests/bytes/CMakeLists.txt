# Links pico_util::bytes, the module it mirrors, rather than the umbrella: the
# test tree mirrors src/ so that what a test covers is readable from its path.
house_cxx_add_module_test(pico_util bytes
    SOURCES as_bytes_test.cpp byte_order_test.cpp
)
