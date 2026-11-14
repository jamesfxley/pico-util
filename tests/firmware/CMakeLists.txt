# The freestanding probe checks linking and supplies the footprint image (AR-2).
# No SDK library supplies a runtime or linker script. The helper uses nosys,
# no exceptions and no RTTI to expose hosted dependencies.
pico_family_add_cross_probe(pico_util_header_probe
    PACKAGE pico_util
    SOURCE header_probe.cpp
    LIBRARIES pico_util::core
)

# The umbrella excludes the harness, so this probe checks it separately (AR-2).
pico_family_add_cross_probe(pico_util_testing_probe
    PACKAGE pico_util
    SOURCE testing_probe.cpp
    LIBRARIES pico_util::testing
)

# SDK-dependent usage examples compile here. tests/docs builds the rest.
house_cxx_add_doc_examples(pico_util_firmware_doc_examples PACKAGE pico_util
    GROUP firmware GROUPS firmware
    DOCUMENTS ${PROJECT_SOURCE_DIR}/docs/usage-guide.md
    HARNESS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/docs
    LIBRARIES pico_util::core pico_stdlib_headers)
