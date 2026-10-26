# The cross-compiled header probe. AR-2's job is that the headers compile and
# link freestanding; the image it produces is what the footprint gate measures.
#
# Declared here rather than in the root listfile: a target lives in the
# directory holding its sources, which is what lets a reader find it from the
# path.
# Freestanding inside the SDK tree: the probe links no SDK library, so it gets
# no linker script and no runtime from one. The helper supplies nosys, no
# exceptions and no RTTI, which is what catches host-only creep -- a throw, a
# dynamic_cast, an <iostream> -- before a firmware consumer's build would.
pico_family_add_cross_probe(pico_util_header_probe
    PACKAGE pico_util
    SOURCE header_probe.cpp
    LIBRARIES pico_util::core
)

# The allocation harness sits outside the umbrella, so the header probe never
# reaches it; this one holds it to AR-2 on its own.
pico_family_add_cross_probe(pico_util_testing_probe
    PACKAGE pico_util
    SOURCE testing_probe.cpp
    LIBRARIES pico_util::testing
)

# The usage guide's examples that need the SDK, compiled here from the guide
# (EN-03.5); the host tree's tests/docs compiles the rest.
house_cxx_add_doc_examples(pico_util_firmware_doc_examples PACKAGE pico_util
    GROUP firmware GROUPS firmware
    DOCUMENTS ${PROJECT_SOURCE_DIR}/docs/usage-guide.md
    HARNESS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/docs
    LIBRARIES pico_util::core pico_stdlib_headers)
