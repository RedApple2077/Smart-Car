cmake_minimum_required(VERSION 3.22)

project(sc_line_host_tests LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(SC_LF04_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sc_lf04.c"
    CACHE FILEPATH "sc_lf04.c under test")
set(SC_LINE_SERVICE_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sc_line_service.c"
    CACHE FILEPATH "sc_line_service.c under test")
set(SC_BT_BENCH_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sc_bt_bench.c"
    CACHE FILEPATH "sc_bt_bench.c integration source")
set(SC_WHEEL_SERVICE_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sc_wheel_service.c"
    CACHE FILEPATH "sc_wheel_service.c integration source")
set(SC_PID_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sc_pid.c"
    CACHE FILEPATH "sc_pid.c integration source")
set(SC_SPEED_CONTROLLER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sc_speed_controller.c"
    CACHE FILEPATH "sc_speed_controller.c integration source")

add_executable(sc_line_tests
    "${SC_LF04_SOURCE}"
    "${SC_LINE_SERVICE_SOURCE}"
    "${SC_BT_BENCH_SOURCE}"
    "${SC_WHEEL_SERVICE_SOURCE}"
    "${SC_PID_SOURCE}"
    "${SC_SPEED_CONTROLLER_SOURCE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/fake/fake_support.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_sc_line.c"
)
target_include_directories(sc_line_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fake"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Inc"
)
target_compile_definitions(sc_line_tests PRIVATE
    SC_BUILD_HC05_BENCH=1
)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(sc_line_tests PRIVATE
        -Wall
        -Wextra
        -Werror
    )
endif()

enable_testing()

set(SC_LINE_TEST_CASES
    lf_init_config
    lf_config_rejections
    lf_init_idempotent
    lf_sample_snapshot_sequence
    lf_sample_all_pins
    lf_sample_not_ready
    lf_config_failure_recovery
    line_first_get_not_ready
    line_all_sixteen_patterns
    line_invalid_and_argument
    line_unknown_valid
    integration_driver_service_bench
    bench_no_line_snapshot_safe_stop
)

foreach(test_case IN LISTS SC_LINE_TEST_CASES)
    add_test(NAME "${test_case}" COMMAND sc_line_tests "${test_case}")
endforeach()
