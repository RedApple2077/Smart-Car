cmake_minimum_required(VERSION 3.22)

project(sc_uart_host_tests LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(SC_UART_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sc_uart.c"
    CACHE FILEPATH "sc_uart.c under test")

add_executable(sc_uart_tests
    "${SC_UART_SOURCE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/fake/fake_hal.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_sc_uart.c"
)
target_include_directories(sc_uart_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fake"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Inc"
)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(sc_uart_tests PRIVATE
        -Wall
        -Wextra
        -Werror
    )
endif()

enable_testing()

set(SC_UART_TEST_CASES
    init_and_idempotency
    init_config_errors
    tx_boundary
    tx_wrap
    tx_start_retry
    rx_overflow
    rx_wrap
    rx_error_ordering
    rx_error_recovery
    dual_port_isolation
)

foreach(test_case IN LISTS SC_UART_TEST_CASES)
    add_test(NAME "${test_case}" COMMAND sc_uart_tests "${test_case}")
endforeach()
