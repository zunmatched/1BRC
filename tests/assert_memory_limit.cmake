execute_process(
    COMMAND "${RUNNER}" "${LIMIT_MIB}" "${EXECUTABLE}" "${INPUT}"
    RESULT_VARIABLE exit_code
    OUTPUT_VARIABLE standard_output
    ERROR_VARIABLE standard_error
)

if(NOT exit_code EQUAL EXPECTED_EXIT)
    message(FATAL_ERROR
        "Expected exit ${EXPECTED_EXIT}, got ${exit_code}.\nstdout: ${standard_output}\nstderr: ${standard_error}")
endif()

if(DEFINED EXPECTED_OUTPUT)
    file(READ "${EXPECTED_OUTPUT}" expected_output)
    if(NOT standard_output STREQUAL expected_output)
        message(FATAL_ERROR "Output mismatch.\nExpected: ${expected_output}\nActual: ${standard_output}")
    endif()
endif()

if(DEFINED ERROR_PATTERN AND NOT standard_error MATCHES "${ERROR_PATTERN}")
    message(FATAL_ERROR "stderr did not match '${ERROR_PATTERN}': ${standard_error}")
endif()

