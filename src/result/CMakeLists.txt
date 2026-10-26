# Fallible returns and non-owning callbacks (FR-1..FR-11). Header-only, like
# every module here, so the target is INTERFACE and carries only its headers.
add_library(pico_util_result INTERFACE)
add_library(pico_util::result ALIAS pico_util_result)

target_sources(pico_util_result INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
    FILES
        include/pico_util/result/callback.hpp
        include/pico_util/result/result.hpp
)

target_include_directories(pico_util_result INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(pico_util_result INTERFACE cxx_std_20)
