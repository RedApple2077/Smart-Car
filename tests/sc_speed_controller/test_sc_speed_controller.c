#include "sc_motor_defaults.h"
#include "sc_speed_controller.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression)                                                     \
  do                                                                          \
  {                                                                           \
    if (!(expression))                                                        \
    {                                                                         \
      fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__,     \
              #expression);                                                   \
      return 1;                                                               \
    }                                                                         \
  } while (0)

static sc_speed_gains_t gains(int32_t kp, int32_t ki, int32_t kd, int32_t ff)
{
  sc_speed_gains_t value = {
    .kp_milli = kp,
    .ki_milli = ki,
    .kd_milli = kd,
    .ff_milli = ff
  };
  return value;
}

static sc_speed_controller_config_t controller_config(
  sc_speed_gains_t left, sc_speed_gains_t right)
{
  sc_speed_controller_config_t config = {
    .left = left,
    .right = right
  };
  return config;
}

static sc_wheel_feedback_t feedback(uint32_t sample_time_ms,
                                    uint32_t sequence,
                                    int32_t left_speed_mm_s,
                                    int32_t right_speed_mm_s,
                                    uint8_t valid)
{
  sc_wheel_feedback_t value = {0};

  value.sample_time_ms = sample_time_ms;
  value.sequence = sequence;
  value.left_speed_mm_s = left_speed_mm_s;
  value.right_speed_mm_s = right_speed_mm_s;
  value.left_valid = valid;
  value.right_valid = valid;
  value.valid = valid;
  return value;
}

static int initialize_running(sc_speed_controller_t *controller,
                              const sc_speed_controller_config_t *config,
                              sc_speed_controller_output_t *output)
{
  CHECK(sc_speed_controller_init(controller, config) ==
        SC_SPEED_CONTROLLER_OK);
  CHECK(controller->mode == SC_SPEED_CONTROLLER_MODE_STOPPED);
  CHECK(sc_speed_controller_set_mode(
          controller, SC_SPEED_CONTROLLER_MODE_RUNNING, output) ==
        SC_SPEED_CONTROLLER_OK);
  CHECK(output->left_permille == 0);
  CHECK(output->right_permille == 0);
  return 0;
}

static int test_target_and_output_ramp_limit(void)
{
  sc_speed_controller_config_t config =
    controller_config(gains(0, 0, 0, 10000), gains(0, 0, 0, 10000));
  sc_speed_controller_t controller;
  sc_speed_controller_output_t output;
  sc_wheel_feedback_t feedback_value;

  CHECK(initialize_running(&controller, &config, &output) == 0);
  feedback_value = feedback(100u, 1u, 0, 0, 1u);
  CHECK(sc_speed_controller_step(
          &controller, 1000, 1000, &feedback_value, 100u, &output) ==
        SC_SPEED_CONTROLLER_WAIT);
  CHECK(output.left_permille == 0);
  CHECK(output.right_permille == 0);

  feedback_value = feedback(150u, 2u, 0, 0, 1u);
  CHECK(sc_speed_controller_step(
          &controller, 1000, 1000, &feedback_value, 150u, &output) ==
        SC_SPEED_CONTROLLER_OK);
  CHECK(output.left_target_ramped_mm_s == 100);
  CHECK(output.right_target_ramped_mm_s == 100);
  CHECK(output.left_requested_permille == 1000);
  CHECK(output.right_requested_permille == 1000);
  CHECK(output.left_permille == 400);
  CHECK(output.right_permille == 400);

  feedback_value = feedback(200u, 3u, 0, 0, 1u);
  CHECK(sc_speed_controller_step(
          &controller, 1000, 1000, &feedback_value, 200u, &output) ==
        SC_SPEED_CONTROLLER_OK);
  CHECK(output.left_target_ramped_mm_s == 200);
  CHECK(output.left_permille == 564);
  CHECK(output.right_permille == 564);

  feedback_value = feedback(250u, 4u, 0, 0, 1u);
  CHECK(sc_speed_controller_step(
          &controller, 1000, 1000, &feedback_value, 250u, &output) ==
        SC_SPEED_CONTROLLER_OK);
  CHECK(output.left_permille == 564);
  CHECK(output.right_permille == 564);
  return 0;
}

static int test_different_left_right_gains(void)
{
  sc_speed_controller_config_t config =
    controller_config(gains(1000, 0, 0, 0), gains(500, 0, 0, 0));
  sc_speed_controller_t controller;
  sc_speed_controller_output_t output;
  sc_wheel_feedback_t feedback_value;

  CHECK(initialize_running(&controller, &config, &output) == 0);
  feedback_value = feedback(100u, 10u, 0, 0, 1u);
  CHECK(sc_speed_controller_step(
          &controller, 100, 100, &feedback_value, 100u, &output) ==
        SC_SPEED_CONTROLLER_WAIT);
  feedback_value = feedback(150u, 11u, 0, 0, 1u);
  CHECK(sc_speed_controller_step(
          &controller, 100, 100, &feedback_value, 150u, &output) ==
        SC_SPEED_CONTROLLER_OK);
  CHECK(output.left_target_ramped_mm_s == 100);
  CHECK(output.right_target_ramped_mm_s == 100);
  CHECK(output.left_requested_permille == 100);
  CHECK(output.right_requested_permille == 50);
  CHECK(output.left_permille == 100);
  CHECK(output.right_permille == 50);
  return 0;
}

static int test_negative_target(void)
{
  sc_speed_controller_config_t config =
    controller_config(gains(0, 0, 0, 10000), gains(0, 0, 0, 10000));
  sc_speed_controller_t controller;
  sc_speed_controller_output_t output;
  sc_wheel_feedback_t feedback_value;

  CHECK(initialize_running(&controller, &config, &output) == 0);
  feedback_value = feedback(100u, 20u, 0, 0, 1u);
  CHECK(sc_speed_controller_step(
          &controller, -1000, -1000, &feedback_value, 100u, &output) ==
        SC_SPEED_CONTROLLER_WAIT);
  feedback_value = feedback(150u, 21u, 0, 0, 1u);
  CHECK(sc_speed_controller_step(
          &controller, -1000, -1000, &feedback_value, 150u, &output) ==
        SC_SPEED_CONTROLLER_OK);
  CHECK(output.left_target_ramped_mm_s == -100);
  CHECK(output.right_target_ramped_mm_s == -100);
  CHECK(output.left_requested_permille == -1000);
  CHECK(output.right_requested_permille == -1000);
  CHECK(output.left_permille == -400);
  CHECK(output.right_permille == -400);

  feedback_value = feedback(200u, 22u, 0, 0, 1u);
  CHECK(sc_speed_controller_step(
          &controller, -1000, -1000, &feedback_value, 200u, &output) ==
        SC_SPEED_CONTROLLER_OK);
  CHECK(output.left_target_ramped_mm_s == -200);
  CHECK(output.left_permille == -564);
  CHECK(output.right_permille == -564);
  return 0;
}

static int test_zero_target_and_stop(void)
{
  sc_speed_controller_config_t config =
    controller_config(gains(0, 0, 0, 10000), gains(0, 0, 0, 10000));
  sc_speed_controller_t controller;
  sc_speed_controller_output_t output;
  sc_wheel_feedback_t feedback_value;

  CHECK(initialize_running(&controller, &config, &output) == 0);
  feedback_value = feedback(100u, 30u, 0, 0, 1u);
  CHECK(sc_speed_controller_step(
          &controller, 1000, 1000, &feedback_value, 100u, &output) ==
        SC_SPEED_CONTROLLER_WAIT);
  feedback_value = feedback(150u, 31u, 0, 0, 1u);
  CHECK(sc_speed_controller_step(
          &controller, 1000, 1000, &feedback_value, 150u, &output) ==
        SC_SPEED_CONTROLLER_OK);
  CHECK(output.left_permille == 400);
  CHECK(output.right_permille == 400);

  feedback_value = feedback(200u, 32u, 0, 0, 1u);
  CHECK(sc_speed_controller_step(
          &controller, 1000, 1000, &feedback_value, 200u, &output) ==
        SC_SPEED_CONTROLLER_OK);
  CHECK(output.left_target_ramped_mm_s == 200);
  CHECK(output.left_permille == 564);
  CHECK(output.right_permille == 564);

  feedback_value = feedback(250u, 33u, 0, 0, 1u);
  CHECK(sc_speed_controller_step(
          &controller, 0, 1000, &feedback_value, 250u, &output) ==
        SC_SPEED_CONTROLLER_OK);
  CHECK(output.left_target_ramped_mm_s == 0);
  CHECK(output.left_permille == 0);
  CHECK(controller.left.pid.initialized == 0u);
  CHECK(controller.left.pid.integral_error_ms == 0);
  CHECK(output.right_target_ramped_mm_s == 300);
  CHECK(output.right_permille == 564);

  feedback_value = feedback(300u, 34u, 0, 0, 1u);
  CHECK(sc_speed_controller_step(
          &controller, 0, 0, &feedback_value, 300u, &output) ==
        SC_SPEED_CONTROLLER_OK);
  CHECK(output.left_target_ramped_mm_s == 0);
  CHECK(output.right_target_ramped_mm_s == 0);
  CHECK(output.left_permille == 0);
  CHECK(output.right_permille == 0);
  CHECK(controller.right.pid.initialized == 0u);
  CHECK(controller.right.pid.integral_error_ms == 0);

  feedback_value = feedback(350u, 35u, 0, 0, 1u);
  CHECK(sc_speed_controller_step(
          &controller, 1000, 1000, &feedback_value, 350u, &output) ==
        SC_SPEED_CONTROLLER_OK);
  CHECK(output.left_target_ramped_mm_s == 100);
  CHECK(output.left_permille == 400);
  CHECK(output.right_permille == 400);

  CHECK(sc_speed_controller_stop(&controller, &output) ==
        SC_SPEED_CONTROLLER_STOPPED);
  CHECK(output.left_permille == 0);
  CHECK(output.right_permille == 0);
  CHECK(controller.mode == SC_SPEED_CONTROLLER_MODE_STOPPED);
  CHECK(sc_speed_controller_reset(&controller, &output) ==
        SC_SPEED_CONTROLLER_STOPPED);
  CHECK(output.left_permille == 0);
  CHECK(output.right_permille == 0);
  return 0;
}

static int test_first_feedback_wait(void)
{
  sc_speed_controller_config_t config =
    controller_config(gains(1000, 1000, 1000, 1),
                      gains(1000, 1000, 1000, 1));
  sc_speed_controller_t controller;
  sc_speed_controller_output_t output;
  sc_wheel_feedback_t feedback_value;

  CHECK(initialize_running(&controller, &config, &output) == 0);
  feedback_value = feedback(500u, 1u, 999, 999, 1u);
  CHECK(sc_speed_controller_step(
          &controller, 1000, 1000, &feedback_value, 500u, &output) ==
        SC_SPEED_CONTROLLER_WAIT);
  CHECK(output.left_permille == 0);
  CHECK(output.right_permille == 0);
  CHECK(output.left_target_ramped_mm_s == 0);
  CHECK(output.right_target_ramped_mm_s == 0);
  CHECK(controller.left.pid.integral_error_ms == 0);
  CHECK(controller.right.pid.integral_error_ms == 0);
  return 0;
}

static int test_duplicate_feedback_is_bumpless(void)
{
  sc_speed_controller_config_t config =
    controller_config(gains(0, 0, 0, 10000), gains(0, 0, 0, 10000));
  sc_speed_controller_t controller;
  sc_speed_controller_output_t output;
  sc_wheel_feedback_t feedback_value;

  CHECK(initialize_running(&controller, &config, &output) == 0);
  feedback_value = feedback(100u, 40u, 0, 0, 1u);
  CHECK(sc_speed_controller_step(
          &controller, 1000, 1000, &feedback_value, 100u, &output) ==
        SC_SPEED_CONTROLLER_WAIT);
  feedback_value = feedback(150u, 41u, 0, 0, 1u);
  CHECK(sc_speed_controller_step(
          &controller, 1000, 1000, &feedback_value, 150u, &output) ==
        SC_SPEED_CONTROLLER_OK);
  CHECK(output.left_permille == 400);
  CHECK(output.right_permille == 400);

  feedback_value = feedback(150u, 41u, 0, 0, 1u);
  CHECK(sc_speed_controller_step(
          &controller, 1000, 1000, &feedback_value, 150u, &output) ==
        SC_SPEED_CONTROLLER_WAIT);
  CHECK(output.left_permille == 400);
  CHECK(output.right_permille == 400);

  CHECK(sc_speed_controller_step(
          &controller, 0, 1000, &feedback_value, 150u, &output) ==
        SC_SPEED_CONTROLLER_WAIT);
  CHECK(output.left_permille == 0);
  CHECK(output.right_permille == 400);
  CHECK(controller.left.pid.initialized == 0u);
  return 0;
}

static int test_invalid_or_stale_feedback(void)
{
  sc_speed_controller_config_t config =
    controller_config(gains(0, 0, 0, 10000), gains(0, 0, 0, 10000));
  sc_speed_controller_t controller;
  sc_speed_controller_output_t output;
  sc_wheel_feedback_t feedback_value;

  CHECK(initialize_running(&controller, &config, &output) == 0);
  feedback_value = feedback(100u, 50u, 0, 0, 1u);
  CHECK(sc_speed_controller_step(
          &controller, 1000, 1000, &feedback_value, 100u, &output) ==
        SC_SPEED_CONTROLLER_WAIT);
  feedback_value = feedback(150u, 51u, 0, 0, 1u);
  CHECK(sc_speed_controller_step(
          &controller, 1000, 1000, &feedback_value, 150u, &output) ==
        SC_SPEED_CONTROLLER_OK);
  CHECK(output.left_permille == 400);

  feedback_value = feedback(200u, 52u, 0, 0, 0u);
  CHECK(sc_speed_controller_step(
          &controller, 1000, 1000, &feedback_value, 200u, &output) ==
        SC_SPEED_CONTROLLER_ERR_FEEDBACK);
  CHECK(output.left_permille == 0);
  CHECK(output.right_permille == 0);
  CHECK(controller.mode == SC_SPEED_CONTROLLER_MODE_STOPPED);

  CHECK(sc_speed_controller_set_mode(
          &controller, SC_SPEED_CONTROLLER_MODE_RUNNING, &output) ==
        SC_SPEED_CONTROLLER_OK);
  feedback_value = feedback(300u, 53u, 0, 0, 1u);
  CHECK(sc_speed_controller_step(
          &controller, 1000, 1000, &feedback_value, 300u, &output) ==
        SC_SPEED_CONTROLLER_WAIT);
  feedback_value = feedback(300u, 54u, 0, 0, 1u);
  CHECK(sc_speed_controller_step(
          &controller, 1000, 1000, &feedback_value, 331u, &output) ==
        SC_SPEED_CONTROLLER_ERR_FEEDBACK);
  CHECK(output.left_permille == 0);
  CHECK(output.right_permille == 0);
  return 0;
}

static int test_defaults_match_calibrated_bench_config(void)
{
  _Static_assert(SC_MOTOR_CALIBRATION_VALID == 1u,
                 "user-waived calibration must be marked valid");
  _Static_assert(SC_MOTOR_TUNING_VALID == 1u,
                 "1061-CPR bench tuning must be marked valid");
  _Static_assert(SC_MOTOR_LEFT_ENCODER_SIGN == -1,
                 "left encoder nominal sign changed");
  _Static_assert(SC_MOTOR_RIGHT_ENCODER_SIGN == 1,
                 "right encoder nominal sign changed");
  _Static_assert(SC_MOTOR_LEFT_COUNTS_PER_REV == 1061u,
                 "left encoder measured CPR changed");
  _Static_assert(SC_MOTOR_RIGHT_COUNTS_PER_REV == 1061u,
                 "right encoder measured CPR changed");
  _Static_assert(SC_MOTOR_LEFT_WHEEL_DIAMETER_UM == 48600,
                 "left wheel caliper diameter changed");
  _Static_assert(SC_MOTOR_RIGHT_WHEEL_DIAMETER_UM == 48600,
                 "right wheel caliper diameter changed");
  _Static_assert(SC_MOTOR_LEFT_MOTOR_SIGN == 1,
                 "left motor nominal sign changed");
  _Static_assert(SC_MOTOR_RIGHT_MOTOR_SIGN == 1,
                 "right motor nominal sign changed");
  _Static_assert(SC_MOTOR_CRUISE_SPEED_MM_S == 500,
                 "cruise speed does not match recalibration target");
  _Static_assert(SC_MOTOR_LEFT_FF_MILLI == 810,
                 "left feedforward does not match identified value");
  _Static_assert(SC_MOTOR_LEFT_KP_MILLI == 100,
                 "left Kp does not match validated value");
  _Static_assert(SC_MOTOR_LEFT_KI_MILLI == 750,
                 "left Ki does not match validated value");
  _Static_assert(SC_MOTOR_LEFT_KD_MILLI == 0,
                 "left Kd does not match validated value");
  _Static_assert(SC_MOTOR_RIGHT_FF_MILLI == 815,
                 "right feedforward does not match identified value");
  _Static_assert(SC_MOTOR_RIGHT_KP_MILLI == 100,
                 "right Kp does not match validated value");
  _Static_assert(SC_MOTOR_RIGHT_KI_MILLI == 750,
                 "right Ki does not match validated value");
  _Static_assert(SC_MOTOR_RIGHT_KD_MILLI == 0,
                 "right Kd does not match validated value");
  return 0;
}

static int test_pid_update_requires_stopped_and_bounds(void)
{
  sc_speed_controller_config_t config =
    controller_config(gains(100, 10, 1, 1234),
                      gains(200, 20, 2, 5678));
  sc_speed_controller_t controller;
  sc_speed_controller_output_t output;

  CHECK(sc_speed_controller_init(&controller, &config) ==
        SC_SPEED_CONTROLLER_OK);
  CHECK(sc_speed_controller_update_pid(
          &controller, SC_SPEED_CONTROLLER_SIDE_LEFT,
          300, 30, 3) == SC_SPEED_CONTROLLER_OK);
  CHECK(controller.config.left.kp_milli == 300);
  CHECK(controller.config.left.ki_milli == 30);
  CHECK(controller.config.left.kd_milli == 3);
  CHECK(controller.config.left.ff_milli == 1234);
  CHECK(controller.config.right.kp_milli == 200);
  CHECK(controller.config.right.ki_milli == 20);
  CHECK(controller.config.right.kd_milli == 2);
  CHECK(controller.config.right.ff_milli == 5678);

  CHECK(sc_speed_controller_update_pid(
          &controller, SC_SPEED_CONTROLLER_SIDE_BOTH,
          -10, -20, -30) == SC_SPEED_CONTROLLER_OK);
  CHECK(controller.config.left.kp_milli == -10);
  CHECK(controller.config.left.ki_milli == -20);
  CHECK(controller.config.left.kd_milli == -30);
  CHECK(controller.config.left.ff_milli == 1234);
  CHECK(controller.config.right.kp_milli == -10);
  CHECK(controller.config.right.ki_milli == -20);
  CHECK(controller.config.right.kd_milli == -30);
  CHECK(controller.config.right.ff_milli == 5678);

  CHECK(sc_speed_controller_update_pid(
          &controller, SC_SPEED_CONTROLLER_SIDE_LEFT,
          SC_SPEED_GAIN_LIMIT_MILLI + 1, 0, 0) ==
        SC_SPEED_CONTROLLER_ERR_CONFIG);
  CHECK(sc_speed_controller_update_pid(
          &controller, 0u, 1, 1, 1) ==
        SC_SPEED_CONTROLLER_ERR_ARGUMENT);
  CHECK(sc_speed_controller_update_pid(
          &controller, 0x80u, 1, 1, 1) ==
        SC_SPEED_CONTROLLER_ERR_ARGUMENT);
  controller.config.left.ff_milli = SC_SPEED_GAIN_LIMIT_MILLI + 1;
  CHECK(sc_speed_controller_update_pid(
          &controller, SC_SPEED_CONTROLLER_SIDE_LEFT,
          1, 2, 3) == SC_SPEED_CONTROLLER_ERR_CONFIG);
  controller.config.left.ff_milli = 1234;

  CHECK(sc_speed_controller_set_mode(
          &controller, SC_SPEED_CONTROLLER_MODE_RUNNING, &output) ==
        SC_SPEED_CONTROLLER_OK);
  CHECK(sc_speed_controller_update_pid(
          &controller, SC_SPEED_CONTROLLER_SIDE_LEFT,
          1, 2, 3) == SC_SPEED_CONTROLLER_ERR_CONFIG);
  CHECK(controller.config.left.kp_milli == -10);
  return 0;
}

static int test_argument_validation(void)
{
  sc_speed_controller_config_t config =
    controller_config(gains(0, 0, 0, 0), gains(0, 0, 0, 0));
  sc_speed_controller_t controller;
  sc_speed_controller_output_t output;
  sc_wheel_feedback_t feedback_value;

  CHECK(sc_speed_controller_init(NULL, &config) ==
        SC_SPEED_CONTROLLER_ERR_ARGUMENT);
  CHECK(sc_speed_controller_init(&controller, NULL) ==
        SC_SPEED_CONTROLLER_ERR_ARGUMENT);
  CHECK(sc_speed_controller_init(&controller, &config) ==
        SC_SPEED_CONTROLLER_OK);
  CHECK(sc_speed_controller_set_mode(
          &controller, (sc_speed_controller_mode_t)9, &output) ==
        SC_SPEED_CONTROLLER_ERR_ARGUMENT);
  feedback_value = feedback(100u, 1u, 0, 0, 1u);
  CHECK(sc_speed_controller_step(
          NULL, 0, 0, &feedback_value, 100u, &output) ==
        SC_SPEED_CONTROLLER_ERR_ARGUMENT);
  CHECK(sc_speed_controller_step(
          &controller, 0, 0, NULL, 100u, &output) ==
        SC_SPEED_CONTROLLER_ERR_ARGUMENT);
  CHECK(sc_speed_controller_step(
          &controller, 0, 0, &feedback_value, 100u, NULL) ==
        SC_SPEED_CONTROLLER_ERR_ARGUMENT);
  CHECK(sc_speed_controller_step(
          &controller, 0, 0, &feedback_value, 100u, &output) ==
        SC_SPEED_CONTROLLER_STOPPED);
  return 0;
}

typedef struct {
  const char *name;
  int (*run)(void);
} test_case_t;

static const test_case_t s_tests[] = {
  {"target_and_output_ramp_limit", test_target_and_output_ramp_limit},
  {"different_left_right_gains", test_different_left_right_gains},
  {"negative_target", test_negative_target},
  {"zero_target_and_stop", test_zero_target_and_stop},
  {"first_feedback_wait", test_first_feedback_wait},
  {"duplicate_feedback_is_bumpless", test_duplicate_feedback_is_bumpless},
  {"invalid_or_stale_feedback", test_invalid_or_stale_feedback},
  {"defaults_match_calibrated_bench_config",
   test_defaults_match_calibrated_bench_config},
  {"pid_update_requires_stopped_and_bounds",
   test_pid_update_requires_stopped_and_bounds},
  {"argument_validation", test_argument_validation}
};

int main(int argc, char **argv)
{
  size_t index;

  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <test-case>\n", argv[0]);
    return 2;
  }
  for (index = 0u; index < (sizeof(s_tests) / sizeof(s_tests[0])); index++)
  {
    if (strcmp(argv[1], s_tests[index].name) == 0)
    {
      return s_tests[index].run();
    }
  }
  fprintf(stderr, "unknown test case: %s\n", argv[1]);
  return 2;
}
