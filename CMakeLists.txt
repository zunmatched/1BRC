cmake_minimum_required(VERSION 3.20)

project(onebrc_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(onebrc_baseline src/baseline.cpp)
add_executable(onebrc_generate tools/generate.cpp)

foreach(target IN ITEMS onebrc_baseline onebrc_generate)
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

    function(add_cli_fixture_test name input expected)
        add_test(
            NAME ${name}
            COMMAND ${CMAKE_COMMAND}
                -DEXECUTABLE=$<TARGET_FILE:onebrc_baseline>
                -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/${input}
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/expected/${expected}
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/test-output/${name}.txt
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert_output.cmake
        )
    endfunction()

    add_cli_fixture_test(sample sample.txt sample.txt)
    add_cli_fixture_test(rounding rounding.txt rounding.txt)
    add_cli_fixture_test(utf8_long_names utf8-long-names.txt utf8-long-names.txt)
    add_cli_fixture_test(extreme_values extreme-values.txt extreme-values.txt)

    function(add_generated_fixture_test name input expected)
        add_test(
            NAME ${name}
            COMMAND ${CMAKE_COMMAND}
                -DEXECUTABLE=$<TARGET_FILE:onebrc_baseline>
                -DINPUT=${CMAKE_CURRENT_BINARY_DIR}/generated-fixtures/${input}
                -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/expected/${expected}
                -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/test-output/${name}.txt
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert_output.cmake
        )
    endfunction()

    add_generated_fixture_test(no_final_newline no-final-newline.txt no-final-newline.txt)
    add_generated_fixture_test(crlf crlf.txt crlf.txt)

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
endif()
