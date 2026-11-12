cmake_minimum_required(VERSION 3.22)

project(sc_telemetry_host_tests LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(SC_TELEMETRY_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sc_telemetry.c"
    CACHE FILEPATH "sc_telemetry.c under test")

add_executable(sc_telemetry_tests
    "${SC_TELEMETRY_SOURCE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/fake/fake_telemetry.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_sc_telemetry.c"
)
target_include_directories(sc_telemetry_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fake"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Inc"
)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(sc_telemetry_tests PRIVATE
        -Wall
        -Wextra
        -Werror
    )
endif()

enable_testing()

foreach(test_case IN ITEMS
    uart_rate_and_csv_frame
    uart_failure_only_degrades
    oled_rate_and_led
    display_failure_degrades
    async_display_failure_degrades
    arguments
    led_phase_patterns
    uart_period_wraps
)
    add_test(NAME "${test_case}" COMMAND sc_telemetry_tests "${test_case}")
endforeach()
