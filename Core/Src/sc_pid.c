/**
  ******************************************************************************
  * @file    sc_pid.c
  * @brief   并联式 PID 实现。
  ******************************************************************************
  */

#include "sc_pid.h"

#include <limits.h>
#include <stddef.h>

#define SC_PID_GAIN_SCALE            1000
#define SC_PID_INTEGRAL_GAIN_SCALE   1000000

static int64_t sc_pid_saturating_add(int64_t value, int64_t increment)
{
  if ((increment > 0) && (value > (INT64_MAX - increment)))
  {
    return INT64_MAX;
  }
  if ((increment < 0) && (value < (INT64_MIN - increment)))
  {
    return INT64_MIN;
  }
  return value + increment;
}

sc_pid_status_t sc_pid_init(sc_pid_t *pid, const sc_pid_gains_t *gains)
{
  if ((pid == NULL) || (gains == NULL))
  {
    return SC_PID_ERR_ARGUMENT;
  }

  pid->gains = *gains;
  sc_pid_reset(pid);
  return SC_PID_OK;
}

void sc_pid_reset(sc_pid_t *pid)
{
  if (pid == NULL)
  {
    return;
  }

  pid->integral_error_ms = 0;
  pid->previous_measurement_mm_s = 0;
  pid->initialized = 0u;
}

sc_pid_status_t sc_pid_calculate(sc_pid_t *pid, int32_t target_mm_s,
                                 int32_t measurement_mm_s, uint32_t dt_ms,
                                 sc_pid_work_t *work)
{
  int64_t error;
  int64_t integral_increment;
  int64_t measurement_delta;

  if ((pid == NULL) || (work == NULL) || (dt_ms == 0u))
  {
    return SC_PID_ERR_ARGUMENT;
  }

  work->error_mm_s = 0;
  work->measurement_mm_s = measurement_mm_s;
  work->dt_ms = dt_ms;
  work->p_term_permille = 0;
  work->i_term_permille = 0;
  work->d_term_permille = 0;
  work->candidate_integral_error_ms = pid->integral_error_ms;
  work->unbounded_output_permille = 0;
  work->clear_history = 0u;

  if (target_mm_s == 0)
  {
    pid->integral_error_ms = 0;
    pid->initialized = 0u;
    work->clear_history = 1u;
  }

  error = (int64_t)target_mm_s - (int64_t)measurement_mm_s;
  work->error_mm_s =
    (error > INT32_MAX) ? INT32_MAX :
    ((error < INT32_MIN) ? INT32_MIN : (int32_t)error);
  work->p_term_permille =
    ((int64_t)pid->gains.kp_milli * error) / SC_PID_GAIN_SCALE;

  if (work->clear_history == 0u)
  {
    integral_increment = error * (int64_t)dt_ms;
    work->candidate_integral_error_ms =
      sc_pid_saturating_add(pid->integral_error_ms, integral_increment);
    work->i_term_permille =
      ((int64_t)pid->gains.ki_milli *
       work->candidate_integral_error_ms) /
      SC_PID_INTEGRAL_GAIN_SCALE;

    if (pid->initialized != 0u)
    {
      measurement_delta =
        (int64_t)measurement_mm_s - pid->previous_measurement_mm_s;
      work->d_term_permille =
        -((int64_t)pid->gains.kd_milli * measurement_delta) /
        (int64_t)dt_ms;
    }
  }

  work->unbounded_output_permille =
    sc_pid_saturating_add(
      sc_pid_saturating_add(work->p_term_permille,
                            work->i_term_permille),
      work->d_term_permille);
  return SC_PID_OK;
}

sc_pid_status_t sc_pid_commit(sc_pid_t *pid, const sc_pid_work_t *work,
                              int64_t requested_total_permille,
                              int16_t actual_output_permille,
                              int16_t output_min_permille,
                              int16_t output_max_permille)
{
  int64_t integral_delta;
  uint8_t block_positive;
  uint8_t block_negative;

  if ((pid == NULL) || (work == NULL) ||
      (output_min_permille > output_max_permille) ||
      (actual_output_permille < output_min_permille) ||
      (actual_output_permille > output_max_permille))
  {
    return SC_PID_ERR_ARGUMENT;
  }

  if (work->clear_history != 0u)
  {
    pid->integral_error_ms = 0;
    pid->previous_measurement_mm_s = 0;
    pid->initialized = 0u;
    return SC_PID_OK;
  }

  integral_delta =
    work->candidate_integral_error_ms - pid->integral_error_ms;
  block_positive =
    (uint8_t)((integral_delta > 0) &&
              ((requested_total_permille >
                (int64_t)output_max_permille) ||
               ((int64_t)actual_output_permille <
                requested_total_permille) ||
               ((actual_output_permille == output_max_permille) &&
                (work->error_mm_s > 0))));
  block_negative =
    (uint8_t)((integral_delta < 0) &&
              ((requested_total_permille <
                (int64_t)output_min_permille) ||
               ((int64_t)actual_output_permille >
                requested_total_permille) ||
               ((actual_output_permille == output_min_permille) &&
                (work->error_mm_s < 0))));

  if ((block_positive == 0u) && (block_negative == 0u))
  {
    pid->integral_error_ms = work->candidate_integral_error_ms;
  }

  pid->previous_measurement_mm_s = work->measurement_mm_s;
  pid->initialized = 1u;
  return SC_PID_OK;
}
