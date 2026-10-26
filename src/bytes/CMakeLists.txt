# Wire-format byte access: both endiannesses, both span flavours (DR-1..DR-5).
# Header-only, like every module here, so the target is INTERFACE and carries
# only its headers.
add_library(pico_util_bytes INTERFACE)
add_library(pico_util::bytes ALIAS pico_util_bytes)

target_sources(pico_util_bytes INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
    FILES
        include/pico_util/bytes/as_bytes.hpp
        include/pico_util/bytes/byte_order.hpp
)

target_include_directories(pico_util_bytes INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(pico_util_bytes INTERFACE cxx_std_20)
