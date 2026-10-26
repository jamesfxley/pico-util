# MR-5: the executable defines all twenty replaceable allocation and
# deallocation functions. The standard's default nothrow and array forms
# forward to the scalar ones, so a form left unreplaced still reaches the
# counter and no behavioural case can see it missing; its symbol can be.
#
#   cmake -DNM=<nm> -DEXECUTABLE=<path> -P every_form_replaced.cmake
execute_process(
    COMMAND "${NM}" -C --defined-only "${EXECUTABLE}"
    OUTPUT_VARIABLE SYMBOLS
    RESULT_VARIABLE STATUS)
if(NOT STATUS EQUAL 0)
    message(FATAL_ERROR "${NM} could not read ${EXECUTABLE}")
endif()
string(REGEX MATCHALL "[^\n]* T operator (new|delete)[^\n]*" FORMS "${SYMBOLS}")
list(LENGTH FORMS FOUND)
if(NOT FOUND EQUAL 20)
    string(REPLACE ";" "\n" LISTED "${FORMS}")
    message(FATAL_ERROR
        "expected 20 replaced allocation functions, found ${FOUND}:\n${LISTED}")
endif()
