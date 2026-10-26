# Cross-context value publication (CR-1..CR-10). Header-only, like every module
# here, so the target is INTERFACE and carries only its headers.
add_library(pico_util_publish INTERFACE)
add_library(pico_util::publish ALIAS pico_util_publish)

target_sources(pico_util_publish INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
    FILES
        include/pico_util/publish/published.hpp
)

target_include_directories(pico_util_publish INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(pico_util_publish INTERFACE cxx_std_20)

# The slot ring reports an exhausted read through Result (CR-3), so publishing
# needs the result module's headers. The one link between modules here.
target_link_libraries(pico_util_publish INTERFACE pico_util_result)
