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
add_executable(onebrc_buffered_io src/baseline.cpp)
target_compile_definitions(onebrc_buffered_io PRIVATE ONEBRC_INTEGER_PARSER=1 ONEBRC_NO_ROW_ALLOCATIONS=1 ONEBRC_BUFFERED_IO=1)
add_executable(onebrc_bounded_memory src/baseline.cpp)
target_compile_definitions(onebrc_bounded_memory PRIVATE ONEBRC_INTEGER_PARSER=1 ONEBRC_NO_ROW_ALLOCATIONS=1 ONEBRC_BUFFERED_IO=1 ONEBRC_BOUNDED_MEMORY=1)
if(WIN32)
    add_executable(onebrc_mmap src/baseline.cpp)
    target_compile_definitions(onebrc_mmap PRIVATE ONEBRC_INTEGER_PARSER=1 ONEBRC_NO_ROW_ALLOCATIONS=1 ONEBRC_MMAP=1)
endif()
add_executable(onebrc_generate tools/generate.cpp)
if(WIN32)
    add_executable(onebrc_memory_limit tools/memory_limit.cpp)
endif()

set(solution_targets onebrc_baseline onebrc_integer_parser onebrc_no_row_allocations onebrc_buffered_io onebrc_bounded_memory)
if(WIN32)
    list(APPEND solution_targets onebrc_mmap)
endif()

foreach(target IN LISTS solution_targets)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    endif()
endforeach()

if(MSVC)
    target_compile_options(onebrc_generate PRIVATE /W4 /permissive-)
    if(WIN32)
        target_compile_options(onebrc_memory_limit PRIVATE /W4 /permissive-)
    endif()
else()
    target_compile_options(onebrc_generate PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(WIN32)
        target_compile_options(onebrc_memory_limit PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    if(WIN32)
        add_executable(onebrc_bounded_memory_pressure src/baseline.cpp)
        target_compile_definitions(onebrc_bounded_memory_pressure PRIVATE
            ONEBRC_INTEGER_PARSER=1
            ONEBRC_NO_ROW_ALLOCATIONS=1
            ONEBRC_BUFFERED_IO=1
            ONEBRC_BOUNDED_MEMORY=1
            ONEBRC_BUFFER_SIZE_BYTES=67108864
        )
        if(MSVC)
            target_compile_options(onebrc_bounded_memory_pressure PRIVATE /W4 /permissive-)
        else()
            target_compile_options(onebrc_bounded_memory_pressure PRIVATE
                -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
        endif()
    endif()

    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/generated-fixtures)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/generated-fixtures/no-final-newline.txt "Solo;-0.5")
    if(WIN32)
        # file(WRITE) translates LF to CRLF on Windows; an explicit CR would be doubled.
        file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/generated-fixtures/crlf.txt "Alpha;1.0\nAlpha;2.0\n")
    else()
        file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/generated-fixtures/crlf.txt "Alpha;1.0\r\nAlpha;2.0\r\n")
    endif()

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
    add_test(
        NAME buffered_io_invalid_record
        COMMAND ${CMAKE_COMMAND}
            -DEXECUTABLE=$<TARGET_FILE:onebrc_buffered_io>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/invalid-record.txt
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert_failure.cmake
    )
    add_test(
        NAME bounded_memory_invalid_record
        COMMAND ${CMAKE_COMMAND}
            -DEXECUTABLE=$<TARGET_FILE:onebrc_bounded_memory>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/invalid-record.txt
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert_failure.cmake
    )
    if(WIN32)
        add_test(
            NAME mmap_invalid_record
            COMMAND ${CMAKE_COMMAND}
                -DEXECUTABLE=$<TARGET_FILE:onebrc_mmap>
                -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/invalid-record.txt
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert_failure.cmake
        )
        add_test(
            NAME bounded_memory_limit_success
            COMMAND ${CMAKE_COMMAND}
                -DRUNNER=$<TARGET_FILE:onebrc_memory_limit>
                -DLIMIT_MIB=32
                -DEXECUTABLE=$<TARGET_FILE:onebrc_bounded_memory>
                -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sample.txt
                -DEXPECTED_EXIT=0
                -DEXPECTED_OUTPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/expected/sample.txt
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert_memory_limit.cmake
        )
        add_test(
            NAME bounded_memory_limit_failure
            COMMAND ${CMAKE_COMMAND}
                -DRUNNER=$<TARGET_FILE:onebrc_memory_limit>
                -DLIMIT_MIB=32
                -DEXECUTABLE=$<TARGET_FILE:onebrc_bounded_memory_pressure>
                -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sample.txt
                -DEXPECTED_EXIT=3
                -DERROR_PATTERN=memory.allocation.failed
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/assert_memory_limit.cmake
        )
    endif()
endif()
