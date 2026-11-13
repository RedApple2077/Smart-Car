#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sc_track.h"

#define CHECK(condition) \
  do { \
    if (!(condition)) { \
      fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
              __FILE__, __LINE__, #condition); \
      return 1; \
    } \
  } while (0)

static sc_line_state_t make_line(uint8_t black_mask, uint8_t valid,
                                 uint32_t sequence,
                                 uint32_t sample_time_ms)
{
  sc_line_state_t line = {0};

  line.sample.valid = valid;
  line.sample.sequence = sequence;
  line.sample.sample_time_ms = sample_time_ms;
  line.black_mask = black_mask;
  line.semantic_state = SC_LINE_UNKNOWN_PATTERN;
  return line;
}

static sc_track_status_t step(sc_track_controller_t *controller,
                              const sc_line_state_t *line,
                              uint32_t now_ms,
                              sc_wheel_target_t *target)
{
  return sc_track_controller_step(controller, line, now_ms, target);
}

static int check_follow_target(int32_t cruise_mm_s, uint8_t black_mask,
                               uint32_t sequence, int32_t expected_left,
                               int32_t expected_right,
                               uint8_t require_not_above_cruise)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;

  sc_track_controller_init(&controller, cruise_mm_s);
  line = make_line(black_mask, 1u, sequence, 0u);
  CHECK(step(&controller, &line, 0u, &target) == SC_TRACK_OK);
  CHECK(target.left_mm_s == expected_left);
  CHECK(target.right_mm_s == expected_right);
  if (require_not_above_cruise != 0u)
  {
    CHECK(target.left_mm_s <= cruise_mm_s);
    CHECK(target.right_mm_s <= cruise_mm_s);
  }
  return 0;
}

static int check_search_target(int32_t cruise_mm_s, uint8_t direction_mask,
                               int32_t expected_left,
                               int32_t expected_right)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;

  sc_track_controller_init(&controller, cruise_mm_s);
  line = make_line(direction_mask, 1u, 1u, 0u);
  CHECK(step(&controller, &line, 0u, &target) == SC_TRACK_OK);
  line = make_line(0x00u, 1u, 2u, 10u);
  CHECK(step(&controller, &line, 10u, &target) == SC_TRACK_RECOVERING);
  CHECK(target.left_mm_s == expected_left);
  CHECK(target.right_mm_s == expected_right);
  CHECK(target.left_mm_s <= cruise_mm_s);
  CHECK(target.right_mm_s <= cruise_mm_s);
  return 0;
}

static int test_ratio_table(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;

  sc_track_controller_init(&controller, 400);

  line = make_line(0x06u, 1u, 1u, 0u);
  CHECK(step(&controller, &line, 0u, &target) == SC_TRACK_OK);
  CHECK(target.left_mm_s == 400);
  CHECK(target.right_mm_s == 400);

  line = make_line(0x02u, 1u, 2u, 1u);
  CHECK(step(&controller, &line, 1u, &target) == SC_TRACK_OK);
  CHECK(target.left_mm_s == 340);
  CHECK(target.right_mm_s == 460);

  line = make_line(0x04u, 1u, 3u, 2u);
  CHECK(step(&controller, &line, 2u, &target) == SC_TRACK_OK);
  CHECK(target.left_mm_s == 460);
  CHECK(target.right_mm_s == 340);

  line = make_line(0x03u, 1u, 4u, 3u);
  CHECK(step(&controller, &line, 3u, &target) == SC_TRACK_OK);
  CHECK(target.left_mm_s == 160);
  CHECK(target.right_mm_s == 360);

  line = make_line(0x0Cu, 1u, 5u, 4u);
  CHECK(step(&controller, &line, 4u, &target) == SC_TRACK_OK);
  CHECK(target.left_mm_s == 360);
  CHECK(target.right_mm_s == 160);

  line = make_line(0x01u, 1u, 6u, 5u);
  CHECK(step(&controller, &line, 5u, &target) == SC_TRACK_OK);
  CHECK(target.left_mm_s == 40);
  CHECK(target.right_mm_s == 360);

  line = make_line(0x0Eu, 1u, 7u, 6u);
  CHECK(step(&controller, &line, 6u, &target) == SC_TRACK_OK);
  CHECK(target.left_mm_s == 400);
  CHECK(target.right_mm_s == 400);
  return 0;
}

static int test_cruise_scaling_and_clamp(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;

  sc_track_controller_init(&controller, 100);
  line = make_line(0x02u, 1u, 1u, 0u);
  CHECK(step(&controller, &line, 0u, &target) == SC_TRACK_OK);
  CHECK(target.left_mm_s == 85);
  CHECK(target.right_mm_s == 115);

  line = make_line(0x03u, 1u, 2u, 1u);
  CHECK(step(&controller, &line, 1u, &target) == SC_TRACK_OK);
  CHECK(target.left_mm_s == 40);
  CHECK(target.right_mm_s == 90);

  line = make_line(0x0Fu, 1u, 3u, 2u);
  CHECK(step(&controller, &line, 2u, &target) == SC_TRACK_OK);
  CHECK(target.left_mm_s == 100);
  CHECK(target.right_mm_s == 100);

  sc_track_controller_init(&controller, INT32_MAX);
  line = make_line(0x02u, 1u, 4u, 3u);
  CHECK(step(&controller, &line, 3u, &target) == SC_TRACK_OK);
  CHECK(target.left_mm_s == 1825361100);
  CHECK(target.right_mm_s == INT32_MAX);
  return 0;
}

static int test_fine_steering_is_conservative(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;

  sc_track_controller_init(&controller, 200);
  line = make_line(0x02u, 1u, 1u, 0u);
  CHECK(step(&controller, &line, 0u, &target) == SC_TRACK_OK);
  CHECK(target.left_mm_s == 170);
  CHECK(target.right_mm_s == 230);
  CHECK(((target.left_mm_s - target.right_mm_s) <= 60) &&
        ((target.right_mm_s - target.left_mm_s) <= 60));

  line = make_line(0x04u, 1u, 2u, 10u);
  CHECK(step(&controller, &line, 10u, &target) == SC_TRACK_OK);
  CHECK(target.left_mm_s == 230);
  CHECK(target.right_mm_s == 170);
  CHECK(((target.left_mm_s - target.right_mm_s) <= 60) &&
        ((target.right_mm_s - target.left_mm_s) <= 60));
  return 0;
}

static int test_severe_steering_derates_and_does_not_overspeed(void)
{
  CHECK(check_follow_target(400, 0x06u, 1u, 400, 400, 0u) == 0);
  CHECK(check_follow_target(400, 0x02u, 2u, 340, 460, 0u) == 0);
  CHECK(check_follow_target(400, 0x04u, 3u, 460, 340, 0u) == 0);
  CHECK(check_follow_target(400, 0x03u, 4u, 160, 360, 1u) == 0);
  CHECK(check_follow_target(400, 0x0Cu, 5u, 360, 160, 1u) == 0);
  CHECK(check_follow_target(400, 0x01u, 6u, 40, 360, 1u) == 0);
  CHECK(check_follow_target(400, 0x08u, 7u, 360, 40, 1u) == 0);
  CHECK(check_search_target(400, 0x02u, 120, 360) == 0);
  CHECK(check_search_target(400, 0x04u, 360, 120) == 0);

  CHECK(check_follow_target(300, 0x06u, 1u, 300, 300, 0u) == 0);
  CHECK(check_follow_target(300, 0x02u, 2u, 255, 345, 0u) == 0);
  CHECK(check_follow_target(300, 0x04u, 3u, 345, 255, 0u) == 0);
  CHECK(check_follow_target(300, 0x03u, 4u, 120, 270, 1u) == 0);
  CHECK(check_follow_target(300, 0x0Cu, 5u, 270, 120, 1u) == 0);
  CHECK(check_follow_target(300, 0x01u, 6u, 30, 270, 1u) == 0);
  CHECK(check_follow_target(300, 0x08u, 7u, 270, 30, 1u) == 0);
  CHECK(check_search_target(300, 0x02u, 90, 270) == 0);
  CHECK(check_search_target(300, 0x04u, 270, 90) == 0);

  CHECK(check_follow_target(200, 0x06u, 1u, 200, 200, 0u) == 0);
  CHECK(check_follow_target(200, 0x02u, 2u, 170, 230, 0u) == 0);
  CHECK(check_follow_target(200, 0x04u, 3u, 230, 170, 0u) == 0);
  CHECK(check_follow_target(200, 0x03u, 4u, 80, 180, 1u) == 0);
  CHECK(check_follow_target(200, 0x0Cu, 5u, 180, 80, 1u) == 0);
  CHECK(check_follow_target(200, 0x01u, 6u, 20, 180, 1u) == 0);
  CHECK(check_follow_target(200, 0x08u, 7u, 180, 20, 1u) == 0);
  CHECK(check_search_target(200, 0x02u, 60, 180) == 0);
  CHECK(check_search_target(200, 0x04u, 180, 60) == 0);
  return 0;
}

static int test_center_clears_direction(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;

  sc_track_controller_init(&controller, 400);
  line = make_line(0x02u, 1u, 1u, 0u);
  CHECK(step(&controller, &line, 0u, &target) == SC_TRACK_OK);
  CHECK(controller.last_turn_direction == -1);

  line = make_line(0x06u, 1u, 2u, 10u);
  CHECK(step(&controller, &line, 10u, &target) == SC_TRACK_OK);
  CHECK(controller.last_turn_direction == 0);
  CHECK(target.left_mm_s == 400);
  CHECK(target.right_mm_s == 400);

  line = make_line(0x04u, 1u, 3u, 20u);
  CHECK(step(&controller, &line, 20u, &target) == SC_TRACK_OK);
  CHECK(controller.last_turn_direction == 1);

  line = make_line(0x0Fu, 1u, 4u, 30u);
  CHECK(step(&controller, &line, 30u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_active == 0u);
  CHECK(controller.crossing_entry_sample_count == 1u);
  CHECK(controller.last_turn_direction == 0);
  CHECK(target.left_mm_s == 400);
  CHECK(target.right_mm_s == 400);

  line = make_line(0x05u, 1u, 5u, 40u);
  CHECK(step(&controller, &line, 40u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_entry_sample_count == 0u);
  CHECK(target.left_mm_s == 340);
  CHECK(target.right_mm_s == 460);
  return 0;
}

static int test_single_junction_does_not_enter(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;

  sc_track_controller_init(&controller, 200);
  line = make_line(0x0Eu, 1u, 1u, 100u);
  CHECK(step(&controller, &line, 100u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_active == 0u);
  CHECK(controller.crossing_entry_sample_count == 1u);
  CHECK(controller.crossing_start_ms == 100u);
  CHECK(target.left_mm_s == 200);
  CHECK(target.right_mm_s == 200);
  return 0;
}

static int test_all_junction_masks_enter(void)
{
  static const uint8_t junction_masks[] = {
    0x07u, 0x0Bu, 0x0Du, 0x0Eu, 0x0Fu
  };
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;
  uint32_t mask_index;

  for (mask_index = 0u;
       mask_index <
         (uint32_t)(sizeof(junction_masks) / sizeof(junction_masks[0]));
       mask_index++)
  {
    sc_track_controller_init(&controller, 200);
    line = make_line(junction_masks[mask_index], 1u, 1u, 100u);
    CHECK(step(&controller, &line, 100u, &target) == SC_TRACK_OK);
    CHECK(controller.crossing_active == 0u);
    CHECK(target.left_mm_s == 200);
    CHECK(target.right_mm_s == 200);
    line = make_line(junction_masks[mask_index], 1u, 2u, 110u);
    CHECK(step(&controller, &line, 110u, &target) == SC_TRACK_OK);
    CHECK(controller.crossing_active == 0u);
    CHECK(target.left_mm_s == 200);
    CHECK(target.right_mm_s == 200);
    line = make_line(junction_masks[mask_index], 1u, 3u, 120u);
    CHECK(step(&controller, &line, 120u, &target) == SC_TRACK_OK);
    CHECK(controller.crossing_active == 1u);
    CHECK(target.left_mm_s == 200);
    CHECK(target.right_mm_s == 200);
  }
  return 0;
}

static int test_junction_entry_requires_three_fresh_samples(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;

  sc_track_controller_init(&controller, 200);
  line = make_line(0x0Eu, 1u, 1u, 100u);
  CHECK(step(&controller, &line, 100u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_entry_sample_count == 1u);
  CHECK(target.left_mm_s == 200);
  CHECK(target.right_mm_s == 200);
  line = make_line(0x0Eu, 1u, 1u, 110u);
  CHECK(step(&controller, &line, 110u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_entry_sample_count == 0u);
  CHECK(controller.crossing_active == 0u);
  CHECK(target.left_mm_s == 200);
  CHECK(target.right_mm_s == 200);

  line = make_line(0x0Eu, 1u, 2u, 120u);
  CHECK(step(&controller, &line, 120u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_entry_sample_count == 1u);
  CHECK(target.left_mm_s == 200);
  CHECK(target.right_mm_s == 200);
  line = make_line(0x0Eu, 1u, 3u, 151u);
  CHECK(step(&controller, &line, 151u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_entry_sample_count == 0u);
  CHECK(controller.crossing_active == 0u);
  CHECK(target.left_mm_s == 200);
  CHECK(target.right_mm_s == 200);

  line = make_line(0x0Eu, 1u, 4u, 160u);
  CHECK(step(&controller, &line, 160u, &target) == SC_TRACK_OK);
  line = make_line(0x0Fu, 1u, 5u, 170u);
  CHECK(step(&controller, &line, 170u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_entry_sample_count == 2u);
  CHECK(controller.crossing_active == 0u);
  CHECK(target.left_mm_s == 200);
  CHECK(target.right_mm_s == 200);
  line = make_line(0x0Eu, 1u, 6u, 180u);
  CHECK(step(&controller, &line, 180u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_entry_sample_count == 3u);
  CHECK(controller.crossing_active == 1u);
  CHECK(controller.crossing_start_ms == 160u);
  CHECK(target.left_mm_s == 200);
  CHECK(target.right_mm_s == 200);
  return 0;
}

static int test_crossing_bridges_short_nonjunction_samples(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;

  sc_track_controller_init(&controller, 200);
  line = make_line(0x0Eu, 1u, 1u, 100u);
  CHECK(step(&controller, &line, 100u, &target) == SC_TRACK_OK);
  line = make_line(0x0Eu, 1u, 2u, 110u);
  CHECK(step(&controller, &line, 110u, &target) == SC_TRACK_OK);
  line = make_line(0x0Eu, 1u, 3u, 120u);
  CHECK(step(&controller, &line, 120u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_active == 1u);

  line = make_line(0x02u, 1u, 4u, 130u);
  CHECK(step(&controller, &line, 130u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_active == 1u);
  CHECK(target.left_mm_s == 200);
  CHECK(target.right_mm_s == 200);

  line = make_line(0x04u, 1u, 5u, 140u);
  CHECK(step(&controller, &line, 140u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_active == 1u);
  CHECK(target.left_mm_s == 200);
  CHECK(target.right_mm_s == 200);

  line = make_line(0x00u, 1u, 6u, 150u);
  CHECK(step(&controller, &line, 150u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_active == 1u);
  CHECK(controller.recovering == 0u);
  CHECK(target.left_mm_s == 200);
  CHECK(target.right_mm_s == 200);

  line = make_line(0x06u, 1u, 7u, 160u);
  CHECK(step(&controller, &line, 160u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_active == 1u);
  CHECK(controller.crossing_center_sample_count == 1u);
  CHECK(target.left_mm_s == 200);
  CHECK(target.right_mm_s == 200);
  return 0;
}

static int test_crossing_persistent_junction_times_out_with_cooldown(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;

  sc_track_controller_init(&controller, 200);
  line = make_line(0x0Eu, 1u, 1u, 100u);
  CHECK(step(&controller, &line, 100u, &target) == SC_TRACK_OK);
  line = make_line(0x0Eu, 1u, 2u, 110u);
  CHECK(step(&controller, &line, 110u, &target) == SC_TRACK_OK);
  line = make_line(0x0Eu, 1u, 3u, 120u);
  CHECK(step(&controller, &line, 120u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_active == 1u);

  line = make_line(0x0Eu, 1u, 4u, 590u);
  CHECK(step(&controller, &line, 590u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_active == 1u);
  CHECK(target.left_mm_s == 200);
  CHECK(target.right_mm_s == 200);

  line = make_line(0x0Eu, 1u, 5u, 600u);
  CHECK(step(&controller, &line, 600u, &target) == SC_TRACK_ERR_LOST);
  CHECK(controller.crossing_active == 0u);
  CHECK(controller.crossing_cooldown_active == 1u);
  CHECK(controller.recovering == 1u);
  CHECK(controller.lost_since_ms == 600u);
  CHECK(target.left_mm_s == 0);
  CHECK(target.right_mm_s == 0);

  line = make_line(0x0Eu, 1u, 6u, 610u);
  CHECK(step(&controller, &line, 610u, &target) == SC_TRACK_ERR_LOST);
  CHECK(controller.crossing_active == 0u);
  line = make_line(0x0Eu, 1u, 7u, 620u);
  CHECK(step(&controller, &line, 620u, &target) == SC_TRACK_ERR_LOST);
  CHECK(controller.crossing_active == 0u);
  line = make_line(0x0Eu, 1u, 8u, 630u);
  CHECK(step(&controller, &line, 630u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_active == 0u);
  CHECK(controller.crossing_cooldown_active == 1u);

  line = make_line(0x0Eu, 1u, 9u, 790u);
  CHECK(step(&controller, &line, 790u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_active == 0u);
  CHECK(controller.crossing_entry_sample_count == 0u);

  line = make_line(0x0Eu, 1u, 10u, 800u);
  CHECK(step(&controller, &line, 800u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_cooldown_active == 0u);
  CHECK(controller.crossing_entry_sample_count == 1u);
  CHECK(controller.crossing_active == 0u);
  line = make_line(0x0Eu, 1u, 11u, 810u);
  CHECK(step(&controller, &line, 810u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_entry_sample_count == 2u);
  line = make_line(0x0Eu, 1u, 12u, 820u);
  CHECK(step(&controller, &line, 820u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_active == 1u);
  return 0;
}

static int test_crossing_exits_on_three_center_samples(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;

  sc_track_controller_init(&controller, 200);
  line = make_line(0x0Eu, 1u, 1u, 100u);
  CHECK(step(&controller, &line, 100u, &target) == SC_TRACK_OK);
  line = make_line(0x0Eu, 1u, 2u, 110u);
  CHECK(step(&controller, &line, 110u, &target) == SC_TRACK_OK);
  line = make_line(0x0Eu, 1u, 3u, 120u);
  CHECK(step(&controller, &line, 120u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_active == 1u);

  line = make_line(0x06u, 1u, 4u, 130u);
  CHECK(step(&controller, &line, 130u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_center_sample_count == 1u);
  CHECK(controller.crossing_active == 1u);
  line = make_line(0x06u, 1u, 5u, 140u);
  CHECK(step(&controller, &line, 140u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_center_sample_count == 2u);
  CHECK(controller.crossing_active == 1u);

  line = make_line(0x06u, 1u, 5u, 145u);
  CHECK(step(&controller, &line, 145u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_center_sample_count == 0u);
  CHECK(controller.crossing_active == 1u);

  line = make_line(0x06u, 1u, 6u, 150u);
  CHECK(step(&controller, &line, 150u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_center_sample_count == 1u);
  line = make_line(0x06u, 1u, 7u, 181u);
  CHECK(step(&controller, &line, 181u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_center_sample_count == 0u);
  CHECK(controller.crossing_active == 1u);

  line = make_line(0x04u, 1u, 8u, 190u);
  CHECK(step(&controller, &line, 190u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_center_sample_count == 0u);
  CHECK(controller.crossing_active == 1u);

  line = make_line(0x06u, 1u, 9u, 200u);
  CHECK(step(&controller, &line, 200u, &target) == SC_TRACK_OK);
  line = make_line(0x06u, 1u, 10u, 210u);
  CHECK(step(&controller, &line, 210u, &target) == SC_TRACK_OK);
  line = make_line(0x06u, 1u, 11u, 220u);
  CHECK(step(&controller, &line, 220u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_active == 0u);
  CHECK(controller.crossing_center_sample_count == 0u);
  CHECK(target.left_mm_s == 200);
  CHECK(target.right_mm_s == 200);

  line = make_line(0x02u, 1u, 12u, 230u);
  CHECK(step(&controller, &line, 230u, &target) == SC_TRACK_OK);
  CHECK(target.left_mm_s == 170);
  CHECK(target.right_mm_s == 230);
  return 0;
}

static int test_crossing_ordinary_line_exits_after_three(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;

  sc_track_controller_init(&controller, 200);
  line = make_line(0x0Eu, 1u, 1u, 100u);
  CHECK(step(&controller, &line, 100u, &target) == SC_TRACK_OK);
  line = make_line(0x0Eu, 1u, 2u, 110u);
  CHECK(step(&controller, &line, 110u, &target) == SC_TRACK_OK);
  line = make_line(0x0Eu, 1u, 3u, 120u);
  CHECK(step(&controller, &line, 120u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_active == 1u);

  line = make_line(0x04u, 1u, 4u, 130u);
  CHECK(step(&controller, &line, 130u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_active == 1u);
  CHECK(controller.crossing_ordinary_sample_count == 1u);
  CHECK(controller.crossing_ordinary_last_mask == 0x04u);
  CHECK(target.left_mm_s == 200);
  CHECK(target.right_mm_s == 200);

  line = make_line(0x04u, 1u, 5u, 140u);
  CHECK(step(&controller, &line, 140u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_active == 1u);
  CHECK(controller.crossing_ordinary_sample_count == 2u);
  line = make_line(0x04u, 1u, 6u, 150u);
  CHECK(step(&controller, &line, 150u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_active == 0u);
  CHECK(controller.crossing_ordinary_sample_count == 0u);
  CHECK(target.left_mm_s == 230);
  CHECK(target.right_mm_s == 170);
  return 0;
}

static int test_crossing_alternating_ordinary_masks_do_not_exit(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;

  sc_track_controller_init(&controller, 200);
  line = make_line(0x0Eu, 1u, 1u, 100u);
  CHECK(step(&controller, &line, 100u, &target) == SC_TRACK_OK);
  line = make_line(0x0Eu, 1u, 2u, 110u);
  CHECK(step(&controller, &line, 110u, &target) == SC_TRACK_OK);
  line = make_line(0x0Eu, 1u, 3u, 120u);
  CHECK(step(&controller, &line, 120u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_active == 1u);

  line = make_line(0x02u, 1u, 4u, 130u);
  CHECK(step(&controller, &line, 130u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_ordinary_sample_count == 1u);
  line = make_line(0x04u, 1u, 5u, 140u);
  CHECK(step(&controller, &line, 140u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_ordinary_sample_count == 0u);

  line = make_line(0x02u, 1u, 6u, 150u);
  CHECK(step(&controller, &line, 150u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_ordinary_sample_count == 1u);
  line = make_line(0x04u, 1u, 7u, 160u);
  CHECK(step(&controller, &line, 160u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_active == 1u);
  CHECK(controller.crossing_ordinary_sample_count == 0u);
  CHECK(target.left_mm_s == 200);
  CHECK(target.right_mm_s == 200);
  return 0;
}

static int test_crossing_ordinary_candidate_rejects_gap_and_duplicate(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;

  sc_track_controller_init(&controller, 200);
  line = make_line(0x0Eu, 1u, 1u, 100u);
  CHECK(step(&controller, &line, 100u, &target) == SC_TRACK_OK);
  line = make_line(0x0Eu, 1u, 2u, 110u);
  CHECK(step(&controller, &line, 110u, &target) == SC_TRACK_OK);
  line = make_line(0x0Eu, 1u, 3u, 120u);
  CHECK(step(&controller, &line, 120u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_active == 1u);

  line = make_line(0x02u, 1u, 4u, 130u);
  CHECK(step(&controller, &line, 130u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_ordinary_sample_count == 1u);
  line = make_line(0x02u, 1u, 5u, 161u);
  CHECK(step(&controller, &line, 161u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_ordinary_sample_count == 0u);

  line = make_line(0x02u, 1u, 6u, 170u);
  CHECK(step(&controller, &line, 170u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_ordinary_sample_count == 1u);
  line = make_line(0x02u, 1u, 6u, 180u);
  CHECK(step(&controller, &line, 180u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_ordinary_sample_count == 0u);

  line = make_line(0x02u, 1u, 7u, 190u);
  CHECK(step(&controller, &line, 190u, &target) == SC_TRACK_OK);
  line = make_line(0x02u, 1u, 8u, 200u);
  CHECK(step(&controller, &line, 200u, &target) == SC_TRACK_OK);
  line = make_line(0x02u, 1u, 9u, 210u);
  CHECK(step(&controller, &line, 210u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_active == 0u);
  CHECK(target.left_mm_s == 170);
  CHECK(target.right_mm_s == 230);
  return 0;
}

static int test_crossing_white_window_stops(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;

  sc_track_controller_init(&controller, 200);
  line = make_line(0x0Eu, 1u, 1u, 100u);
  CHECK(step(&controller, &line, 100u, &target) == SC_TRACK_OK);
  line = make_line(0x0Eu, 1u, 2u, 110u);
  CHECK(step(&controller, &line, 110u, &target) == SC_TRACK_OK);
  line = make_line(0x0Eu, 1u, 3u, 120u);
  CHECK(step(&controller, &line, 120u, &target) == SC_TRACK_OK);

  line = make_line(0x00u, 1u, 4u, 300u);
  CHECK(step(&controller, &line, 300u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_active == 1u);
  CHECK(target.left_mm_s == 200);
  CHECK(target.right_mm_s == 200);

  line = make_line(0x00u, 1u, 5u, 399u);
  CHECK(step(&controller, &line, 399u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_active == 1u);
  CHECK(target.left_mm_s == 200);
  CHECK(target.right_mm_s == 200);

  line = make_line(0x00u, 1u, 6u, 400u);
  CHECK(step(&controller, &line, 400u, &target) == SC_TRACK_ERR_LOST);
  CHECK(controller.crossing_active == 0u);
  CHECK(controller.crossing_white_active == 0u);
  CHECK(controller.crossing_cooldown_active == 1u);
  CHECK(controller.recovering == 1u);
  CHECK(controller.lost_since_ms == 400u);
  CHECK(target.left_mm_s == 0);
  CHECK(target.right_mm_s == 0);
  return 0;
}

static int test_crossing_timeout_single_ordinary_stops(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;

  sc_track_controller_init(&controller, 200);
  line = make_line(0x0Eu, 1u, 1u, 100u);
  CHECK(step(&controller, &line, 100u, &target) == SC_TRACK_OK);
  line = make_line(0x0Eu, 1u, 2u, 110u);
  CHECK(step(&controller, &line, 110u, &target) == SC_TRACK_OK);
  line = make_line(0x0Eu, 1u, 3u, 120u);
  CHECK(step(&controller, &line, 120u, &target) == SC_TRACK_OK);

  line = make_line(0x04u, 1u, 4u, 600u);
  CHECK(step(&controller, &line, 600u, &target) == SC_TRACK_ERR_LOST);
  CHECK(controller.crossing_active == 0u);
  CHECK(controller.crossing_cooldown_active == 1u);
  CHECK(controller.recovering == 1u);
  CHECK(controller.lost_since_ms == 600u);
  CHECK(target.left_mm_s == 0);
  CHECK(target.right_mm_s == 0);
  return 0;
}

static int test_crossing_deadline_and_center_priority(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;

  sc_track_controller_init(&controller, 200);
  line = make_line(0x0Eu, 1u, 1u, 100u);
  CHECK(step(&controller, &line, 100u, &target) == SC_TRACK_OK);
  line = make_line(0x0Eu, 1u, 2u, 110u);
  CHECK(step(&controller, &line, 110u, &target) == SC_TRACK_OK);
  line = make_line(0x0Eu, 1u, 3u, 120u);
  CHECK(step(&controller, &line, 120u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_active == 1u);

  line = make_line(0x0Eu, 1u, 4u, 599u);
  CHECK(step(&controller, &line, 599u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_active == 1u);
  CHECK(target.left_mm_s == 200);
  CHECK(target.right_mm_s == 200);
  line = make_line(0x0Eu, 1u, 5u, 600u);
  CHECK(step(&controller, &line, 600u, &target) == SC_TRACK_ERR_LOST);
  CHECK(controller.crossing_active == 0u);
  CHECK(controller.recovering == 1u);
  CHECK(target.left_mm_s == 0);
  CHECK(target.right_mm_s == 0);

  sc_track_controller_init(&controller, 200);
  line = make_line(0x0Eu, 1u, 1u, 100u);
  CHECK(step(&controller, &line, 100u, &target) == SC_TRACK_OK);
  line = make_line(0x0Eu, 1u, 2u, 110u);
  CHECK(step(&controller, &line, 110u, &target) == SC_TRACK_OK);
  line = make_line(0x0Eu, 1u, 3u, 120u);
  CHECK(step(&controller, &line, 120u, &target) == SC_TRACK_OK);
  line = make_line(0x06u, 1u, 4u, 580u);
  CHECK(step(&controller, &line, 580u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_center_sample_count == 1u);
  line = make_line(0x06u, 1u, 5u, 590u);
  CHECK(step(&controller, &line, 590u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_center_sample_count == 2u);
  line = make_line(0x06u, 1u, 6u, 600u);
  CHECK(step(&controller, &line, 600u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_active == 0u);
  CHECK(controller.crossing_center_sample_count == 0u);
  CHECK(controller.crossing_cooldown_active == 0u);
  CHECK(controller.recovering == 0u);
  CHECK(target.left_mm_s == 200);
  CHECK(target.right_mm_s == 200);
  return 0;
}

static int test_invalid_sample_clears_crossing(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;

  sc_track_controller_init(&controller, 200);
  line = make_line(0x0Eu, 1u, 1u, 100u);
  CHECK(step(&controller, &line, 100u, &target) == SC_TRACK_OK);
  line = make_line(0x0Eu, 1u, 2u, 110u);
  CHECK(step(&controller, &line, 110u, &target) == SC_TRACK_OK);
  line = make_line(0x0Eu, 1u, 3u, 120u);
  CHECK(step(&controller, &line, 120u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_active == 1u);

  line = make_line(0x0Eu, 0u, 4u, 130u);
  CHECK(step(&controller, &line, 130u, &target) ==
        SC_TRACK_ERR_INVALID_LINE);
  CHECK(controller.crossing_active == 0u);
  CHECK(controller.crossing_entry_sample_count == 0u);
  CHECK(controller.crossing_center_sample_count == 0u);
  CHECK(target.left_mm_s == 0);
  CHECK(target.right_mm_s == 0);

  sc_track_controller_init(&controller, 200);
  line = make_line(0x0Eu, 1u, 1u, 200u);
  CHECK(step(&controller, &line, 200u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_entry_sample_count == 1u);
  line = make_line(0x1Eu, 1u, 2u, 210u);
  CHECK(step(&controller, &line, 210u, &target) ==
        SC_TRACK_ERR_INVALID_LINE);
  CHECK(controller.crossing_entry_sample_count == 0u);
  CHECK(controller.crossing_active == 0u);

  sc_track_controller_init(&controller, 200);
  line = make_line(0x0Eu, 1u, 1u, 300u);
  CHECK(step(&controller, &line, 300u, &target) == SC_TRACK_OK);
  line = make_line(0x0Eu, 1u, 2u, 310u);
  CHECK(step(&controller, &line, 310u, &target) == SC_TRACK_OK);
  line = make_line(0x0Eu, 1u, 3u, 320u);
  CHECK(step(&controller, &line, 320u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_active == 1u);
  line = make_line(0x1Eu, 1u, 4u, 330u);
  CHECK(step(&controller, &line, 330u, &target) ==
        SC_TRACK_ERR_INVALID_LINE);
  CHECK(controller.crossing_active == 0u);
  CHECK(controller.crossing_white_active == 0u);
  CHECK(controller.crossing_ordinary_sample_count == 0u);
  return 0;
}

static int test_unconditional_all_white_stop(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target = {-1, -1};
  sc_line_state_t line = make_line(0x00u, 1u, 1u, 10u);

  sc_track_controller_init(&controller, 400);
  CHECK(step(&controller, &line, 10u, &target) == SC_TRACK_ERR_LOST);
  CHECK(target.left_mm_s == 0);
  CHECK(target.right_mm_s == 0);
  CHECK(controller.recovering == 1u);
  CHECK(controller.lost_since_ms == 10u);
  return 0;
}

static int test_left_and_right_search(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;

  sc_track_controller_init(&controller, 400);
  line = make_line(0x02u, 1u, 1u, 0u);
  CHECK(step(&controller, &line, 0u, &target) == SC_TRACK_OK);
  line = make_line(0x00u, 1u, 2u, 10u);
  CHECK(step(&controller, &line, 10u, &target) == SC_TRACK_RECOVERING);
  CHECK(target.left_mm_s == 120);
  CHECK(target.right_mm_s == 360);

  sc_track_controller_init(&controller, 400);
  line = make_line(0x04u, 1u, 3u, 20u);
  CHECK(step(&controller, &line, 20u, &target) == SC_TRACK_OK);
  line = make_line(0x00u, 1u, 4u, 30u);
  CHECK(step(&controller, &line, 30u, &target) == SC_TRACK_RECOVERING);
  CHECK(target.left_mm_s == 360);
  CHECK(target.right_mm_s == 120);
  return 0;
}

static int test_single_black_sample_does_not_reset_recovery(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;

  sc_track_controller_init(&controller, 396);
  line = make_line(0x02u, 1u, 1u, 0u);
  CHECK(step(&controller, &line, 0u, &target) == SC_TRACK_OK);

  line = make_line(0x00u, 1u, 2u, 10u);
  CHECK(step(&controller, &line, 10u, &target) == SC_TRACK_RECOVERING);
  CHECK(controller.lost_since_ms == 10u);

  line = make_line(0x06u, 1u, 3u, 20u);
  CHECK(step(&controller, &line, 20u, &target) == SC_TRACK_RECOVERING);
  CHECK(controller.recovering == 1u);
  CHECK(controller.lost_since_ms == 10u);
  CHECK(controller.recovery_sample_count == 1u);
  CHECK(target.left_mm_s == 119);
  CHECK(target.right_mm_s == 356);

  line = make_line(0x00u, 1u, 4u, 30u);
  CHECK(step(&controller, &line, 30u, &target) == SC_TRACK_RECOVERING);
  CHECK(controller.recovering == 1u);
  CHECK(controller.lost_since_ms == 10u);
  CHECK(controller.recovery_sample_count == 0u);
  CHECK(target.left_mm_s == 119);
  CHECK(target.right_mm_s == 356);
  return 0;
}

static int test_three_fresh_samples_and_minimum_time_confirm(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;

  sc_track_controller_init(&controller, 396);
  line = make_line(0x02u, 1u, 1u, 0u);
  CHECK(step(&controller, &line, 0u, &target) == SC_TRACK_OK);
  line = make_line(0x00u, 1u, 2u, 10u);
  CHECK(step(&controller, &line, 10u, &target) == SC_TRACK_RECOVERING);

  line = make_line(0x06u, 1u, 3u, 20u);
  CHECK(step(&controller, &line, 20u, &target) == SC_TRACK_RECOVERING);
  line = make_line(0x06u, 1u, 4u, 30u);
  CHECK(step(&controller, &line, 30u, &target) == SC_TRACK_RECOVERING);
  line = make_line(0x06u, 1u, 5u, 39u);
  CHECK(step(&controller, &line, 39u, &target) == SC_TRACK_RECOVERING);
  CHECK(controller.recovery_sample_count == 3u);
  CHECK(controller.recovering == 1u);

  line = make_line(0x06u, 1u, 6u, 40u);
  CHECK(step(&controller, &line, 40u, &target) == SC_TRACK_OK);
  CHECK(controller.recovering == 0u);
  CHECK(controller.recovery_sample_count == 0u);
  CHECK(controller.last_turn_direction == 0);
  CHECK(target.left_mm_s == 396);
  CHECK(target.right_mm_s == 396);
  return 0;
}

static int test_recovering_junction_masks_excluded(void)
{
  static const uint8_t direction_masks[] = {0x02u, 0x04u};
  static const uint8_t junction_masks[] = {
    0x07u, 0x0Bu, 0x0Du, 0x0Eu, 0x0Fu
  };
  size_t direction_index;
  size_t junction_index;

  for (direction_index = 0u;
       direction_index <
       (sizeof(direction_masks) / sizeof(direction_masks[0]));
       direction_index++)
  {
    for (junction_index = 0u;
         junction_index <
         (sizeof(junction_masks) / sizeof(junction_masks[0]));
         junction_index++)
    {
      sc_track_controller_t controller;
      sc_wheel_target_t target;
      sc_line_state_t line;
      int8_t expected_direction =
        (direction_masks[direction_index] == 0x02u) ? -1 : 1;

      sc_track_controller_init(&controller, 400);
      line = make_line(direction_masks[direction_index], 1u, 1u, 0u);
      CHECK(step(&controller, &line, 0u, &target) == SC_TRACK_OK);
      line = make_line(0x00u, 1u, 2u, 10u);
      CHECK(step(&controller, &line, 10u, &target) ==
            SC_TRACK_RECOVERING);
      line = make_line(0x06u, 1u, 3u, 20u);
      CHECK(step(&controller, &line, 20u, &target) ==
            SC_TRACK_RECOVERING);
      CHECK(controller.recovery_sample_count == 1u);

      line = make_line(junction_masks[junction_index], 1u, 4u, 30u);
      CHECK(step(&controller, &line, 30u, &target) ==
            SC_TRACK_RECOVERING);
      CHECK(controller.recovering == 1u);
      CHECK(controller.recovery_sample_count == 0u);
      CHECK(controller.crossing_entry_sample_count == 0u);
      CHECK(controller.crossing_active == 0u);
      CHECK(controller.last_turn_direction == expected_direction);
      CHECK(controller.lost_since_ms == 10u);
      if (expected_direction < 0)
      {
        CHECK(target.left_mm_s == 120);
        CHECK(target.right_mm_s == 360);
      }
      else
      {
        CHECK(target.left_mm_s == 360);
        CHECK(target.right_mm_s == 120);
      }

      line = make_line(junction_masks[junction_index], 1u, 5u, 40u);
      CHECK(step(&controller, &line, 40u, &target) ==
            SC_TRACK_RECOVERING);
      line = make_line(junction_masks[junction_index], 1u, 6u, 50u);
      CHECK(step(&controller, &line, 50u, &target) ==
            SC_TRACK_RECOVERING);
      CHECK(controller.recovering == 1u);
      CHECK(controller.recovery_sample_count == 0u);
      CHECK(controller.crossing_entry_sample_count == 0u);
      CHECK(controller.crossing_active == 0u);
      CHECK(controller.last_turn_direction == expected_direction);
      CHECK(controller.lost_since_ms == 10u);
    }
  }
  return 0;
}

static int test_recovering_junction_resets_candidate(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;

  sc_track_controller_init(&controller, 400);
  line = make_line(0x02u, 1u, 1u, 0u);
  CHECK(step(&controller, &line, 0u, &target) == SC_TRACK_OK);
  line = make_line(0x00u, 1u, 2u, 10u);
  CHECK(step(&controller, &line, 10u, &target) ==
        SC_TRACK_RECOVERING);

  line = make_line(0x06u, 1u, 3u, 20u);
  CHECK(step(&controller, &line, 20u, &target) ==
        SC_TRACK_RECOVERING);
  CHECK(controller.recovery_sample_count == 1u);
  line = make_line(0x0Du, 1u, 4u, 30u);
  CHECK(step(&controller, &line, 30u, &target) ==
        SC_TRACK_RECOVERING);
  CHECK(controller.recovery_sample_count == 0u);

  line = make_line(0x06u, 1u, 5u, 40u);
  CHECK(step(&controller, &line, 40u, &target) ==
        SC_TRACK_RECOVERING);
  line = make_line(0x06u, 1u, 6u, 50u);
  CHECK(step(&controller, &line, 50u, &target) ==
        SC_TRACK_RECOVERING);
  CHECK(controller.recovery_sample_count == 2u);
  line = make_line(0x07u, 1u, 7u, 60u);
  CHECK(step(&controller, &line, 60u, &target) ==
        SC_TRACK_RECOVERING);
  CHECK(controller.recovery_sample_count == 0u);
  CHECK(controller.crossing_entry_sample_count == 0u);

  line = make_line(0x06u, 1u, 8u, 70u);
  CHECK(step(&controller, &line, 70u, &target) ==
        SC_TRACK_RECOVERING);
  line = make_line(0x06u, 1u, 9u, 80u);
  CHECK(step(&controller, &line, 80u, &target) ==
        SC_TRACK_RECOVERING);
  line = make_line(0x06u, 1u, 10u, 90u);
  CHECK(step(&controller, &line, 90u, &target) == SC_TRACK_OK);
  CHECK(controller.recovering == 0u);
  CHECK(controller.recovery_sample_count == 0u);
  CHECK(controller.lost_since_ms == 10u);
  CHECK(target.left_mm_s == 400);
  CHECK(target.right_mm_s == 400);
  return 0;
}

static int test_recovery_candidate_window_and_wrap(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;

  sc_track_controller_init(&controller, 400);
  line = make_line(0x02u, 1u, 1u, 0u);
  CHECK(step(&controller, &line, 0u, &target) == SC_TRACK_OK);
  line = make_line(0x00u, 1u, 2u, 10u);
  CHECK(step(&controller, &line, 10u, &target) ==
        SC_TRACK_RECOVERING);

  line = make_line(0x06u, 1u, 10u, 20u);
  CHECK(step(&controller, &line, 20u, &target) ==
        SC_TRACK_RECOVERING);
  CHECK(controller.recovery_sample_count == 1u);
  line = make_line(0x06u, 1u, 10u, 30u);
  CHECK(step(&controller, &line, 30u, &target) ==
        SC_TRACK_RECOVERING);
  CHECK(controller.recovery_sample_count == 0u);

  line = make_line(0x06u, 1u, 11u, 40u);
  CHECK(step(&controller, &line, 40u, &target) ==
        SC_TRACK_RECOVERING);
  line = make_line(0x06u, 1u, 12u, 40u);
  CHECK(step(&controller, &line, 40u, &target) ==
        SC_TRACK_RECOVERING);
  CHECK(controller.recovery_sample_count == 1u);
  CHECK(controller.recovery_first_sample_time_ms == 40u);
  line = make_line(0x06u, 1u, 13u, 41u);
  CHECK(step(&controller, &line, 41u, &target) ==
        SC_TRACK_RECOVERING);
  CHECK(controller.recovery_sample_count == 2u);
  line = make_line(0x06u, 1u, 14u, 71u);
  CHECK(step(&controller, &line, 71u, &target) == SC_TRACK_OK);
  CHECK(controller.recovering == 0u);

  sc_track_controller_init(&controller, 400);
  line = make_line(0x04u, 1u, 1u, 100u);
  CHECK(step(&controller, &line, 100u, &target) == SC_TRACK_OK);
  line = make_line(0x00u, 1u, 2u, 110u);
  CHECK(step(&controller, &line, 110u, &target) ==
        SC_TRACK_RECOVERING);
  line = make_line(0x06u, 1u, 3u, 120u);
  CHECK(step(&controller, &line, 120u, &target) ==
        SC_TRACK_RECOVERING);
  line = make_line(0x06u, 1u, 4u, 151u);
  CHECK(step(&controller, &line, 151u, &target) ==
        SC_TRACK_RECOVERING);
  CHECK(controller.recovery_sample_count == 1u);
  CHECK(controller.recovery_first_sample_time_ms == 151u);
  line = make_line(0x06u, 1u, 5u, 152u);
  CHECK(step(&controller, &line, 152u, &target) ==
        SC_TRACK_RECOVERING);
  line = make_line(0x06u, 1u, 6u, 182u);
  CHECK(step(&controller, &line, 182u, &target) == SC_TRACK_OK);
  CHECK(controller.recovering == 0u);

  sc_track_controller_init(&controller, 400);
  line = make_line(0x02u, 1u, 1u, 200u);
  CHECK(step(&controller, &line, 200u, &target) == SC_TRACK_OK);
  line = make_line(0x00u, 1u, 2u, 210u);
  CHECK(step(&controller, &line, 210u, &target) ==
        SC_TRACK_RECOVERING);
  line = make_line(0x06u, 1u, UINT32_MAX - 2u, 220u);
  CHECK(step(&controller, &line, 220u, &target) ==
        SC_TRACK_RECOVERING);
  line = make_line(0x06u, 1u, UINT32_MAX - 1u, 230u);
  CHECK(step(&controller, &line, 230u, &target) ==
        SC_TRACK_RECOVERING);
  line = make_line(0x06u, 1u, UINT32_MAX, 239u);
  CHECK(step(&controller, &line, 239u, &target) ==
        SC_TRACK_RECOVERING);
  CHECK(controller.recovery_sample_count == 3u);
  CHECK(controller.recovering == 1u);
  line = make_line(0x06u, 1u, 0u, 240u);
  CHECK(step(&controller, &line, 240u, &target) == SC_TRACK_OK);
  CHECK(controller.recovering == 0u);
  CHECK(controller.lost_since_ms == 210u);

  sc_track_controller_init(&controller, 400);
  line = make_line(0x02u, 1u, 100u, UINT32_MAX - 20u);
  CHECK(step(&controller, &line, UINT32_MAX - 20u, &target) ==
        SC_TRACK_OK);
  line = make_line(0x00u, 1u, 101u, UINT32_MAX - 10u);
  CHECK(step(&controller, &line, UINT32_MAX - 10u, &target) ==
        SC_TRACK_RECOVERING);
  line = make_line(0x06u, 1u, 102u, UINT32_MAX - 9u);
  (void)step(&controller, &line, UINT32_MAX - 9u, &target);
  line = make_line(0x06u, 1u, 103u, UINT32_MAX - 4u);
  (void)step(&controller, &line, UINT32_MAX - 4u, &target);
  line = make_line(0x06u, 1u, 104u, UINT32_MAX);
  (void)step(&controller, &line, UINT32_MAX, &target);
  line = make_line(0x06u, 1u, 105u, 0u);
  (void)step(&controller, &line, 0u, &target);
  line = make_line(0x06u, 1u, 106u, 10u);
  {
    sc_track_status_t time_wrap_status =
      step(&controller, &line, 10u, &target);

    CHECK(time_wrap_status == SC_TRACK_OK);
  }
  CHECK(controller.recovering == 0u);
  CHECK(controller.lost_since_ms == UINT32_MAX - 10u);
  return 0;
}

static int test_recovery_mixed_ordinary_masks_confirm(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;

  sc_track_controller_init(&controller, 400);
  line = make_line(0x02u, 1u, 1u, 0u);
  CHECK(step(&controller, &line, 0u, &target) == SC_TRACK_OK);
  line = make_line(0x00u, 1u, 2u, 10u);
  CHECK(step(&controller, &line, 10u, &target) ==
        SC_TRACK_RECOVERING);
  line = make_line(0x06u, 1u, 3u, 20u);
  CHECK(step(&controller, &line, 20u, &target) ==
        SC_TRACK_RECOVERING);
  line = make_line(0x02u, 1u, 4u, 30u);
  CHECK(step(&controller, &line, 30u, &target) ==
        SC_TRACK_RECOVERING);
  CHECK(controller.recovery_sample_count == 2u);
  line = make_line(0x04u, 1u, 5u, 40u);
  CHECK(step(&controller, &line, 40u, &target) == SC_TRACK_OK);
  CHECK(controller.recovering == 0u);
  CHECK(controller.recovery_sample_count == 0u);
  CHECK(controller.last_turn_direction == 1);
  CHECK(target.left_mm_s == 460);
  CHECK(target.right_mm_s == 340);
  return 0;
}

static int test_recovery_ordinary_reconfirms_crossing(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;

  sc_track_controller_init(&controller, 400);
  line = make_line(0x02u, 1u, 1u, 0u);
  CHECK(step(&controller, &line, 0u, &target) == SC_TRACK_OK);
  line = make_line(0x00u, 1u, 2u, 10u);
  CHECK(step(&controller, &line, 10u, &target) ==
        SC_TRACK_RECOVERING);
  line = make_line(0x06u, 1u, 3u, 20u);
  CHECK(step(&controller, &line, 20u, &target) ==
        SC_TRACK_RECOVERING);
  line = make_line(0x03u, 1u, 4u, 30u);
  CHECK(step(&controller, &line, 30u, &target) ==
        SC_TRACK_RECOVERING);
  line = make_line(0x04u, 1u, 5u, 40u);
  CHECK(step(&controller, &line, 40u, &target) == SC_TRACK_OK);
  CHECK(controller.recovering == 0u);
  CHECK(controller.last_turn_direction == 1);
  CHECK(target.left_mm_s == 460);
  CHECK(target.right_mm_s == 340);

  line = make_line(0x0Eu, 1u, 6u, 50u);
  CHECK(step(&controller, &line, 50u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_entry_sample_count == 1u);
  CHECK(controller.crossing_active == 0u);
  line = make_line(0x0Eu, 1u, 7u, 60u);
  CHECK(step(&controller, &line, 60u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_entry_sample_count == 2u);
  CHECK(controller.crossing_active == 0u);
  line = make_line(0x0Eu, 1u, 8u, 70u);
  CHECK(step(&controller, &line, 70u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_entry_sample_count == 3u);
  CHECK(controller.crossing_active == 1u);
  CHECK(controller.crossing_start_ms == 50u);
  CHECK(controller.recovering == 0u);
  return 0;
}

static int test_duplicate_sequence_does_not_confirm(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;

  sc_track_controller_init(&controller, 396);
  line = make_line(0x02u, 1u, 1u, 0u);
  CHECK(step(&controller, &line, 0u, &target) == SC_TRACK_OK);
  line = make_line(0x00u, 1u, 2u, 10u);
  CHECK(step(&controller, &line, 10u, &target) == SC_TRACK_RECOVERING);

  line = make_line(0x06u, 1u, 10u, 20u);
  CHECK(step(&controller, &line, 20u, &target) == SC_TRACK_RECOVERING);
  CHECK(controller.recovery_sample_count == 1u);

  line = make_line(0x06u, 1u, 10u, 40u);
  CHECK(step(&controller, &line, 40u, &target) == SC_TRACK_RECOVERING);
  CHECK(controller.recovery_sample_count == 0u);
  CHECK(controller.recovering == 1u);
  CHECK(target.left_mm_s == 119);
  CHECK(target.right_mm_s == 356);
  return 0;
}

static int test_alternating_white_black_without_timeout(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;
  uint32_t now_ms;
  uint32_t sequence = 3u;

  sc_track_controller_init(&controller, 400);
  line = make_line(0x02u, 1u, 1u, 0u);
  CHECK(step(&controller, &line, 0u, &target) == SC_TRACK_OK);
  line = make_line(0x00u, 1u, 2u, 10u);
  CHECK(step(&controller, &line, 10u, &target) == SC_TRACK_RECOVERING);
  CHECK(controller.lost_since_ms == 10u);

  for (now_ms = 20u; now_ms <= 10000u; now_ms += 10u)
  {
    uint8_t mask = (((now_ms / 10u) & 1u) == 0u) ? 0x06u : 0x00u;

    line = make_line(mask, 1u, sequence++, now_ms);
    CHECK(step(&controller, &line, now_ms, &target) ==
          SC_TRACK_RECOVERING);
    CHECK(controller.recovering == 1u);
    CHECK(controller.lost_since_ms == 10u);
    CHECK(controller.recovery_sample_count <= 1u);
  }

  line = make_line(0x00u, 1u, sequence, 10010u);
  CHECK(step(&controller, &line, 10010u, &target) ==
        SC_TRACK_RECOVERING);
  CHECK(controller.recovering == 1u);
  CHECK(controller.lost_since_ms == 10u);
  CHECK(target.left_mm_s == 120);
  CHECK(target.right_mm_s == 360);
  return 0;
}

static int test_recovery_candidate_rejects_gap_and_duplicate(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;

  sc_track_controller_init(&controller, 396);
  line = make_line(0x02u, 1u, 1u, 0u);
  CHECK(step(&controller, &line, 0u, &target) == SC_TRACK_OK);
  line = make_line(0x00u, 1u, 2u, 10u);
  CHECK(step(&controller, &line, 10u, &target) == SC_TRACK_RECOVERING);

  line = make_line(0x06u, 1u, 3u, 20u);
  CHECK(step(&controller, &line, 20u, &target) == SC_TRACK_RECOVERING);
  CHECK(controller.recovery_sample_count == 1u);
  line = make_line(0x06u, 1u, 3u, 30u);
  CHECK(step(&controller, &line, 30u, &target) == SC_TRACK_RECOVERING);
  CHECK(controller.recovery_sample_count == 0u);

  line = make_line(0x06u, 1u, 4u, 40u);
  CHECK(step(&controller, &line, 40u, &target) == SC_TRACK_RECOVERING);
  CHECK(controller.recovery_sample_count == 1u);
  line = make_line(0x06u, 1u, 5u, 80u);
  CHECK(step(&controller, &line, 80u, &target) == SC_TRACK_RECOVERING);
  CHECK(controller.recovery_sample_count == 1u);
  CHECK(controller.recovery_first_sample_time_ms == 80u);
  return 0;
}

static int test_lost_search_without_timeout(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target = {-1, -1};
  sc_line_state_t line;

  sc_track_controller_init(&controller, 400);
  line = make_line(0x02u, 1u, 1u, 0u);
  CHECK(step(&controller, &line, 0u, &target) == SC_TRACK_OK);
  line = make_line(0x00u, 1u, 2u, 10u);
  CHECK(step(&controller, &line, 10u, &target) == SC_TRACK_RECOVERING);
  line = make_line(0x00u, 1u, 3u, 1009u);
  CHECK(step(&controller, &line, 1009u, &target) == SC_TRACK_RECOVERING);
  CHECK(target.left_mm_s == 120);
  CHECK(target.right_mm_s == 360);
  CHECK(controller.lost_since_ms == 10u);
  line = make_line(0x00u, 1u, 4u, 1010u);
  CHECK(step(&controller, &line, 1010u, &target) == SC_TRACK_RECOVERING);
  CHECK(target.left_mm_s == 120);
  CHECK(target.right_mm_s == 360);
  CHECK(controller.lost_since_ms == 10u);
  line = make_line(0x00u, 1u, 5u, 10000u);
  CHECK(step(&controller, &line, 10000u, &target) == SC_TRACK_RECOVERING);
  CHECK(target.left_mm_s == 120);
  CHECK(target.right_mm_s == 360);
  CHECK(controller.lost_since_ms == 10u);
  return 0;
}

static int test_recovering_full_black_context(void)
{
  static const uint8_t direction_masks[] = {0x02u, 0x04u};
  size_t direction_index;

  for (direction_index = 0u;
       direction_index <
       (sizeof(direction_masks) / sizeof(direction_masks[0]));
       direction_index++)
  {
    sc_track_controller_t controller;
    sc_wheel_target_t target;
    sc_line_state_t line;
    int8_t expected_direction =
      (direction_masks[direction_index] == 0x02u) ? -1 : 1;
    uint32_t now_ms;
    uint32_t sequence = 3u;

    sc_track_controller_init(&controller, 400);
    line = make_line(direction_masks[direction_index], 1u, 1u, 0u);
    CHECK(step(&controller, &line, 0u, &target) == SC_TRACK_OK);
    line = make_line(0x00u, 1u, 2u, 10u);
    CHECK(step(&controller, &line, 10u, &target) ==
          SC_TRACK_RECOVERING);

    for (now_ms = 20u; now_ms <= 510u; now_ms += 10u)
    {
      line = make_line(0x00u, 1u, sequence++, now_ms);
      CHECK(step(&controller, &line, now_ms, &target) ==
            SC_TRACK_RECOVERING);
    }
    for (now_ms = 520u; now_ms <= 600u; now_ms += 10u)
    {
      line = make_line(0x0Fu, 1u, sequence++, now_ms);
      CHECK(step(&controller, &line, now_ms, &target) ==
            SC_TRACK_RECOVERING);
      CHECK(controller.recovering == 1u);
      CHECK(controller.last_turn_direction == expected_direction);
      CHECK(controller.lost_since_ms == 10u);
      CHECK(controller.recovery_sample_count == 0u);
      CHECK(controller.crossing_entry_sample_count == 0u);
      CHECK(controller.crossing_active == 0u);
    }

    CHECK(target.left_mm_s ==
          ((expected_direction < 0) ? 120 : 360));
    CHECK(target.right_mm_s ==
          ((expected_direction < 0) ? 360 : 120));
  }
  return 0;
}

static int test_recovering_full_black_timeline_endpoint(void)
{
  static const uint8_t direction_masks[] = {0x02u, 0x04u};
  size_t direction_index;

  for (direction_index = 0u;
       direction_index <
       (sizeof(direction_masks) / sizeof(direction_masks[0]));
       direction_index++)
  {
    sc_track_controller_t controller;
    sc_wheel_target_t target = {0};
    sc_line_state_t line;
    sc_track_status_t final_status = SC_TRACK_OK;
    int8_t expected_direction =
      (direction_masks[direction_index] == 0x02u) ? -1 : 1;
    uint32_t now_ms;
    uint32_t last_now_ms = 0u;
    uint32_t sequence = 3u;

    sc_track_controller_init(&controller, 400);
    line = make_line(direction_masks[direction_index], 1u, 1u, 0u);
    CHECK(step(&controller, &line, 0u, &target) == SC_TRACK_OK);
    line = make_line(0x00u, 1u, 2u, 10u);
    CHECK(step(&controller, &line, 10u, &target) ==
          SC_TRACK_RECOVERING);

    for (now_ms = 20u; now_ms <= 1510u; now_ms += 10u)
    {
      line = make_line(0x00u, 1u, sequence++, now_ms);
      final_status = step(&controller, &line, now_ms, &target);
    }
    for (now_ms = 1520u; now_ms <= 11530u; now_ms += 10u)
    {
      line = make_line(0x0Fu, 1u, sequence++, now_ms);
      final_status = step(&controller, &line, now_ms, &target);
      last_now_ms = now_ms;
    }

    CHECK((last_now_ms == 11530u) &&
          (final_status == SC_TRACK_RECOVERING));
    CHECK(controller.recovering == 1u);
    CHECK(controller.last_turn_direction == expected_direction);
    CHECK(controller.lost_since_ms == 10u);
    CHECK(controller.recovery_sample_count == 0u);
    CHECK(controller.crossing_entry_sample_count == 0u);
    CHECK(controller.crossing_active == 0u);
    CHECK(target.left_mm_s ==
          ((expected_direction < 0) ? 120 : 360));
    CHECK(target.right_mm_s ==
          ((expected_direction < 0) ? 360 : 120));
  }
  return 0;
}

static int test_invalid_line_clears_recovery(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;

  sc_track_controller_init(&controller, 400);
  line = make_line(0x02u, 1u, 1u, 0u);
  CHECK(step(&controller, &line, 0u, &target) == SC_TRACK_OK);
  line = make_line(0x00u, 1u, 2u, 10u);
  CHECK(step(&controller, &line, 10u, &target) == SC_TRACK_RECOVERING);

  line = make_line(0x06u, 0u, 3u, 20u);
  CHECK(step(&controller, &line, 20u, &target) ==
        SC_TRACK_ERR_INVALID_LINE);
  line = make_line(0x10u, 1u, 4u, 21u);
  CHECK(step(&controller, &line, 21u, &target) ==
        SC_TRACK_ERR_INVALID_LINE);
  return 0;
}

static int test_ambiguous_patterns_and_arguments(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;

  sc_track_controller_init(&controller, 396);
  line = make_line(0x05u, 1u, 1u, 0u);
  CHECK(step(&controller, &line, 0u, &target) == SC_TRACK_OK);
  CHECK(target.left_mm_s == 337);
  CHECK(target.right_mm_s == 455);
  line = make_line(0x0Au, 1u, 2u, 1u);
  CHECK(step(&controller, &line, 1u, &target) == SC_TRACK_OK);
  CHECK(target.left_mm_s == 455);
  CHECK(target.right_mm_s == 337);
  line = make_line(0x0Bu, 1u, 3u, 2u);
  CHECK(step(&controller, &line, 2u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_active == 0u);
  CHECK(target.left_mm_s == 396);
  CHECK(target.right_mm_s == 396);
  line = make_line(0x0Du, 1u, 4u, 3u);
  CHECK(step(&controller, &line, 3u, &target) == SC_TRACK_OK);
  CHECK(controller.crossing_active == 0u);
  CHECK(target.left_mm_s == 396);
  CHECK(target.right_mm_s == 396);

  CHECK(step(NULL, &line, 4u, &target) == SC_TRACK_ERR_ARGUMENT);
  CHECK(step(&controller, NULL, 4u, &target) == SC_TRACK_ERR_ARGUMENT);
  CHECK(step(&controller, &line, 4u, NULL) == SC_TRACK_ERR_ARGUMENT);
  sc_track_controller_init(NULL, 396);
  return 0;
}

static int test_start_eligibility(void)
{
  sc_line_state_t line = make_line(0x06u, 1u, 1u, 0u);

  CHECK(sc_track_start_eligible(&line) == 1u);
  line.black_mask = 0x0Fu;
  CHECK(sc_track_start_eligible(&line) == 0u);
  line.black_mask = 0x04u;
  CHECK(sc_track_start_eligible(&line) == 0u);
  line.black_mask = 0x06u;
  line.sample.valid = 0u;
  CHECK(sc_track_start_eligible(&line) == 0u);
  CHECK(sc_track_start_eligible(NULL) == 0u);
  return 0;
}

typedef struct {
  const char *name;
  int (*run)(void);
} test_case_t;

static const test_case_t s_tests[] = {
  {"ratio_table", test_ratio_table},
  {"cruise_scaling_and_clamp", test_cruise_scaling_and_clamp},
  {"fine_steering_is_conservative", test_fine_steering_is_conservative},
  {"severe_steering_derates_and_does_not_overspeed",
   test_severe_steering_derates_and_does_not_overspeed},
  {"center_clears_direction", test_center_clears_direction},
  {"single_junction_does_not_enter", test_single_junction_does_not_enter},
  {"all_junction_masks_enter", test_all_junction_masks_enter},
  {"junction_entry_requires_three_fresh_samples",
   test_junction_entry_requires_three_fresh_samples},
  {"crossing_bridges_short_nonjunction_samples",
   test_crossing_bridges_short_nonjunction_samples},
  {"crossing_persistent_junction_times_out_with_cooldown",
   test_crossing_persistent_junction_times_out_with_cooldown},
  {"crossing_exits_on_three_center_samples",
   test_crossing_exits_on_three_center_samples},
  {"crossing_ordinary_line_exits_after_three",
   test_crossing_ordinary_line_exits_after_three},
  {"crossing_alternating_ordinary_masks_do_not_exit",
   test_crossing_alternating_ordinary_masks_do_not_exit},
  {"crossing_ordinary_candidate_rejects_gap_and_duplicate",
   test_crossing_ordinary_candidate_rejects_gap_and_duplicate},
  {"crossing_white_window_stops", test_crossing_white_window_stops},
  {"crossing_timeout_single_ordinary_stops",
   test_crossing_timeout_single_ordinary_stops},
  {"crossing_deadline_and_center_priority",
   test_crossing_deadline_and_center_priority},
  {"invalid_sample_clears_crossing", test_invalid_sample_clears_crossing},
  {"unconditional_all_white_stop", test_unconditional_all_white_stop},
  {"left_and_right_search", test_left_and_right_search},
  {"single_black_sample_does_not_reset_recovery",
   test_single_black_sample_does_not_reset_recovery},
  {"three_fresh_samples_and_minimum_time_confirm",
   test_three_fresh_samples_and_minimum_time_confirm},
  {"recovering_junction_masks_excluded",
   test_recovering_junction_masks_excluded},
  {"recovering_junction_resets_candidate",
   test_recovering_junction_resets_candidate},
  {"recovery_candidate_window_and_wrap",
   test_recovery_candidate_window_and_wrap},
  {"recovery_mixed_ordinary_masks_confirm",
   test_recovery_mixed_ordinary_masks_confirm},
  {"recovery_ordinary_reconfirms_crossing",
   test_recovery_ordinary_reconfirms_crossing},
  {"duplicate_sequence_does_not_confirm",
   test_duplicate_sequence_does_not_confirm},
  {"alternating_white_black_without_timeout",
   test_alternating_white_black_without_timeout},
  {"lost_search_without_timeout",
   test_lost_search_without_timeout},
  {"recovering_full_black_context",
   test_recovering_full_black_context},
  {"recovering_full_black_timeline_endpoint",
   test_recovering_full_black_timeline_endpoint},
  {"invalid_line_clears_recovery", test_invalid_line_clears_recovery},
  {"recovery_candidate_rejects_gap_and_duplicate",
   test_recovery_candidate_rejects_gap_and_duplicate},
  {"ambiguous_patterns_and_arguments",
   test_ambiguous_patterns_and_arguments},
  {"start_eligibility", test_start_eligibility}
};

int main(int argc, char **argv)
{
  size_t index;

  if (argc != 2)
  {
    return 2;
  }
  for (index = 0u; index < (sizeof(s_tests) / sizeof(s_tests[0])); index++)
  {
    if (strcmp(argv[1], s_tests[index].name) == 0)
    {
      return s_tests[index].run();
    }
  }
  return 2;
}
