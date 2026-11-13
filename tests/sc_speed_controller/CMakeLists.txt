cmake_minimum_required(VERSION 3.22)

project(sc_speed_controller_host_tests LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(SC_SPEED_CONTROLLER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sc_speed_controller.c"
    CACHE FILEPATH "sc_speed_controller.c under test")
set(SC_PID_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sc_pid.c"
    CACHE FILEPATH "sc_pid.c used by speed controller")

add_executable(sc_speed_controller_tests
    "${SC_SPEED_CONTROLLER_SOURCE}"
    "${SC_PID_SOURCE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_sc_speed_controller.c"
)
target_include_directories(sc_speed_controller_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Inc"
)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(sc_speed_controller_tests PRIVATE
        -Wall
        -Wextra
        -Werror
    )
endif()

enable_testing()

foreach(test_case IN ITEMS
    target_and_output_ramp_limit
    different_left_right_gains
    negative_target
    zero_target_and_stop
    first_feedback_wait
    duplicate_feedback_is_bumpless
    invalid_or_stale_feedback
    defaults_match_validated_bench_config
    pid_update_requires_stopped_and_bounds
    argument_validation
)
    add_test(NAME "${test_case}"
             COMMAND sc_speed_controller_tests "${test_case}")
endforeach()
