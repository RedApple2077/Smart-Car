cmake_minimum_required(VERSION 3.22)

project(sc_track_host_tests LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(SC_TRACK_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sc_track.c"
    CACHE FILEPATH "sc_track.c under test")

add_executable(sc_track_tests
    "${SC_TRACK_SOURCE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_sc_track.c"
)
target_include_directories(sc_track_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Inc"
)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(sc_track_tests PRIVATE -Wall -Wextra -Werror)
endif()

enable_testing()

foreach(test_case IN ITEMS
    ratio_table
    cruise_scaling_and_clamp
    center_and_junction_clear_direction
    unconditional_all_white_stop
    left_and_right_search
    single_black_sample_does_not_reset_recovery
    three_fresh_samples_and_minimum_time_confirm
    duplicate_sequence_does_not_confirm
    alternating_white_black_uses_total_deadline
    recovery_candidate_rejects_gap_and_duplicate
    recovery_timeout_and_invalid_lines
    ambiguous_patterns_and_arguments
    start_eligibility
)
    add_test(NAME "${test_case}" COMMAND sc_track_tests "${test_case}")
endforeach()
