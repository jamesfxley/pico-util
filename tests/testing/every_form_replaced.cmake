# Symbols detect all twenty replacements (MR-5). Default array/nothrow
# forwarding can reach a replaced scalar function and hide a missing form.
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
