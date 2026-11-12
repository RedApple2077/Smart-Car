cmake_minimum_required(VERSION 3.22)

project(sc_safety_host_tests LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(SC_SAFETY_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sc_safety.c"
    CACHE FILEPATH "sc_safety.c under test")

add_executable(sc_safety_tests
    "${SC_SAFETY_SOURCE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/fake/fake_registers.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_sc_safety.c"
)
target_include_directories(sc_safety_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fake"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Inc"
)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(sc_safety_tests PRIVATE
        -Wall
        -Wextra
        -Werror
    )
endif()

enable_testing()
foreach(test_case IN ITEMS
    emergency_stop_direct_registers
    iwdg_configuration_and_feed
    iwdg_timeout
)
    add_test(NAME "${test_case}" COMMAND sc_safety_tests "${test_case}")
endforeach()
