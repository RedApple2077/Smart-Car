cmake_minimum_required(VERSION 3.22)

project(sc_wheel_service_host_tests LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(SC_WHEEL_SERVICE_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sc_wheel_service.c"
    CACHE FILEPATH "sc_wheel_service.c under test")

add_executable(sc_wheel_service_tests
    "${SC_WHEEL_SERVICE_SOURCE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_sc_wheel_service.c"
)
target_include_directories(sc_wheel_service_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Inc"
)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(sc_wheel_service_tests PRIVATE
        -Wall
        -Wextra
        -Werror
    )
endif()

enable_testing()

foreach(test_case IN ITEMS
    precision_sign_and_five_frame_window
    sliding_window_actual_time
    duplicate_sequence
    stale_snapshot
    time_wrap
    reset_frame
    left_right_independent_validity
    arguments_and_calibration
)
    add_test(NAME "${test_case}"
             COMMAND sc_wheel_service_tests "${test_case}")
endforeach()
