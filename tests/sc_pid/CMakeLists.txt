cmake_minimum_required(VERSION 3.22)

project(sc_pid_host_tests LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(SC_PID_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sc_pid.c"
    CACHE FILEPATH "sc_pid.c under test")

add_executable(sc_pid_tests
    "${SC_PID_SOURCE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_sc_pid.c"
)
target_include_directories(sc_pid_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Inc"
)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(sc_pid_tests PRIVATE
        -Wall
        -Wextra
        -Werror
    )
endif()

enable_testing()

foreach(test_case IN ITEMS
    init_and_reset
    derivative_on_measurement
    negative_integral_accumulation
    conditional_antiwindup
    zero_target_clears_history
    argument_validation
)
    add_test(NAME "${test_case}" COMMAND sc_pid_tests "${test_case}")
endforeach()
