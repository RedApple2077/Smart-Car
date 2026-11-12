/**
  ******************************************************************************
  * @file    sc_speed_controller.c
  * @brief   双轮速度控制实现。
  ******************************************************************************
  */

#include "sc_speed_controller.h"

#include <limits.h>
#include <stddef.h>

#define SC_SPEED_GAIN_SCALE 1000

static int16_t sc_speed_clamp_int16(int64_t value, int16_t minimum,
                                    int16_t maximum)
{
  if (value < minimum)
  {
    return minimum;
  }
  if (value > maximum)
  {
    return maximum;
  }
  return (int16_t)value;
}

static int32_t sc_speed_move_toward_i32(int32_t current, int32_t target,
                                        uint32_t maximum_step)
{
  if (target > current)
  {
    int64_t next = (int64_t)current + maximum_step;
    return (next >= target) ? target : (int32_t)next;
  }
  if (target < current)
  {
    int64_t next = (int64_t)current - maximum_step;
    return (next <= target) ? target : (int32_t)next;
  }
  return current;
}

static int16_t sc_speed_move_toward_i16(int16_t current, int16_t target,
                                        uint32_t maximum_step)
{
  if (target > current)
  {
    int64_t next = (int64_t)current + (int64_t)maximum_step;
    return (next >= target) ? target : (int16_t)next;
  }
  if (target < current)
  {
    int64_t next = (int64_t)current - (int64_t)maximum_step;
    return (next <= target) ? target : (int16_t)next;
  }
  return current;
}

static uint32_t sc_speed_rate_step(uint32_t rate_per_second,
                                   uint32_t dt_ms)
{
  uint64_t step =
    ((uint64_t)rate_per_second * dt_ms) / 1000u;
  return (step > UINT32_MAX) ? UINT32_MAX : (uint32_t)step;
}

static void sc_speed_set_output(sc_speed_controller_output_t *output,
                                const sc_speed_controller_t *controller,
                                uint8_t feedback_valid)
{
  output->left_permille = controller->left.applied_permille;
  output->right_permille = controller->right.applied_permille;
  output->left_target_ramped_mm_s =
    controller->left.target_ramped_mm_s;
  output->right_target_ramped_mm_s =
    controller->right.target_ramped_mm_s;
  output->left_requested_permille = controller->left.requested_permille;
  output->right_requested_permille = controller->right.requested_permille;
  output->feedback_valid = feedback_valid;
}

static void sc_speed_reset_runtime(sc_speed_controller_t *controller)
{
  controller->left.target_ramped_mm_s = 0;
  controller->right.target_ramped_mm_s = 0;
  controller->left.applied_permille = 0;
  controller->right.applied_permille = 0;
  controller->left.requested_permille = 0;
  controller->right.requested_permille = 0;
  sc_pid_reset(&controller->left.pid);
  sc_pid_reset(&controller->right.pid);
  controller->last_feedback_time_ms = 0u;
  controller->last_feedback_sequence = 0u;
  controller->feedback_initialized = 0u;
}

static void sc_speed_force_zero_side(sc_speed_controller_side_t *side)
{
  side->target_ramped_mm_s = 0;
  side->applied_permille = 0;
  side->requested_permille = 0;
  sc_pid_reset(&side->pid);
}

static sc_pid_status_t sc_speed_init_side_pid(
  sc_speed_controller_side_t *side, const sc_speed_gains_t *gains)
{
  sc_pid_gains_t pid_gains;

  pid_gains.kp_milli = gains->kp_milli;
  pid_gains.ki_milli = gains->ki_milli;
  pid_gains.kd_milli = gains->kd_milli;
  return sc_pid_init(&side->pid, &pid_gains);
}

static uint8_t sc_speed_gain_valid(int32_t gain_milli)
{
  return (uint8_t)((gain_milli >= -SC_SPEED_GAIN_LIMIT_MILLI) &&
                   (gain_milli <= SC_SPEED_GAIN_LIMIT_MILLI));
}

static uint8_t sc_speed_gains_valid(const sc_speed_gains_t *gains)
{
  return (uint8_t)(
    (gains != NULL) &&
    sc_speed_gain_valid(gains->kp_milli) &&
    sc_speed_gain_valid(gains->ki_milli) &&
    sc_speed_gain_valid(gains->kd_milli) &&
    sc_speed_gain_valid(gains->ff_milli));
}

static sc_speed_controller_status_t sc_speed_apply_side(
  sc_speed_controller_side_t *side, const sc_speed_gains_t *gains,
  int32_t requested_target_mm_s, int32_t measurement_mm_s,
  uint32_t dt_ms)
{
  sc_pid_work_t pid_work;
  int64_t feedforward_permille;
  int64_t requested_permille;
  int16_t limited_request_permille;
  uint32_t output_step;
  sc_pid_status_t pid_status;

  side->target_ramped_mm_s = sc_speed_move_toward_i32(
    side->target_ramped_mm_s, requested_target_mm_s,
    sc_speed_rate_step(SC_SPEED_TARGET_RAMP_MM_S2, dt_ms));

  feedforward_permille =
    ((int64_t)gains->ff_milli * side->target_ramped_mm_s) /
    SC_SPEED_GAIN_SCALE;
  pid_status = sc_pid_calculate(
    &side->pid, side->target_ramped_mm_s, measurement_mm_s, dt_ms,
    &pid_work);
  if (pid_status != SC_PID_OK)
  {
    return SC_SPEED_CONTROLLER_ERR_CONFIG;
  }

  requested_permille =
    feedforward_permille + pid_work.unbounded_output_permille;
  side->requested_permille = requested_permille;
  limited_request_permille = sc_speed_clamp_int16(
    requested_permille, -SC_SPEED_OUTPUT_LIMIT_PERMILLE,
    SC_SPEED_OUTPUT_LIMIT_PERMILLE);
  output_step =
    sc_speed_rate_step(SC_SPEED_OUTPUT_RAMP_PERMILLE_S, dt_ms);
  side->applied_permille = sc_speed_move_toward_i16(
    side->applied_permille, limited_request_permille, output_step);
  side->applied_permille = sc_speed_clamp_int16(
    side->applied_permille, -SC_SPEED_OUTPUT_LIMIT_PERMILLE,
    SC_SPEED_OUTPUT_LIMIT_PERMILLE);

  pid_status = sc_pid_commit(
    &side->pid, &pid_work, requested_permille, side->applied_permille,
    -SC_SPEED_OUTPUT_LIMIT_PERMILLE, SC_SPEED_OUTPUT_LIMIT_PERMILLE);
  return (pid_status == SC_PID_OK) ? SC_SPEED_CONTROLLER_OK :
    SC_SPEED_CONTROLLER_ERR_CONFIG;
}

sc_speed_controller_status_t sc_speed_controller_init(
  sc_speed_controller_t *controller,
  const sc_speed_controller_config_t *config)
{
  if ((controller == NULL) || (config == NULL))
  {
    return SC_SPEED_CONTROLLER_ERR_ARGUMENT;
  }

  controller->config = *config;
  controller->mode = SC_SPEED_CONTROLLER_MODE_STOPPED;
  controller->initialized = 1u;
  sc_speed_reset_runtime(controller);
  if ((sc_speed_init_side_pid(&controller->left, &config->left) != SC_PID_OK) ||
      (sc_speed_init_side_pid(&controller->right, &config->right) != SC_PID_OK))
  {
    controller->initialized = 0u;
    return SC_SPEED_CONTROLLER_ERR_CONFIG;
  }
  return SC_SPEED_CONTROLLER_OK;
}

sc_speed_controller_status_t sc_speed_controller_set_mode(
  sc_speed_controller_t *controller, sc_speed_controller_mode_t mode,
  sc_speed_controller_output_t *output)
{
  if ((controller == NULL) || (output == NULL) ||
      ((mode != SC_SPEED_CONTROLLER_MODE_STOPPED) &&
       (mode != SC_SPEED_CONTROLLER_MODE_RUNNING)))
  {
    return SC_SPEED_CONTROLLER_ERR_ARGUMENT;
  }
  if (controller->initialized == 0u)
  {
    return SC_SPEED_CONTROLLER_ERR_NOT_READY;
  }

  if (mode != controller->mode)
  {
    sc_speed_reset_runtime(controller);
    controller->mode = mode;
  }
  sc_speed_set_output(output, controller, 0u);
  return (mode == SC_SPEED_CONTROLLER_MODE_RUNNING) ?
    SC_SPEED_CONTROLLER_OK : SC_SPEED_CONTROLLER_STOPPED;
}

sc_speed_controller_status_t sc_speed_controller_reset(
  sc_speed_controller_t *controller,
  sc_speed_controller_output_t *output)
{
  return sc_speed_controller_set_mode(
    controller, SC_SPEED_CONTROLLER_MODE_STOPPED, output);
}

sc_speed_controller_status_t sc_speed_controller_stop(
  sc_speed_controller_t *controller,
  sc_speed_controller_output_t *output)
{
  return sc_speed_controller_reset(controller, output);
}

sc_speed_controller_status_t sc_speed_controller_update_pid(
  sc_speed_controller_t *controller, uint8_t side_mask,
  int32_t kp_milli, int32_t ki_milli, int32_t kd_milli)
{
  sc_speed_controller_config_t updated_config;
  sc_pid_t updated_left_pid;
  sc_pid_t updated_right_pid;
  sc_pid_gains_t pid_gains;

  if ((controller == NULL) || (side_mask == 0u) ||
      ((side_mask & (uint8_t)~SC_SPEED_CONTROLLER_SIDE_BOTH) != 0u))
  {
    return SC_SPEED_CONTROLLER_ERR_ARGUMENT;
  }
  if (controller->initialized == 0u)
  {
    return SC_SPEED_CONTROLLER_ERR_NOT_READY;
  }
  if ((controller->mode != SC_SPEED_CONTROLLER_MODE_STOPPED) ||
      (controller->left.applied_permille != 0) ||
      (controller->right.applied_permille != 0) ||
      (controller->left.target_ramped_mm_s != 0) ||
      (controller->right.target_ramped_mm_s != 0))
  {
    return SC_SPEED_CONTROLLER_ERR_CONFIG;
  }
  if ((!sc_speed_gain_valid(kp_milli)) ||
      (!sc_speed_gain_valid(ki_milli)) ||
      (!sc_speed_gain_valid(kd_milli)))
  {
    return SC_SPEED_CONTROLLER_ERR_CONFIG;
  }

  updated_config = controller->config;
  if ((side_mask & SC_SPEED_CONTROLLER_SIDE_LEFT) != 0u)
  {
    updated_config.left.kp_milli = kp_milli;
    updated_config.left.ki_milli = ki_milli;
    updated_config.left.kd_milli = kd_milli;
  }
  if ((side_mask & SC_SPEED_CONTROLLER_SIDE_RIGHT) != 0u)
  {
    updated_config.right.kp_milli = kp_milli;
    updated_config.right.ki_milli = ki_milli;
    updated_config.right.kd_milli = kd_milli;
  }
  if ((!sc_speed_gains_valid(&updated_config.left)) ||
      (!sc_speed_gains_valid(&updated_config.right)))
  {
    return SC_SPEED_CONTROLLER_ERR_CONFIG;
  }

  pid_gains.kp_milli = updated_config.left.kp_milli;
  pid_gains.ki_milli = updated_config.left.ki_milli;
  pid_gains.kd_milli = updated_config.left.kd_milli;
  if (sc_pid_init(&updated_left_pid, &pid_gains) != SC_PID_OK)
  {
    return SC_SPEED_CONTROLLER_ERR_CONFIG;
  }
  pid_gains.kp_milli = updated_config.right.kp_milli;
  pid_gains.ki_milli = updated_config.right.ki_milli;
  pid_gains.kd_milli = updated_config.right.kd_milli;
  if (sc_pid_init(&updated_right_pid, &pid_gains) != SC_PID_OK)
  {
    return SC_SPEED_CONTROLLER_ERR_CONFIG;
  }

  controller->config = updated_config;
  controller->left.pid = updated_left_pid;
  controller->right.pid = updated_right_pid;
  return SC_SPEED_CONTROLLER_OK;
}

sc_speed_controller_status_t sc_speed_controller_step(
  sc_speed_controller_t *controller, int32_t left_target_mm_s,
  int32_t right_target_mm_s, const sc_wheel_feedback_t *feedback,
  uint32_t now_ms, sc_speed_controller_output_t *output)
{
  uint32_t dt_ms;
  sc_speed_controller_status_t left_status;
  sc_speed_controller_status_t right_status;

  if ((controller == NULL) || (feedback == NULL) || (output == NULL))
  {
    return SC_SPEED_CONTROLLER_ERR_ARGUMENT;
  }
  if (controller->initialized == 0u)
  {
    return SC_SPEED_CONTROLLER_ERR_NOT_READY;
  }
  if (controller->mode != SC_SPEED_CONTROLLER_MODE_RUNNING)
  {
    sc_speed_set_output(output, controller, feedback->valid);
    return SC_SPEED_CONTROLLER_STOPPED;
  }
  if (left_target_mm_s == 0)
  {
    sc_speed_force_zero_side(&controller->left);
  }
  if (right_target_mm_s == 0)
  {
    sc_speed_force_zero_side(&controller->right);
  }
  if ((feedback->valid == 0u) || (feedback->left_valid == 0u) ||
      (feedback->right_valid == 0u) ||
      ((uint32_t)(now_ms - feedback->sample_time_ms) >
       SC_WHEEL_MAX_SNAPSHOT_AGE_MS))
  {
    sc_speed_reset_runtime(controller);
    controller->mode = SC_SPEED_CONTROLLER_MODE_STOPPED;
    sc_speed_set_output(output, controller, 0u);
    return SC_SPEED_CONTROLLER_ERR_FEEDBACK;
  }
  if ((controller->feedback_initialized != 0u) &&
      (feedback->sequence == controller->last_feedback_sequence))
  {
    sc_speed_set_output(output, controller, feedback->valid);
    return SC_SPEED_CONTROLLER_WAIT;
  }
  if (controller->feedback_initialized == 0u)
  {
    controller->last_feedback_time_ms = feedback->sample_time_ms;
    controller->last_feedback_sequence = feedback->sequence;
    controller->feedback_initialized = 1u;
    sc_speed_set_output(output, controller, feedback->valid);
    return SC_SPEED_CONTROLLER_WAIT;
  }

  dt_ms =
    (uint32_t)(feedback->sample_time_ms -
               controller->last_feedback_time_ms);
  if (dt_ms == 0u)
  {
    sc_speed_set_output(output, controller, feedback->valid);
    return SC_SPEED_CONTROLLER_WAIT;
  }

  left_status = sc_speed_apply_side(
    &controller->left, &controller->config.left, left_target_mm_s,
    feedback->left_speed_mm_s, dt_ms);
  right_status = sc_speed_apply_side(
    &controller->right, &controller->config.right, right_target_mm_s,
    feedback->right_speed_mm_s, dt_ms);
  controller->last_feedback_time_ms = feedback->sample_time_ms;
  controller->last_feedback_sequence = feedback->sequence;
  sc_speed_set_output(output, controller, feedback->valid);

  if ((left_status != SC_SPEED_CONTROLLER_OK) ||
      (right_status != SC_SPEED_CONTROLLER_OK))
  {
    sc_speed_reset_runtime(controller);
    controller->mode = SC_SPEED_CONTROLLER_MODE_STOPPED;
    sc_speed_set_output(output, controller, 0u);
    return SC_SPEED_CONTROLLER_ERR_CONFIG;
  }
  return SC_SPEED_CONTROLLER_OK;
}
