# Examples are read from Markdown at configure time. Harnesses supply what the
# examples omit, so an API change that breaks an example fails this build.
house_cxx_add_doc_examples(pico_util_doc_examples PACKAGE pico_util
    DOCUMENTS ${PROJECT_SOURCE_DIR}/README.md
              ${PROJECT_SOURCE_DIR}/docs/usage-guide.md
    GROUPS firmware
    GROUP_DOCUMENTS ${PROJECT_SOURCE_DIR}/docs/usage-guide.md
    HARNESS_DIR ${CMAKE_CURRENT_SOURCE_DIR}
    LIBRARIES pico_util::core pico_util::testing GTest::gtest)
