get_filename_component(output_directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")

execute_process(
    COMMAND "${EXECUTABLE}" "${INPUT}" ${ARGUMENTS}
    RESULT_VARIABLE exit_code
    OUTPUT_FILE "${OUTPUT}"
    ERROR_VARIABLE standard_error
)

if(NOT exit_code EQUAL 0)
    message(FATAL_ERROR "Program exited with ${exit_code}: ${standard_error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${OUTPUT}" "${EXPECTED}"
    RESULT_VARIABLE difference
)
if(NOT difference EQUAL 0)
    file(READ "${OUTPUT}" actual)
    file(READ "${EXPECTED}" expected)
    message(FATAL_ERROR "Output mismatch.\nExpected: ${expected}\nActual: ${actual}")
endif()
