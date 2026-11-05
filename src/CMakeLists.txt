set(PICO_UTIL_PUBLIC_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_subdirectory(bytes)
add_subdirectory(containers)
add_subdirectory(heap)
add_subdirectory(publish)
add_subdirectory(result)
add_subdirectory(testing)

# Version definitions belong on the host test: INTERFACE definitions here
# would propagate build-time values into installed consumers.
add_library(pico_util_core INTERFACE)
add_library(pico_util::core ALIAS pico_util_core)

target_sources(pico_util_core INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${PICO_UTIL_PUBLIC_INCLUDE_DIR}
    FILES
        ${PICO_UTIL_PUBLIC_INCLUDE_DIR}/pico_util/pico_util.hpp
)

target_include_directories(pico_util_core INTERFACE
    $<BUILD_INTERFACE:${PICO_UTIL_PUBLIC_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(pico_util_core INTERFACE cxx_std_20)

target_link_libraries(pico_util_core INTERFACE
    pico_util::bytes
    pico_util::containers
    pico_util::heap
    pico_util::publish
    pico_util::result
)
