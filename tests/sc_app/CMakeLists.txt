cmake_minimum_required(VERSION 3.22)

project(sc_app_host_tests LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(SC_APP_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sc_app.c"
    CACHE FILEPATH "sc_app.c under test")
set(SC_TRACK_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sc_track.c"
    CACHE FILEPATH "sc_track.c used by app tests")
set(SC_WHEEL_SERVICE_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sc_wheel_service.c"
    CACHE FILEPATH "sc_wheel_service.c used by app tests")
set(SC_PID_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sc_pid.c"
    CACHE FILEPATH "sc_pid.c used by app tests")
set(SC_SPEED_CONTROLLER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sc_speed_controller.c"
    CACHE FILEPATH "sc_speed_controller.c used by app tests")

add_executable(sc_app_tests
    "${SC_APP_SOURCE}"
    "${SC_TRACK_SOURCE}"
    "${SC_WHEEL_SERVICE_SOURCE}"
    "${SC_PID_SOURCE}"
    "${SC_SPEED_CONTROLLER_SOURCE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/fake/fake_app.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_sc_app.c"
)
target_include_directories(sc_app_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fake"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Inc"
)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(sc_app_tests PRIVATE
        -Wall
        -Wextra
        -Werror
    )
endif()

enable_testing()

foreach(test_case IN ITEMS
    defaults_match_bench_state
    power_on_held_release_does_not_start
    isolated_release_does_not_start
    center_window_and_start_order
    center_interruption_blocks_start
    single_side_feedback_invalid_blocks_start
    full_white_and_invalid_config_block_start
    long_press_stops_then_new_click_restarts
    active_all_white_faults
    recoverable_health_without_new_press_no_restart
    line_lost_recovery_requires_center
    feedback_recovery_requires_nonwhite_line
    tb_fault_is_fatal
    single_wheel_stall
    jittery_counts_fail_progress_window
    real_progress_passes_stall_window
    inner_zero_target_no_stall
    negative_command_forward_counts_no_direction_fault
    negative_command_reverse_counts_not_direction_fatal
    reverse_direction_is_fatal
    stale_feedback_faults
    scheduling_overrun_faults
    degraded_bits_do_not_stop
    snapshot_pwm_uses_motor_sign
)
    add_test(NAME "${test_case}" COMMAND sc_app_tests "${test_case}")
endforeach()
