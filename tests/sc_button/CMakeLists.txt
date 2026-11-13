cmake_minimum_required(VERSION 3.22)

project(sc_button_host_tests LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(SC_BUTTON_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sc_button.c"
    CACHE FILEPATH "sc_button.c under test")

add_executable(sc_button_tests
    "${SC_BUTTON_SOURCE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/fake/fake_hal.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_sc_button.c"
)
target_include_directories(sc_button_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fake"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Inc"
)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(sc_button_tests PRIVATE
        -Wall
        -Wextra
        -Werror
    )
endif()

enable_testing()

foreach(test_case IN ITEMS
    initial_held_release_has_no_press
    press_release_sequence
    long_press_emitted_once
)
    add_test(NAME "${test_case}" COMMAND sc_button_tests "${test_case}")
endforeach()
