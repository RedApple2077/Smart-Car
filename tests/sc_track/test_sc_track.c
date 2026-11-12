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

static int test_ratio_table(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;

  sc_track_controller_init(&controller, 396);

  line = make_line(0x06u, 1u, 1u, 0u);
  CHECK(step(&controller, &line, 0u, &target) == SC_TRACK_OK);
  CHECK(target.left_mm_s == 396);
  CHECK(target.right_mm_s == 396);

  line = make_line(0x02u, 1u, 2u, 1u);
  CHECK(step(&controller, &line, 1u, &target) == SC_TRACK_OK);
  CHECK(target.left_mm_s == 221);
  CHECK(target.right_mm_s == 526);

  line = make_line(0x04u, 1u, 3u, 2u);
  CHECK(step(&controller, &line, 2u, &target) == SC_TRACK_OK);
  CHECK(target.left_mm_s == 526);
  CHECK(target.right_mm_s == 221);

  line = make_line(0x03u, 1u, 4u, 3u);
  CHECK(step(&controller, &line, 3u, &target) == SC_TRACK_OK);
  CHECK(target.left_mm_s == 106);
  CHECK(target.right_mm_s == 549);

  line = make_line(0x0Cu, 1u, 5u, 4u);
  CHECK(step(&controller, &line, 4u, &target) == SC_TRACK_OK);
  CHECK(target.left_mm_s == 549);
  CHECK(target.right_mm_s == 106);

  line = make_line(0x01u, 1u, 6u, 5u);
  CHECK(step(&controller, &line, 5u, &target) == SC_TRACK_OK);
  CHECK(target.left_mm_s == 0);
  CHECK(target.right_mm_s == 564);

  line = make_line(0x0Eu, 1u, 7u, 6u);
  CHECK(step(&controller, &line, 6u, &target) == SC_TRACK_OK);
  CHECK(target.left_mm_s == 564);
  CHECK(target.right_mm_s == 0);
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
  CHECK(target.left_mm_s == 56);
  CHECK(target.right_mm_s == 133);

  line = make_line(0x03u, 1u, 2u, 1u);
  CHECK(step(&controller, &line, 1u, &target) == SC_TRACK_OK);
  CHECK(target.left_mm_s == 27);
  CHECK(target.right_mm_s == 139);

  line = make_line(0x0Fu, 1u, 3u, 2u);
  CHECK(step(&controller, &line, 2u, &target) == SC_TRACK_OK);
  CHECK(target.left_mm_s == 100);
  CHECK(target.right_mm_s == 100);

  sc_track_controller_init(&controller, INT32_MAX);
  line = make_line(0x01u, 1u, 4u, 3u);
  CHECK(step(&controller, &line, 3u, &target) == SC_TRACK_OK);
  CHECK(target.left_mm_s == 0);
  CHECK(target.right_mm_s == INT32_MAX);
  return 0;
}

static int test_center_and_junction_clear_direction(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;

  sc_track_controller_init(&controller, 396);
  line = make_line(0x02u, 1u, 1u, 0u);
  CHECK(step(&controller, &line, 0u, &target) == SC_TRACK_OK);
  CHECK(controller.last_turn_direction == -1);

  line = make_line(0x06u, 1u, 2u, 10u);
  CHECK(step(&controller, &line, 10u, &target) == SC_TRACK_OK);
  CHECK(controller.last_turn_direction == 0);
  CHECK(target.left_mm_s == 396);
  CHECK(target.right_mm_s == 396);

  line = make_line(0x04u, 1u, 3u, 20u);
  CHECK(step(&controller, &line, 20u, &target) == SC_TRACK_OK);
  CHECK(controller.last_turn_direction == 1);

  line = make_line(0x0Fu, 1u, 4u, 30u);
  CHECK(step(&controller, &line, 30u, &target) == SC_TRACK_OK);
  CHECK(controller.last_turn_direction == 0);
  CHECK(target.left_mm_s == 396);
  CHECK(target.right_mm_s == 396);

  line = make_line(0x05u, 1u, 5u, 40u);
  CHECK(step(&controller, &line, 40u, &target) == SC_TRACK_OK);
  CHECK(controller.last_turn_direction == -1);
  line = make_line(0x09u, 1u, 6u, 50u);
  CHECK(step(&controller, &line, 50u, &target) == SC_TRACK_OK);
  CHECK(controller.last_turn_direction == 0);
  CHECK(target.left_mm_s == 396);
  CHECK(target.right_mm_s == 396);
  return 0;
}

static int test_unconditional_all_white_stop(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target = {-1, -1};
  sc_line_state_t line = make_line(0x00u, 1u, 1u, 10u);

  sc_track_controller_init(&controller, 396);
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

  sc_track_controller_init(&controller, 396);
  line = make_line(0x02u, 1u, 1u, 0u);
  CHECK(step(&controller, &line, 0u, &target) == SC_TRACK_OK);
  line = make_line(0x00u, 1u, 2u, 10u);
  CHECK(step(&controller, &line, 10u, &target) == SC_TRACK_RECOVERING);
  CHECK(target.left_mm_s == 30);
  CHECK(target.right_mm_s == 473);

  sc_track_controller_init(&controller, 396);
  line = make_line(0x04u, 1u, 3u, 20u);
  CHECK(step(&controller, &line, 20u, &target) == SC_TRACK_OK);
  line = make_line(0x00u, 1u, 4u, 30u);
  CHECK(step(&controller, &line, 30u, &target) == SC_TRACK_RECOVERING);
  CHECK(target.left_mm_s == 473);
  CHECK(target.right_mm_s == 30);
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
  CHECK(target.left_mm_s == 30);
  CHECK(target.right_mm_s == 473);

  line = make_line(0x00u, 1u, 4u, 30u);
  CHECK(step(&controller, &line, 30u, &target) == SC_TRACK_RECOVERING);
  CHECK(controller.recovering == 1u);
  CHECK(controller.lost_since_ms == 10u);
  CHECK(controller.recovery_sample_count == 0u);
  CHECK(target.left_mm_s == 30);
  CHECK(target.right_mm_s == 473);
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
  CHECK(target.left_mm_s == 30);
  CHECK(target.right_mm_s == 473);
  return 0;
}

static int test_alternating_white_black_uses_total_deadline(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target;
  sc_line_state_t line;
  uint32_t now_ms;
  uint32_t sequence = 3u;

  sc_track_controller_init(&controller, 396);
  line = make_line(0x02u, 1u, 1u, 0u);
  CHECK(step(&controller, &line, 0u, &target) == SC_TRACK_OK);
  line = make_line(0x00u, 1u, 2u, 10u);
  CHECK(step(&controller, &line, 10u, &target) == SC_TRACK_RECOVERING);
  CHECK(controller.lost_since_ms == 10u);

  for (now_ms = 20u; now_ms < 1010u; now_ms += 10u)
  {
    uint8_t mask = (((now_ms / 10u) & 1u) == 0u) ? 0x06u : 0x00u;

    line = make_line(mask, 1u, sequence++, now_ms);
    CHECK(step(&controller, &line, now_ms, &target) ==
          SC_TRACK_RECOVERING);
    CHECK(controller.recovering == 1u);
    CHECK(controller.lost_since_ms == 10u);
    CHECK(controller.recovery_sample_count <= 1u);
  }

  line = make_line(0x06u, 1u, sequence, 1010u);
  CHECK(step(&controller, &line, 1010u, &target) ==
        SC_TRACK_ERR_LOST);
  CHECK(target.left_mm_s == 0);
  CHECK(target.right_mm_s == 0);
  CHECK(controller.lost_since_ms == 10u);
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

static int test_recovery_timeout_and_invalid_lines(void)
{
  sc_track_controller_t controller;
  sc_wheel_target_t target = {-1, -1};
  sc_line_state_t line;

  sc_track_controller_init(&controller, 396);
  line = make_line(0x02u, 1u, 1u, 0u);
  CHECK(step(&controller, &line, 0u, &target) == SC_TRACK_OK);
  line = make_line(0x00u, 1u, 2u, 10u);
  CHECK(step(&controller, &line, 10u, &target) == SC_TRACK_RECOVERING);
  line = make_line(0x00u, 1u, 3u, 1009u);
  CHECK(step(&controller, &line, 1009u, &target) == SC_TRACK_RECOVERING);
  line = make_line(0x00u, 1u, 4u, 1010u);
  CHECK(step(&controller, &line, 1010u, &target) == SC_TRACK_ERR_LOST);
  CHECK(target.left_mm_s == 0);
  CHECK(target.right_mm_s == 0);

  line = make_line(0x06u, 0u, 5u, 1011u);
  CHECK(step(&controller, &line, 1011u, &target) ==
        SC_TRACK_ERR_INVALID_LINE);
  line = make_line(0x10u, 1u, 6u, 1012u);
  CHECK(step(&controller, &line, 1012u, &target) ==
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
  CHECK(target.left_mm_s == 221);
  CHECK(target.right_mm_s == 526);
  line = make_line(0x0Au, 1u, 2u, 1u);
  CHECK(step(&controller, &line, 1u, &target) == SC_TRACK_OK);
  CHECK(target.left_mm_s == 526);
  CHECK(target.right_mm_s == 221);
  line = make_line(0x0Bu, 1u, 3u, 2u);
  CHECK(step(&controller, &line, 2u, &target) == SC_TRACK_OK);
  CHECK(target.left_mm_s == 221);
  CHECK(target.right_mm_s == 526);
  line = make_line(0x0Du, 1u, 4u, 3u);
  CHECK(step(&controller, &line, 3u, &target) == SC_TRACK_OK);
  CHECK(target.left_mm_s == 526);
  CHECK(target.right_mm_s == 221);

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
  {"center_and_junction_clear_direction",
   test_center_and_junction_clear_direction},
  {"unconditional_all_white_stop", test_unconditional_all_white_stop},
  {"left_and_right_search", test_left_and_right_search},
  {"single_black_sample_does_not_reset_recovery",
   test_single_black_sample_does_not_reset_recovery},
  {"three_fresh_samples_and_minimum_time_confirm",
   test_three_fresh_samples_and_minimum_time_confirm},
  {"duplicate_sequence_does_not_confirm",
   test_duplicate_sequence_does_not_confirm},
  {"alternating_white_black_uses_total_deadline",
   test_alternating_white_black_uses_total_deadline},
  {"recovery_candidate_rejects_gap_and_duplicate",
   test_recovery_candidate_rejects_gap_and_duplicate},
  {"recovery_timeout_and_invalid_lines",
   test_recovery_timeout_and_invalid_lines},
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
