cmake_minimum_required(VERSION 3.22)

project(sc_bt_bench_host_tests LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(SC_BT_BENCH_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sc_bt_bench.c"
    CACHE FILEPATH "sc_bt_bench.c under test")
set(SC_WHEEL_SERVICE_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sc_wheel_service.c"
    CACHE FILEPATH "sc_wheel_service.c used by bench tests")
set(SC_PID_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sc_pid.c"
    CACHE FILEPATH "sc_pid.c used by bench tests")
set(SC_SPEED_CONTROLLER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sc_speed_controller.c"
    CACHE FILEPATH "sc_speed_controller.c used by bench tests")

add_executable(sc_bt_bench_tests
    "${SC_BT_BENCH_SOURCE}"
    "${SC_WHEEL_SERVICE_SOURCE}"
    "${SC_PID_SOURCE}"
    "${SC_SPEED_CONTROLLER_SOURCE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/fake/fake_bench.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_sc_bt_bench.c"
)
target_include_directories(sc_bt_bench_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fake"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Inc"
)
target_compile_definitions(sc_bt_bench_tests PRIVATE
    SC_BUILD_HC05_BENCH=1
)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(sc_bt_bench_tests PRIVATE
        -Wall
        -Wextra
        -Werror
    )
endif()

enable_testing()

set(SC_BT_BENCH_TEST_CASES
    init_safe_standby
    ping_exact
    status_fields
    line_exact
    line_alias_fragmented
    line_candidate_states_valid
    line_does_not_extend_motion
    line_invalid_snapshot_safe_stop
    line_unavailable_and_bad_argument
    line_help_and_tx_full
    fragmented_crlf
    max_line_exact
    overlong_safe_stop
    overlong_resync
    invalid_byte_safe_stop
    arm_expiry_boundary
    arm_tick_wrap
    motor_bounds_single_wheel
    motor_bounds_rejected
    repeated_motion_safe_stop
    unarmed_motion_safe_stop
    auto_stop_deadline
    auto_stop_driver_failure
    motor_tick_wrap
    stop_command
    reset_only_stopped
    heartbeat_cadence
    heartbeat_suppressed_during_motion
    heartbeat_resumes_after_motion_stop
    capture_export_heartbeat_suppressed
    uart_error_resets_parser
    uart_io_failure_safe_stop
    driver_failure_safe_stop
    tx_full_stop_still_safe
    raw_requires_fresh_feedback
    raw_requires_arm_and_limits
    raw_stall_zero_progress
    raw_stall_jitter_keeps_window
    raw_stall_five_per_window_fails
    raw_stall_real_progress_passes
    raw_feedback_lost_sync_stops
    raw_feedback_sequence_freeze_stops
    speed_requires_arm_and_feedback
    speed_closed_loop_auto_stop
    speed_stall_zero_progress
    pid_ram_readback_and_state_gate
    capture_100hz_two_seconds
    capture_tx_full_retries
    speed_feedback_timeout_stops
    capture_requires_stopped_and_uart_error_safe
)

foreach(test_case IN LISTS SC_BT_BENCH_TEST_CASES)
    add_test(NAME "${test_case}" COMMAND sc_bt_bench_tests "${test_case}")
endforeach()
