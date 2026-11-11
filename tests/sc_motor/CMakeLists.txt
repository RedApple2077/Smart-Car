cmake_minimum_required(VERSION 3.22)

project(sc_motor_host_tests LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(SC_TB6612_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sc_tb6612.c"
    CACHE FILEPATH "sc_tb6612.c under test")
set(SC_ENCODER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sc_encoder.c"
    CACHE FILEPATH "sc_encoder.c under test")

add_executable(sc_motor_tests
    "${SC_TB6612_SOURCE}"
    "${SC_ENCODER_SOURCE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/fake/fake_hal.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_sc_motor.c"
)
target_include_directories(sc_motor_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fake"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Inc"
)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(sc_motor_tests PRIVATE
        -Wall
        -Wextra
        -Werror
    )
endif()

enable_testing()

set(SC_MOTOR_TEST_CASES
    tb_init_standby_idempotent
    tb_second_channel_rollback
    tb_init_event_failure_rollback
    tb_config_reject
    tb_apply_uninitialized_no_tim
    tb_scaling_same_direction
    tb_zero_high_impedance
    tb_out_of_range_safe_fault
    tb_set_standby_clear_sequence
    tb_set_standby_update_failure
    tb_direction_transition_order
    tb_generate_event_failure
    encoder_init_idempotent
    encoder_second_start_rollback
    encoder_config_reject
    encoder_baseline_and_period
    encoder_signed_wrap
    encoder_ambiguous_side
    encoder_long_interval
    encoder_delta_zero
    encoder_latest_cache
    encoder_reset_recovery
    encoder_time_wrap
)

foreach(test_case IN LISTS SC_MOTOR_TEST_CASES)
    add_test(NAME "${test_case}" COMMAND sc_motor_tests "${test_case}")
endforeach()
