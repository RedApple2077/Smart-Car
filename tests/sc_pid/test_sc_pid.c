#include "sc_pid.h"

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

static sc_pid_gains_t gains(int32_t kp_milli, int32_t ki_milli,
                            int32_t kd_milli)
{
  sc_pid_gains_t value = {
    .kp_milli = kp_milli,
    .ki_milli = ki_milli,
    .kd_milli = kd_milli
  };
  return value;
}

static int commit_scaled(sc_pid_t *pid, const sc_pid_work_t *work,
                         int64_t requested, int16_t actual)
{
  return (int)sc_pid_commit(pid, work, requested, actual, -564, 564);
}

static int test_init_and_reset(void)
{
  sc_pid_gains_t pid_gains = gains(1000, 1000, 1000);
  sc_pid_t pid;
  sc_pid_work_t work;

  CHECK(sc_pid_init(NULL, &pid_gains) == SC_PID_ERR_ARGUMENT);
  CHECK(sc_pid_init(&pid, NULL) == SC_PID_ERR_ARGUMENT);
  pid.integral_error_ms = 123;
  pid.initialized = 1u;
  CHECK(sc_pid_init(&pid, &pid_gains) == SC_PID_OK);
  CHECK(pid.gains.kp_milli == 1000);
  CHECK(pid.integral_error_ms == 0);
  CHECK(pid.initialized == 0u);

  pid.integral_error_ms = 456;
  pid.initialized = 1u;
  sc_pid_reset(&pid);
  CHECK(pid.integral_error_ms == 0);
  CHECK(pid.previous_measurement_mm_s == 0);
  CHECK(pid.initialized == 0u);

  CHECK(sc_pid_calculate(&pid, 10, 0, 10u, &work) == SC_PID_OK);
  CHECK(work.error_mm_s == 10);
  CHECK(work.p_term_permille == 10);
  CHECK(work.i_term_permille == 0);
  CHECK(work.d_term_permille == 0);
  return 0;
}

static int test_derivative_on_measurement(void)
{
  sc_pid_gains_t pid_gains = gains(1000, 0, 1000);
  sc_pid_t pid;
  sc_pid_work_t work;

  CHECK(sc_pid_init(&pid, &pid_gains) == SC_PID_OK);
  CHECK(sc_pid_calculate(&pid, 100, 0, 10u, &work) == SC_PID_OK);
  CHECK(work.d_term_permille == 0);
  CHECK(work.unbounded_output_permille == 100);
  CHECK(commit_scaled(&pid, &work, 100, 100) == 0);

  CHECK(sc_pid_calculate(&pid, 200, 0, 10u, &work) == SC_PID_OK);
  CHECK(work.d_term_permille == 0);
  CHECK(work.p_term_permille == 200);
  CHECK(work.unbounded_output_permille == 200);
  CHECK(commit_scaled(&pid, &work, 200, 200) == 0);

  CHECK(sc_pid_calculate(&pid, 200, 10, 10u, &work) == SC_PID_OK);
  CHECK(work.p_term_permille == 190);
  CHECK(work.d_term_permille == -1000);
  CHECK(work.unbounded_output_permille == -810);
  CHECK(commit_scaled(&pid, &work, -810, -564) == 0);
  return 0;
}

static int test_negative_integral_accumulation(void)
{
  sc_pid_gains_t pid_gains = gains(1000, 1000, 0);
  sc_pid_t pid;
  sc_pid_work_t work;

  CHECK(sc_pid_init(&pid, &pid_gains) == SC_PID_OK);
  CHECK(sc_pid_calculate(&pid, -100, 0, 100u, &work) == SC_PID_OK);
  CHECK(work.error_mm_s == -100);
  CHECK(work.p_term_permille == -100);
  CHECK(work.i_term_permille == -10);
  CHECK(work.unbounded_output_permille == -110);
  CHECK(commit_scaled(&pid, &work, -110, -110) == 0);
  CHECK(pid.integral_error_ms == -10000);

  CHECK(sc_pid_calculate(&pid, -100, -50, 100u, &work) == SC_PID_OK);
  CHECK(work.error_mm_s == -50);
  CHECK(work.candidate_integral_error_ms == -15000);
  CHECK(work.p_term_permille == -50);
  CHECK(work.i_term_permille == -15);
  CHECK(work.unbounded_output_permille == -65);
  return 0;
}

static int test_conditional_antiwindup(void)
{
  sc_pid_gains_t pid_gains = gains(1000, 1000, 0);
  sc_pid_t pid;
  sc_pid_work_t work;

  CHECK(sc_pid_init(&pid, &pid_gains) == SC_PID_OK);
  CHECK(sc_pid_calculate(&pid, 100, 0, 100u, &work) == SC_PID_OK);
  CHECK(work.unbounded_output_permille == 110);
  CHECK(work.candidate_integral_error_ms == 10000);
  CHECK(sc_pid_commit(&pid, &work, 110, 50, 0, 100) == SC_PID_OK);
  CHECK(pid.integral_error_ms == 0);

  CHECK(sc_pid_calculate(&pid, 10, 0, 100u, &work) == SC_PID_OK);
  CHECK(work.unbounded_output_permille == 11);
  CHECK(sc_pid_commit(&pid, &work, 11, 5, 0, 100) == SC_PID_OK);
  CHECK(pid.integral_error_ms == 0);

  CHECK(sc_pid_calculate(&pid, 10, 0, 100u, &work) == SC_PID_OK);
  CHECK(sc_pid_commit(&pid, &work, 11, 11, 0, 100) == SC_PID_OK);
  CHECK(pid.integral_error_ms == 1000);
  CHECK(pid.previous_measurement_mm_s == 0);
  CHECK(pid.initialized == 1u);
  return 0;
}

static int test_zero_target_clears_history(void)
{
  sc_pid_gains_t pid_gains = gains(1000, 1000, 1000);
  sc_pid_t pid;
  sc_pid_work_t work;

  CHECK(sc_pid_init(&pid, &pid_gains) == SC_PID_OK);
  CHECK(sc_pid_calculate(&pid, 100, 0, 100u, &work) == SC_PID_OK);
  CHECK(sc_pid_commit(&pid, &work, 110, 110, -564, 564) == SC_PID_OK);
  CHECK(pid.integral_error_ms == 10000);
  CHECK(pid.initialized == 1u);

  CHECK(sc_pid_calculate(&pid, 0, 20, 10u, &work) == SC_PID_OK);
  CHECK(work.clear_history == 1u);
  CHECK(work.error_mm_s == -20);
  CHECK(work.p_term_permille == -20);
  CHECK(work.i_term_permille == 0);
  CHECK(work.d_term_permille == 0);
  CHECK(pid.integral_error_ms == 0);
  CHECK(pid.initialized == 0u);
  CHECK(sc_pid_commit(&pid, &work, -20, -20, -564, 564) == SC_PID_OK);
  CHECK(pid.integral_error_ms == 0);
  CHECK(pid.initialized == 0u);

  CHECK(sc_pid_calculate(&pid, 10, 20, 10u, &work) == SC_PID_OK);
  CHECK(work.p_term_permille == -10);
  CHECK(work.d_term_permille == 0);
  CHECK(work.unbounded_output_permille == -10);
  return 0;
}

static int test_argument_validation(void)
{
  sc_pid_gains_t pid_gains = gains(0, 0, 0);
  sc_pid_t pid;
  sc_pid_work_t work;

  CHECK(sc_pid_init(&pid, &pid_gains) == SC_PID_OK);
  CHECK(sc_pid_calculate(NULL, 1, 0, 10u, &work) ==
        SC_PID_ERR_ARGUMENT);
  CHECK(sc_pid_calculate(&pid, 1, 0, 0u, &work) ==
        SC_PID_ERR_ARGUMENT);
  CHECK(sc_pid_calculate(&pid, 1, 0, 10u, NULL) ==
        SC_PID_ERR_ARGUMENT);
  CHECK(sc_pid_calculate(&pid, 1, 0, 10u, &work) == SC_PID_OK);
  CHECK(sc_pid_commit(&pid, &work, 10, 10, 1, -1) ==
        SC_PID_ERR_ARGUMENT);
  CHECK(sc_pid_commit(&pid, &work, 10, 11, -10, 10) ==
        SC_PID_ERR_ARGUMENT);
  return 0;
}

typedef struct {
  const char *name;
  int (*run)(void);
} test_case_t;

static const test_case_t s_tests[] = {
  {"init_and_reset", test_init_and_reset},
  {"derivative_on_measurement", test_derivative_on_measurement},
  {"negative_integral_accumulation", test_negative_integral_accumulation},
  {"conditional_antiwindup", test_conditional_antiwindup},
  {"zero_target_clears_history", test_zero_target_clears_history},
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
