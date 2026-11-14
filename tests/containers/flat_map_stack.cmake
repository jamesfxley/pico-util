# Frames stay below the 4 KiB value. Resetting through a temporary would put
# that value on the stack (BR-16).
#   cmake -DSU_DIR=<object dir> -DLIMIT=<bytes> -P flat_map_stack.cmake
file(GLOB_RECURSE USAGE "${SU_DIR}/*.su")
if(NOT USAGE)
    message(FATAL_ERROR "no .su file under ${SU_DIR}; -fstack-usage did not run")
endif()
file(STRINGS "${USAGE}" FRAMES)
if(NOT FRAMES)
    message(FATAL_ERROR "${USAGE} lists no functions")
endif()
foreach(FRAME IN LISTS FRAMES)
    string(REGEX MATCH "\t([0-9]+)\t" SIZE "${FRAME}")
    if(CMAKE_MATCH_1 GREATER_EQUAL LIMIT)
        message(FATAL_ERROR "frame of ${CMAKE_MATCH_1} bytes: ${FRAME}")
    endif()
endforeach()
