cmake_minimum_required(VERSION 3.20)

project(onebrc_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(onebrc_baseline src/baseline.cpp)
add_executable(onebrc_integer_parser src/baseline.cpp)
target_compile_definitions(onebrc_integer_parser PRIVATE ONEBRC_INTEGER_PARSER=1)
add_executable(onebrc_no_row_allocations src/baseline.cpp)
target_compile_definitions(onebrc_no_row_allocations PRIVATE ONEBRC_INTEGER_PARSER=1 ONEBRC_NO_ROW_ALLOCATIONS=1)
add_executable(onebrc_generate tools/generate.cpp)

foreach(target IN ITEMS onebrc_baseline onebrc_integer_parser onebrc_no_row_allocations onebrc_generate)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    endif()
endforeach()

include(CTest)
if(BUILD_TESTING)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/generated-fixtures)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/generated-fixtures/no-final-newline.txt "Solo;-0.5")
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/generated-fixtures/crlf.txt "Alpha;1.0\r\nAlpha;2.0\r\n")

    function(add_cli_fixture_test target name input expected)
        add_test(
            NAME ${target}_${name}
            COMMAND ${CMAKE_COMMAND}
                -DEXECUTABLE=$<TARGET_FILE:${target}>
                -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/${input}
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/expected/${expected}
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/test-output/${target}-${name}.txt
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert_output.cmake
        )
    endfunction()

    set(solution_targets onebrc_baseline onebrc_integer_parser onebrc_no_row_allocations)
    foreach(target IN LISTS solution_targets)
        add_cli_fixture_test(${target} sample sample.txt sample.txt)
        add_cli_fixture_test(${target} rounding rounding.txt rounding.txt)
        add_cli_fixture_test(${target} utf8_long_names utf8-long-names.txt utf8-long-names.txt)
        add_cli_fixture_test(${target} extreme_values extreme-values.txt extreme-values.txt)
    endforeach()

    function(add_generated_fixture_test target name input expected)
        add_test(
            NAME ${target}_${name}
            COMMAND ${CMAKE_COMMAND}
                -DEXECUTABLE=$<TARGET_FILE:${target}>
                -DINPUT=${CMAKE_CURRENT_BINARY_DIR}/generated-fixtures/${input}
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/expected/${expected}
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/test-output/${target}-${name}.txt
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert_output.cmake
        )
    endfunction()

    foreach(target IN LISTS solution_targets)
        add_generated_fixture_test(${target} no_final_newline no-final-newline.txt no-final-newline.txt)
        add_generated_fixture_test(${target} crlf crlf.txt crlf.txt)
    endforeach()

    add_test(
        NAME missing_input
        COMMAND ${CMAKE_COMMAND}
            -DEXECUTABLE=$<TARGET_FILE:onebrc_baseline>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert_failure.cmake
    )
    add_test(
        NAME invalid_record
        COMMAND ${CMAKE_COMMAND}
            -DEXECUTABLE=$<TARGET_FILE:onebrc_baseline>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/invalid-record.txt
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert_failure.cmake
    )
    add_test(
        NAME integer_parser_invalid_record
        COMMAND ${CMAKE_COMMAND}
            -DEXECUTABLE=$<TARGET_FILE:onebrc_integer_parser>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/invalid-record.txt
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert_failure.cmake
    )
    add_test(
        NAME no_row_allocations_invalid_record
        COMMAND ${CMAKE_COMMAND}
            -DEXECUTABLE=$<TARGET_FILE:onebrc_no_row_allocations>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/invalid-record.txt
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert_failure.cmake
    )
endif()
