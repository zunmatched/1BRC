if(DEFINED INPUT)
    execute_process(
        COMMAND "${EXECUTABLE}" "${INPUT}" ${ARGUMENTS}
        RESULT_VARIABLE exit_code
        OUTPUT_VARIABLE standard_output
        ERROR_VARIABLE standard_error
    )
else()
    execute_process(
        COMMAND "${EXECUTABLE}"
        RESULT_VARIABLE exit_code
        OUTPUT_VARIABLE standard_output
        ERROR_VARIABLE standard_error
    )
endif()

if(exit_code EQUAL 0)
    message(FATAL_ERROR "Program unexpectedly succeeded")
endif()
if(NOT standard_output STREQUAL "")
    message(FATAL_ERROR "Failure path wrote to stdout: ${standard_output}")
endif()
if(standard_error STREQUAL "")
    message(FATAL_ERROR "Failure path did not explain the error on stderr")
endif()
if(DEFINED ERROR_PATTERN AND NOT standard_error MATCHES "${ERROR_PATTERN}")
    message(FATAL_ERROR "stderr did not match '${ERROR_PATTERN}': ${standard_error}")
endif()
