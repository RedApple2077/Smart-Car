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
    fine_steering_is_conservative
    severe_steering_derates_and_does_not_overspeed
    center_clears_direction
    single_junction_does_not_enter
    all_junction_masks_enter
    junction_entry_requires_three_fresh_samples
    crossing_bridges_short_nonjunction_samples
    crossing_persistent_junction_times_out_with_cooldown
    crossing_exits_on_three_center_samples
    crossing_ordinary_line_exits_after_three
    crossing_alternating_ordinary_masks_do_not_exit
    crossing_ordinary_candidate_rejects_gap_and_duplicate
    crossing_white_window_stops
    crossing_timeout_single_ordinary_stops
    crossing_deadline_and_center_priority
    invalid_sample_clears_crossing
    unconditional_all_white_stop
    left_and_right_search
    single_black_sample_does_not_reset_recovery
    three_fresh_samples_and_minimum_time_confirm
    recovering_junction_masks_excluded
    recovering_junction_resets_candidate
    recovery_candidate_window_and_wrap
    recovery_mixed_ordinary_masks_confirm
    recovery_ordinary_reconfirms_crossing
    duplicate_sequence_does_not_confirm
    alternating_white_black_without_timeout
    lost_search_without_timeout
    recovering_full_black_context
    recovering_full_black_timeline_endpoint
    invalid_line_clears_recovery
    recovery_candidate_rejects_gap_and_duplicate
    ambiguous_patterns_and_arguments
    start_eligibility
)
    add_test(NAME "${test_case}" COMMAND sc_track_tests "${test_case}")
endforeach()
