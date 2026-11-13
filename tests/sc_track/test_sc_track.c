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

static sc_line_state_t line_state(sc_line_class_t state)
{
  sc_line_state_t line = {0};

  line.sample.valid = 1u;
  line.semantic_state = state;
  return line;
}

static sc_track_status_t step(sc_track_controller_t *controller,
                              const sc_line_state_t *line,
                              uint32_t now_ms,
                              sc_track_command_t *command)
{
  return sc_track_controller_step(controller, line, now_ms, command);
}

static int test_straight_and_junction(void)
{
  sc_track_command_t command;
  sc_track_controller_t controller;
  sc_line_state_t line = line_state(SC_LINE_STRAIGHT);

  sc_track_controller_init(&controller);
  line.black_mask = 0x06u;
  CHECK(step(&controller, &line, 0u, &command) == SC_TRACK_OK);
  CHECK(command.left_permille == 396);
  CHECK(command.right_permille == 396);
  line.semantic_state = SC_LINE_JUNCTION_CANDIDATE;
  line.black_mask = 0x0Fu;
  CHECK(step(&controller, &line, 1u, &command) == SC_TRACK_OK);
  CHECK(command.left_permille == 396);
  CHECK(command.right_permille == 396);
  CHECK(controller.last_turn_direction == 0);
  return 0;
}

static int test_fine_turns(void)
{
  sc_track_command_t command;
  sc_track_controller_t controller;
  sc_line_state_t line = line_state(SC_LINE_LEFT_FINE_ADJUST_CANDIDATE);

  sc_track_controller_init(&controller);
  line.black_mask = 0x02u;
  CHECK(step(&controller, &line, 0u, &command) == SC_TRACK_OK);
  CHECK(command.left_permille == 221);
  CHECK(command.right_permille == 526);
  line.semantic_state = SC_LINE_RIGHT_FINE_ADJUST_CANDIDATE;
  line.black_mask = 0x04u;
  CHECK(step(&controller, &line, 1u, &command) == SC_TRACK_OK);
  CHECK(command.left_permille == 526);
  CHECK(command.right_permille == 221);
  return 0;
}

static int test_large_turns(void)
{
  sc_track_command_t command;
  sc_track_controller_t controller;
  sc_line_state_t line = line_state(SC_LINE_LEFT_LARGE_BEND_CANDIDATE);

  sc_track_controller_init(&controller);
  line.black_mask = 0x01u;
  CHECK(step(&controller, &line, 0u, &command) == SC_TRACK_OK);
  CHECK(command.left_permille == 0);
  CHECK(command.right_permille == 564);
  line.semantic_state = SC_LINE_RIGHT_RIGHT_ANGLE_CANDIDATE;
  line.black_mask = 0x0Eu;
  CHECK(step(&controller, &line, 1u, &command) == SC_TRACK_OK);
  CHECK(command.left_permille == 564);
  CHECK(command.right_permille == 0);
  return 0;
}

static int test_contiguous_pair_turns(void)
{
  sc_track_command_t command;
  sc_track_controller_t controller;
  sc_line_state_t line = line_state(SC_LINE_UNKNOWN_PATTERN);

  sc_track_controller_init(&controller);
  line.black_mask = 0x03u;
  CHECK(step(&controller, &line, 0u, &command) == SC_TRACK_OK);
  CHECK(command.left_permille == 106);
  CHECK(command.right_permille == 549);
  line.black_mask = 0x0Cu;
  CHECK(step(&controller, &line, 1u, &command) == SC_TRACK_OK);
  CHECK(command.left_permille == 549);
  CHECK(command.right_permille == 106);
  return 0;
}

static int test_lost_recovery(void)
{
  sc_track_command_t command;
  sc_track_controller_t controller;
  sc_line_state_t line = line_state(SC_LINE_LEFT_FINE_ADJUST_CANDIDATE);

  sc_track_controller_init(&controller);
  line.black_mask = 0x02u;
  CHECK(step(&controller, &line, 10u, &command) == SC_TRACK_OK);
  line.semantic_state = SC_LINE_LOST_CANDIDATE;
  line.black_mask = 0x00u;
  CHECK(step(&controller, &line, 20u, &command) == SC_TRACK_RECOVERING);
  CHECK(command.left_permille == 30);
  CHECK(command.right_permille == 473);
  CHECK(step(&controller, &line, 1019u, &command) == SC_TRACK_RECOVERING);
  CHECK(step(&controller, &line, 1020u, &command) == SC_TRACK_ERR_LOST);

  sc_track_controller_init(&controller);
  line.semantic_state = SC_LINE_RIGHT_FINE_ADJUST_CANDIDATE;
  line.black_mask = 0x04u;
  CHECK(step(&controller, &line, 100u, &command) == SC_TRACK_OK);
  line.semantic_state = SC_LINE_LOST_CANDIDATE;
  line.black_mask = 0x00u;
  CHECK(step(&controller, &line, 101u, &command) == SC_TRACK_RECOVERING);
  CHECK(command.left_permille == 473);
  CHECK(command.right_permille == 30);
  line.semantic_state = SC_LINE_STRAIGHT;
  line.black_mask = 0x06u;
  CHECK(step(&controller, &line, 102u, &command) == SC_TRACK_OK);
  CHECK(controller.recovering == 0u);
  return 0;
}

static int test_ambiguous_patterns(void)
{
  sc_track_command_t command;
  sc_track_controller_t controller;
  sc_line_state_t line = line_state(SC_LINE_UNKNOWN_PATTERN);

  sc_track_controller_init(&controller);
  line.black_mask = 0x05u;
  CHECK(step(&controller, &line, 0u, &command) == SC_TRACK_OK);
  CHECK(command.left_permille == 221);
  CHECK(command.right_permille == 526);
  line.black_mask = 0x0Au;
  CHECK(step(&controller, &line, 1u, &command) == SC_TRACK_OK);
  CHECK(command.left_permille == 526);
  CHECK(command.right_permille == 221);
  line.black_mask = 0x09u;
  CHECK(step(&controller, &line, 2u, &command) == SC_TRACK_OK);
  CHECK(command.left_permille == 396);
  CHECK(command.right_permille == 396);
  line.black_mask = 0x0Bu;
  CHECK(step(&controller, &line, 3u, &command) == SC_TRACK_OK);
  CHECK(command.left_permille == 221);
  CHECK(command.right_permille == 526);
  line.black_mask = 0x0Du;
  CHECK(step(&controller, &line, 4u, &command) == SC_TRACK_OK);
  CHECK(command.left_permille == 526);
  CHECK(command.right_permille == 221);

  line.sample.valid = 0u;
  CHECK(step(&controller, &line, 5u, &command) == SC_TRACK_ERR_INVALID_LINE);
  line.sample.valid = 1u;
  line.black_mask = 0x10u;
  CHECK(step(&controller, &line, 6u, &command) == SC_TRACK_ERR_INVALID_LINE);
  return 0;
}

static int test_arguments(void)
{
  sc_track_command_t command;
  sc_track_controller_t controller;
  sc_line_state_t line = line_state(SC_LINE_STRAIGHT);

  sc_track_controller_init(&controller);
  line.black_mask = 0x06u;
  CHECK(step(NULL, &line, 0u, &command) == SC_TRACK_ERR_ARGUMENT);
  CHECK(step(&controller, NULL, 0u, &command) == SC_TRACK_ERR_ARGUMENT);
  CHECK(step(&controller, &line, 0u, NULL) == SC_TRACK_ERR_ARGUMENT);
  return 0;
}

typedef struct {
  const char *name;
  int (*run)(void);
} test_case_t;

static const test_case_t s_tests[] = {
  {"straight_and_junction", test_straight_and_junction},
  {"fine_turns", test_fine_turns},
  {"large_turns", test_large_turns},
  {"contiguous_pair_turns", test_contiguous_pair_turns},
  {"lost_recovery", test_lost_recovery},
  {"ambiguous_patterns", test_ambiguous_patterns},
  {"arguments", test_arguments}
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
