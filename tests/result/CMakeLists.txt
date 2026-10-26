# Links pico_util::result, the module it mirrors, rather than the umbrella: the
# test tree mirrors src/ so that what a test covers is readable from its path.
house_cxx_add_module_test(pico_util result
    SOURCES callback_test.cpp result_test.cpp
)

# FR-1 and FR-6 as build failures; see tests/CMakeLists.txt.
pico_util_refusal_probe(pico_util_result_probe pico_util::result)
pico_util_refusal(pico_util_refuses_discarded_result pico_util::result 1
    "nodiscard")
pico_util_refusal(pico_util_refuses_result_same_type pico_util::result 2
    "do not convert to each other")
pico_util_refusal(pico_util_refuses_result_convertible_types pico_util::result 3
    "do not convert to each other")
