cmake_minimum_required(VERSION 3.22)

project(sc_track_host_tests LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(sc_track_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sc_track.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_sc_track.c"
)
target_include_directories(sc_track_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Inc"
)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(sc_track_tests PRIVATE -Wall -Wextra -Werror)
endif()

enable_testing()

foreach(test_case IN ITEMS straight_and_junction fine_turns large_turns contiguous_pair_turns lost_recovery ambiguous_patterns arguments)
    add_test(NAME "${test_case}" COMMAND sc_track_tests "${test_case}")
endforeach()
