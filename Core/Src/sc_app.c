/**
  ******************************************************************************
  * @file    sc_app.c
  * @brief   正式固件应用状态机与唯一执行器提交路径。
  ******************************************************************************
  */

#include "sc_app.h"

#include <stddef.h>

#include "sc_button.h"
#include "sc_led.h"
#include "sc_lf04.h"
#include "sc_motor_defaults.h"
#include "sc_safety.h"
#include "sc_tb6612.h"

static int16_t sc_app_abs_i16(int16_t value)
{
  return (value < 0) ? (int16_t)-value : value;
}

static uint8_t sc_app_config_structural_valid(
  const sc_app_config_t *config)
{
  return (uint8_t)(
    (config != NULL) &&
    (config->cruise_mm_s > 0) &&
    ((config->left_motor_sign == -1) ||
     (config->left_motor_sign == 1)) &&
    ((config->right_motor_sign == -1) ||
     (config->right_motor_sign == 1)) &&
    ((config->wheel_calibration.left_encoder_sign == -1) ||
     (config->wheel_calibration.left_encoder_sign == 1)) &&
    ((config->wheel_calibration.right_encoder_sign == -1) ||
     (config->wheel_calibration.right_encoder_sign == 1)) &&
    (config->wheel_calibration.left_counts_per_rev > 0u) &&
    (config->wheel_calibration.right_counts_per_rev > 0u) &&
    (config->wheel_calibration.left_wheel_diameter_um > 0) &&
    (config->wheel_calibration.right_wheel_diameter_um > 0));
}

static uint8_t sc_app_start_config_valid(const sc_app_t *app)
{
  return (uint8_t)(
    (app->config.calibration_valid != 0u) &&
    (app->config.tuning_valid != 0u));
}

static uint8_t sc_app_line_fresh(const sc_app_t *app, uint32_t now_ms)
{
  return (uint8_t)(
    (app->line_sample_initialized != 0u) &&
    (app->line.sample.valid != 0u) &&
    ((uint32_t)(now_ms - app->line.sample.sample_time_ms) <=
     SC_APP_MAX_SAMPLE_GAP_MS));
}

static uint8_t sc_app_feedback_fresh(const sc_app_t *app, uint32_t now_ms)
{
  return (uint8_t)(
    (app->feedback_has_sequence != 0u) &&
    (app->wheel.valid != 0u) &&
    ((uint32_t)(now_ms - app->last_valid_feedback_ms) <=
     SC_APP_MAX_SAMPLE_GAP_MS));
}

static uint8_t sc_app_center_window_ready(const sc_app_t *app)
{
  return (uint8_t)(
    (app->center_sample_count >= SC_APP_START_CENTER_SAMPLES) &&
    ((uint32_t)(app->center_last_time_ms -
                app->center_first_time_ms) >=
     SC_APP_START_CENTER_SPAN_MS));
}

static void sc_app_reset_center_window(sc_app_t *app)
{
  app->center_sample_count = 0u;
  app->center_first_time_ms = 0u;
  app->center_last_time_ms = 0u;
  app->center_last_sequence = 0u;
  app->center_has_sequence = 0u;
}

static void sc_app_update_center_window(sc_app_t *app)
{
  uint32_t current_time = app->line.sample.sample_time_ms;

  if ((app->line.sample.valid == 0u) ||
      (app->line.black_mask != 0x06u))
  {
    sc_app_reset_center_window(app);
    return;
  }
  if ((app->center_has_sequence != 0u) &&
      (app->line.sample.sequence == app->center_last_sequence))
  {
    return;
  }
  if ((app->center_sample_count != 0u) &&
      ((uint32_t)(current_time - app->center_last_time_ms) >
       SC_APP_MAX_SAMPLE_GAP_MS))
  {
    sc_app_reset_center_window(app);
  }

  if (app->center_sample_count == 0u)
  {
    app->center_first_time_ms = current_time;
  }
  if (app->center_sample_count < SC_APP_START_CENTER_SAMPLES)
  {
    app->center_sample_count++;
  }
  app->center_last_time_ms = current_time;
  app->center_last_sequence = app->line.sample.sequence;
  app->center_has_sequence = 1u;
}

static sc_tb6612_status_t sc_app_get_tb_state(sc_app_t *app,
                                               sc_tb6612_state_t *state)
{
  (void)app;
  return sc_tb6612_get_state(state);
}

static uint8_t sc_app_safe_stop_outputs(sc_app_t *app)
{
  sc_speed_controller_output_t stopped_output;
  sc_tb6612_state_t tb_state;
  uint8_t failed = 0u;

  if (sc_speed_controller_stop(
        &app->speed_controller, &stopped_output) !=
      SC_SPEED_CONTROLLER_STOPPED)
  {
    failed = 1u;
  }
  if (sc_app_get_tb_state(app, &tb_state) != SC_TB6612_OK)
  {
    failed = 1u;
  }
  else if (tb_state.enabled != 0u)
  {
    if (sc_tb6612_apply(0, 0) != SC_TB6612_OK)
    {
      failed = 1u;
    }
  }
  if (sc_tb6612_set_standby(0u) != SC_TB6612_OK)
  {
    failed = 1u;
  }

  app->speed_output = (sc_speed_controller_output_t){0};
  app->target = (sc_wheel_target_t){0};
  app->software_stby = 0u;
  if (failed != 0u)
  {
    sc_safety_emergency_stop();
    return 0u;
  }
  return 1u;
}

static void sc_app_enter_fault(sc_app_t *app, uint32_t fault_bits,
                               sc_app_stop_reason_t reason)
{
  uint8_t stop_ok;

  stop_ok = sc_app_safe_stop_outputs(app);
  app->fault_bits |= fault_bits;
  app->stop_reason = reason;
  if (stop_ok == 0u)
  {
    app->fault_bits |= SC_APP_FAULT_TB6612 | SC_APP_FAULT_HARDWARE;
  }
  app->phase = SC_APP_PHASE_FAULT;
  app->recovery_health_active = 0u;
  app->recovery_healthy_since_ms = 0u;
  app->button_press_seen = 0u;
  sc_safety_emergency_stop();
}

static void sc_app_request_manual_stop(sc_app_t *app, uint32_t now_ms)
{
  (void)now_ms;
  if ((app->fault_bits & SC_APP_FATAL_MASK) != 0u)
  {
    sc_safety_emergency_stop();
    return;
  }
  if (sc_app_safe_stop_outputs(app) == 0u)
  {
    sc_app_enter_fault(
      app, SC_APP_FAULT_TB6612 | SC_APP_FAULT_HARDWARE,
      SC_APP_STOP_TB6612);
    return;
  }
  sc_safety_emergency_stop();
  app->stop_reason = SC_APP_STOP_LONG_PRESS;
  app->phase = SC_APP_PHASE_STOPPING;
}

static sc_app_status_t sc_app_start_motion(sc_app_t *app, uint32_t now_ms)
{
  sc_speed_controller_output_t output;

  if ((sc_app_start_config_valid(app) == 0u) ||
      (sc_app_center_window_ready(app) == 0u) ||
      (sc_app_feedback_fresh(app, now_ms) == 0u) ||
      (sc_track_start_eligible(&app->line) == 0u))
  {
    app->stop_reason = SC_APP_STOP_START_REJECTED;
    return SC_APP_OK;
  }

  if (sc_speed_controller_reset(&app->speed_controller, &output) !=
      SC_SPEED_CONTROLLER_STOPPED)
  {
    sc_app_enter_fault(app, SC_APP_FAULT_CONFIG,
                       SC_APP_STOP_CONTROL);
    return SC_APP_FAULT_FATAL;
  }
  sc_track_controller_init(&app->track, app->config.cruise_mm_s);
  if (sc_tb6612_set_standby(1u) != SC_TB6612_OK)
  {
    sc_app_enter_fault(app, SC_APP_FAULT_TB6612,
                       SC_APP_STOP_TB6612);
    return SC_APP_FAULT_FATAL;
  }
  if (sc_tb6612_apply(0, 0) != SC_TB6612_OK)
  {
    sc_app_enter_fault(app, SC_APP_FAULT_TB6612,
                       SC_APP_STOP_TB6612);
    return SC_APP_FAULT_FATAL;
  }
  if (sc_speed_controller_set_mode(
        &app->speed_controller, SC_SPEED_CONTROLLER_MODE_RUNNING,
        &output) != SC_SPEED_CONTROLLER_OK)
  {
    sc_app_enter_fault(app, SC_APP_FAULT_CONFIG,
                       SC_APP_STOP_CONTROL);
    return SC_APP_FAULT_FATAL;
  }

  app->speed_output = output;
  app->target = (sc_wheel_target_t){0};
  app->phase = SC_APP_PHASE_ACTIVE;
  app->stop_reason = SC_APP_STOP_NONE;
  app->software_stby = 1u;
  app->reverse_samples[0] = 0u;
  app->reverse_samples[1] = 0u;
  app->stall_active[0] = 0u;
  app->stall_active[1] = 0u;
  app->stall_window_start_ms[0] = 0u;
  app->stall_window_start_ms[1] = 0u;
  app->stall_window_start_count[0] = 0;
  app->stall_window_start_count[1] = 0;
  app->stall_window_target_mm_s[0] = 0;
  app->stall_window_target_mm_s[1] = 0;
  return SC_APP_OK;
}

static uint8_t sc_app_sample_line(sc_app_t *app, uint32_t now_ms)
{
  sc_line_sample_t sample = {
    .raw_levels = 0u,
    .sample_time_ms = now_ms,
    .sequence = 0u,
    .valid = 0u
  };

  if ((app->line_sample_initialized != 0u) &&
      ((uint32_t)(now_ms - app->last_line_sample_ms) <
       SC_APP_LINE_PERIOD_MS))
  {
    return 0u;
  }

  if (sc_lf04_sample(now_ms, &sample) != SC_LF04_OK)
  {
    sample.sample_time_ms = now_ms;
    sample.valid = 0u;
  }
  (void)sc_line_service_update(&sample);
  if (sc_line_service_get_latest(&app->line) != SC_LINE_OK)
  {
    app->line = (sc_line_state_t){0};
    app->line.sample = sample;
  }
  app->last_line_sample_ms = now_ms;
  app->line_sample_initialized = 1u;
  sc_app_update_center_window(app);
  return 1u;
}

static uint32_t sc_app_sample_encoder(sc_app_t *app, uint32_t now_ms,
                                      uint8_t *new_snapshot)
{
  sc_encoder_snapshot_t snapshot;
  sc_encoder_status_t encoder_status;
  sc_wheel_service_status_t wheel_status;
  uint32_t fault_bits = 0u;

  *new_snapshot = 0u;
  encoder_status = sc_encoder_service(now_ms);
  if ((encoder_status == SC_ENCODER_ERR_HAL) ||
      (encoder_status == SC_ENCODER_ERR_CONFIG) ||
      (encoder_status == SC_ENCODER_ERR_NOT_READY))
  {
    return SC_APP_FAULT_HARDWARE;
  }
  if (sc_encoder_get_latest(&snapshot) != SC_ENCODER_OK)
  {
    return SC_APP_FAULT_HARDWARE;
  }
  app->encoder = snapshot;

  if ((app->encoder_has_sequence == 0u) ||
      (snapshot.sequence != app->last_encoder_sequence))
  {
    app->last_encoder_sequence = snapshot.sequence;
    app->encoder_has_sequence = 1u;
    *new_snapshot = 1u;
    wheel_status = sc_wheel_service_update(
      &app->wheel_service, &snapshot,
      &app->config.wheel_calibration, now_ms, &app->wheel);
    if ((wheel_status == SC_WHEEL_SERVICE_ERR_ARGUMENT) ||
        (wheel_status == SC_WHEEL_SERVICE_ERR_CALIBRATION) ||
        (wheel_status == SC_WHEEL_SERVICE_ERR_NOT_READY))
    {
      fault_bits |= SC_APP_FAULT_HARDWARE;
    }
    if (app->wheel.valid != 0u)
    {
      app->feedback_has_sequence = 1u;
      app->last_feedback_sequence = app->wheel.sequence;
      app->last_valid_feedback_ms = app->wheel.sample_time_ms;
    }
  }
  if (encoder_status == SC_ENCODER_ERR_LOST_SYNC)
  {
    fault_bits |= SC_APP_FAULT_FEEDBACK;
  }
  return fault_bits;
}

static uint8_t sc_app_start_prerequisites(sc_app_t *app, uint32_t now_ms)
{
  sc_tb6612_state_t tb_state;

  if ((app->fault_bits != 0u) ||
      (sc_app_start_config_valid(app) == 0u) ||
      (sc_app_center_window_ready(app) == 0u) ||
      (sc_app_line_fresh(app, now_ms) == 0u) ||
      (sc_app_feedback_fresh(app, now_ms) == 0u) ||
      (sc_track_start_eligible(&app->line) == 0u))
  {
    return 0u;
  }
  if (sc_app_get_tb_state(app, &tb_state) != SC_TB6612_OK)
  {
    sc_app_enter_fault(app, SC_APP_FAULT_HARDWARE,
                       SC_APP_STOP_TB6612);
    return 0u;
  }
  if ((tb_state.mode != SC_TB6612_MODE_STANDBY) ||
      (tb_state.standby != 0u))
  {
    return 0u;
  }
  return 1u;
}

static int16_t sc_app_apply_motor_sign(int16_t value, int8_t sign)
{
  return (sign < 0) ? (int16_t)-value : value;
}

static int64_t sc_app_physical_count(int64_t total_count, int8_t sign)
{
  if (sign >= 0)
  {
    return total_count;
  }
  if (total_count == INT64_MIN)
  {
    return INT64_MAX;
  }
  return -total_count;
}

static int64_t sc_app_saturating_multiply_positive(int64_t left,
                                                   int64_t right)
{
  if ((left <= 0) || (right <= 0))
  {
    return 0;
  }
  if (left > (INT64_MAX / right))
  {
    return INT64_MAX;
  }
  return left * right;
}

static int64_t sc_app_stall_minimum_count(
  const sc_app_t *app, uint8_t side, int32_t target_mm_s,
  uint32_t elapsed_ms)
{
  const uint32_t counts_per_rev[2] = {
    app->config.wheel_calibration.left_counts_per_rev,
    app->config.wheel_calibration.right_counts_per_rev
  };
  const int32_t wheel_diameter_um[2] = {
    app->config.wheel_calibration.left_wheel_diameter_um,
    app->config.wheel_calibration.right_wheel_diameter_um
  };
  int64_t distance_numerator;
  int64_t counts_numerator;
  int64_t circumference_numerator;
  int64_t theoretical_counts;

  /*
   * counts = target * elapsed_ms * counts_per_rev /
   *          (pi * wheel_diameter_um)
   * 用 355/113 近似 pi，并要求至少达到理论计数的 20%。
   */
  distance_numerator =
    sc_app_saturating_multiply_positive(target_mm_s, elapsed_ms);
  counts_numerator = sc_app_saturating_multiply_positive(
    distance_numerator, counts_per_rev[side]);
  counts_numerator = sc_app_saturating_multiply_positive(
    counts_numerator, 113);
  circumference_numerator =
    (int64_t)wheel_diameter_um[side] * 355;
  if (circumference_numerator <= 0)
  {
    return INT64_MAX;
  }
  theoretical_counts = counts_numerator / circumference_numerator;
  if (theoretical_counts < 1)
  {
    return 1;
  }
  return (theoretical_counts / SC_APP_STALL_PROGRESS_DIVISOR) +
    ((theoretical_counts % SC_APP_STALL_PROGRESS_DIVISOR) != 0 ? 1 : 0);
}

static uint32_t sc_app_monitor_wheel_health(
  sc_app_t *app, uint32_t now_ms)
{
  const sc_encoder_side_snapshot_t *sides[2] = {
    &app->encoder.left, &app->encoder.right
  };
  const int32_t targets[2] = {
    app->target.left_mm_s, app->target.right_mm_s
  };
  const int16_t outputs[2] = {
    app->speed_output.left_permille, app->speed_output.right_permille
  };
  const int8_t signs[2] = {
    app->config.wheel_calibration.left_encoder_sign,
    app->config.wheel_calibration.right_encoder_sign
  };
  uint32_t fault_bits = 0u;
  uint8_t side;

  for (side = 0u; side < 2u; side++)
  {
    int64_t physical_count =
      sc_app_physical_count(sides[side]->total_count, signs[side]);
    int64_t physical_delta =
      (int64_t)sides[side]->delta_count * signs[side];
    int8_t target_sign =
      (targets[side] > 0) ? 1 : ((targets[side] < 0) ? -1 : 0);
    int8_t command_sign =
      (outputs[side] > 0) ? 1 : ((outputs[side] < 0) ? -1 : 0);
    uint8_t command_matches_target =
      (uint8_t)((target_sign != 0) && (target_sign == command_sign));
    uint8_t command_reversed =
      (uint8_t)(((command_sign > 0) && (physical_delta < 0)) ||
                ((command_sign < 0) && (physical_delta > 0)));
    uint8_t stall_condition =
      (uint8_t)((targets[side] >= SC_APP_STALL_TARGET_MM_S) &&
                (sc_app_abs_i16(outputs[side]) >=
                 SC_APP_STALL_PWM_PERMILLE));

    if ((command_matches_target != 0u) && (command_reversed != 0u))
    {
      if (app->reverse_samples[side] < UINT8_MAX)
      {
        app->reverse_samples[side]++;
      }
    }
    else
    {
      app->reverse_samples[side] = 0u;
    }
    if (app->reverse_samples[side] >= SC_APP_REVERSE_SAMPLES)
    {
      fault_bits |= (side == 0u) ? SC_APP_FAULT_DIRECTION_LEFT :
                                   SC_APP_FAULT_DIRECTION_RIGHT;
    }

    if (stall_condition != 0u)
    {
      if (app->stall_active[side] == 0u)
      {
        app->stall_active[side] = 1u;
        app->stall_window_start_ms[side] = now_ms;
        app->stall_window_start_count[side] = physical_count;
        app->stall_window_target_mm_s[side] = targets[side];
      }
      else if ((uint32_t)(now_ms - app->stall_window_start_ms[side]) >=
               SC_APP_STALL_TIME_MS)
      {
        int64_t net_progress =
          physical_count - app->stall_window_start_count[side];
        int64_t minimum_progress = sc_app_stall_minimum_count(
          app, side, app->stall_window_target_mm_s[side],
          (uint32_t)(now_ms - app->stall_window_start_ms[side]));

        if ((net_progress <= 0) || (net_progress < minimum_progress))
        {
          fault_bits |= (side == 0u) ? SC_APP_FAULT_STALL_LEFT :
                                       SC_APP_FAULT_STALL_RIGHT;
        }
        else
        {
          app->stall_window_start_ms[side] = now_ms;
          app->stall_window_start_count[side] = physical_count;
          app->stall_window_target_mm_s[side] = targets[side];
        }
      }
    }
    else
    {
      app->stall_active[side] = 0u;
      app->stall_window_start_ms[side] = 0u;
      app->stall_window_start_count[side] = 0;
      app->stall_window_target_mm_s[side] = 0;
    }
  }
  return fault_bits;
}

static uint32_t sc_app_update_motion(sc_app_t *app, uint32_t now_ms,
                                     uint8_t line_updated,
                                     uint8_t encoder_updated)
{
  sc_track_status_t track_status;
  sc_speed_controller_output_t speed_output;
  sc_speed_controller_status_t speed_status;
  sc_tb6612_state_t tb_state;
  int16_t left_pwm;
  int16_t right_pwm;
  uint32_t faults = 0u;

  if (sc_app_line_fresh(app, now_ms) == 0u)
  {
    return SC_APP_FAULT_LINE_INVALID;
  }
  if (line_updated != 0u)
  {
    if ((app->line.sample.valid == 0u) ||
        ((app->line.black_mask & 0xF0u) != 0u))
    {
      return SC_APP_FAULT_LINE_INVALID;
    }
    track_status = sc_track_controller_step(
      &app->track, &app->line, now_ms, &app->target);
    if (track_status == SC_TRACK_ERR_ARGUMENT)
    {
      return SC_APP_FAULT_CONFIG;
    }
    if (track_status == SC_TRACK_ERR_INVALID_LINE)
    {
      return SC_APP_FAULT_LINE_INVALID;
    }
    if (track_status == SC_TRACK_ERR_LOST)
    {
      return SC_APP_FAULT_LINE_LOST;
    }
  }

  if (encoder_updated != 0u)
  {
    if (app->wheel.valid == 0u)
    {
      return SC_APP_FAULT_FEEDBACK;
    }
    speed_status = sc_speed_controller_step(
      &app->speed_controller, app->target.left_mm_s,
      app->target.right_mm_s, &app->wheel, now_ms, &speed_output);
    if ((speed_status != SC_SPEED_CONTROLLER_OK) &&
        (speed_status != SC_SPEED_CONTROLLER_WAIT))
    {
      return SC_APP_FAULT_CONTROL;
    }
    app->speed_output = speed_output;
    left_pwm = sc_app_apply_motor_sign(
      speed_output.left_permille, app->config.left_motor_sign);
    right_pwm = sc_app_apply_motor_sign(
      speed_output.right_permille, app->config.right_motor_sign);
    if (sc_tb6612_apply(left_pwm, right_pwm) != SC_TB6612_OK)
    {
      return SC_APP_FAULT_TB6612;
    }
    faults |= sc_app_monitor_wheel_health(app, now_ms);
  }
  else if (sc_app_feedback_fresh(app, now_ms) == 0u)
  {
    return SC_APP_FAULT_FEEDBACK;
  }

  if (sc_app_get_tb_state(app, &tb_state) != SC_TB6612_OK)
  {
    return SC_APP_FAULT_HARDWARE;
  }
  if ((tb_state.mode != SC_TB6612_MODE_ENABLED) ||
      (tb_state.enabled == 0u) || (tb_state.standby == 0u))
  {
    return SC_APP_FAULT_TB6612;
  }
  app->software_stby = 1u;
  return faults;
}

static sc_app_stop_reason_t sc_app_reason_for_fault(uint32_t fault_bits)
{
  if ((fault_bits & SC_APP_FAULT_DIRECTION_LEFT) != 0u)
  {
    return SC_APP_STOP_DIRECTION_LEFT;
  }
  if ((fault_bits & SC_APP_FAULT_DIRECTION_RIGHT) != 0u)
  {
    return SC_APP_STOP_DIRECTION_RIGHT;
  }
  if ((fault_bits & SC_APP_FAULT_STALL_LEFT) != 0u)
  {
    return SC_APP_STOP_STALL_LEFT;
  }
  if ((fault_bits & SC_APP_FAULT_STALL_RIGHT) != 0u)
  {
    return SC_APP_STOP_STALL_RIGHT;
  }
  if ((fault_bits & SC_APP_FAULT_SCHEDULE) != 0u)
  {
    return SC_APP_STOP_SCHEDULE;
  }
  if ((fault_bits & SC_APP_FAULT_LINE_LOST) != 0u)
  {
    return SC_APP_STOP_LINE_LOST;
  }
  if ((fault_bits & SC_APP_FAULT_LINE_INVALID) != 0u)
  {
    return SC_APP_STOP_LINE_INVALID;
  }
  if ((fault_bits & SC_APP_FAULT_FEEDBACK) != 0u)
  {
    return SC_APP_STOP_FEEDBACK;
  }
  if ((fault_bits & SC_APP_FAULT_TB6612) != 0u)
  {
    return SC_APP_STOP_TB6612;
  }
  return SC_APP_STOP_CONTROL;
}

static void sc_app_process_button(sc_app_t *app, uint8_t events,
                                  uint32_t now_ms)
{
  if ((events & SC_BUTTON_EVENT_LONG_PRESS) != 0u)
  {
    app->long_press_consumed = 1u;
    app->button_press_seen = 0u;
    if (app->phase == SC_APP_PHASE_FAULT)
    {
      sc_safety_emergency_stop();
      app->stop_reason = SC_APP_STOP_LONG_PRESS;
    }
    else
    {
      sc_app_request_manual_stop(app, now_ms);
    }
    return;
  }

  if ((events & SC_BUTTON_EVENT_PRESSED) != 0u)
  {
    if ((app->long_press_consumed == 0u) &&
        ((app->phase == SC_APP_PHASE_IDLE) ||
         (app->phase == SC_APP_PHASE_READY)))
    {
      app->button_press_seen = 1u;
    }
  }

  if ((events & SC_BUTTON_EVENT_RELEASED) != 0u)
  {
    uint8_t short_release =
      (uint8_t)((app->button_press_seen != 0u) &&
                (app->long_press_consumed == 0u));

    app->button_press_seen = 0u;
    app->long_press_consumed = 0u;
    if ((short_release != 0u) &&
        (app->phase == SC_APP_PHASE_READY))
    {
      if (sc_app_start_motion(app, now_ms) == SC_APP_OK)
      {
        return;
      }
    }
    else if (short_release != 0u)
    {
      app->stop_reason = SC_APP_STOP_START_REJECTED;
    }
  }
}

static void sc_app_update_ready(sc_app_t *app, uint32_t now_ms)
{
  if ((app->phase == SC_APP_PHASE_ACTIVE) ||
      (app->phase == SC_APP_PHASE_FAULT) ||
      (app->phase == SC_APP_PHASE_STOPPING) ||
      (app->phase == SC_APP_PHASE_BOOT))
  {
    return;
  }

  if (sc_app_start_prerequisites(app, now_ms) != 0u)
  {
    app->phase = SC_APP_PHASE_READY;
  }
  else if (app->phase != SC_APP_PHASE_FAULT)
  {
    app->phase = SC_APP_PHASE_IDLE;
  }
}

static uint8_t sc_app_recovery_healthy(sc_app_t *app, uint32_t now_ms)
{
  sc_tb6612_state_t tb_state;

  if ((sc_app_start_config_valid(app) == 0u) ||
      (sc_app_line_fresh(app, now_ms) == 0u) ||
      (sc_app_feedback_fresh(app, now_ms) == 0u) ||
      (app->line.black_mask == 0x00u))
  {
    return 0u;
  }
  if (((app->fault_bits & SC_APP_FAULT_LINE_LOST) != 0u) &&
      ((app->line.black_mask != 0x06u) ||
       (sc_app_center_window_ready(app) == 0u)))
  {
    return 0u;
  }
  if (sc_app_get_tb_state(app, &tb_state) != SC_TB6612_OK)
  {
    app->fault_bits |= SC_APP_FAULT_HARDWARE;
    return 0u;
  }
  return (uint8_t)(
    (tb_state.mode == SC_TB6612_MODE_STANDBY) &&
    (tb_state.enabled == 0u) && (tb_state.standby == 0u));
}

static void sc_app_process_recovery(sc_app_t *app, uint32_t now_ms)
{
  if ((app->fault_bits & SC_APP_FATAL_MASK) != 0u)
  {
    return;
  }
  if (sc_app_recovery_healthy(app, now_ms) == 0u)
  {
    app->recovery_health_active = 0u;
    app->recovery_healthy_since_ms = 0u;
    return;
  }
  if (app->recovery_health_active == 0u)
  {
    app->recovery_health_active = 1u;
    app->recovery_healthy_since_ms = now_ms;
    return;
  }
  if ((uint32_t)(now_ms - app->recovery_healthy_since_ms) >=
      SC_APP_RECOVERY_HEALTH_MS)
  {
    app->fault_bits &= SC_APP_FATAL_MASK;
    app->recovery_health_active = 0u;
    app->recovery_healthy_since_ms = 0u;
    app->phase = SC_APP_PHASE_IDLE;
  }
}

void sc_app_get_default_config(sc_app_config_t *config)
{
  if (config == NULL)
  {
    return;
  }
  config->wheel_calibration.left_encoder_sign =
    SC_MOTOR_LEFT_ENCODER_SIGN;
  config->wheel_calibration.right_encoder_sign =
    SC_MOTOR_RIGHT_ENCODER_SIGN;
  config->wheel_calibration.left_counts_per_rev =
    SC_MOTOR_LEFT_COUNTS_PER_REV;
  config->wheel_calibration.right_counts_per_rev =
    SC_MOTOR_RIGHT_COUNTS_PER_REV;
  config->wheel_calibration.left_wheel_diameter_um =
    SC_MOTOR_LEFT_WHEEL_DIAMETER_UM;
  config->wheel_calibration.right_wheel_diameter_um =
    SC_MOTOR_RIGHT_WHEEL_DIAMETER_UM;
  config->speed_controller.left.kp_milli = SC_MOTOR_LEFT_KP_MILLI;
  config->speed_controller.left.ki_milli = SC_MOTOR_LEFT_KI_MILLI;
  config->speed_controller.left.kd_milli = SC_MOTOR_LEFT_KD_MILLI;
  config->speed_controller.left.ff_milli = SC_MOTOR_LEFT_FF_MILLI;
  config->speed_controller.right.kp_milli = SC_MOTOR_RIGHT_KP_MILLI;
  config->speed_controller.right.ki_milli = SC_MOTOR_RIGHT_KI_MILLI;
  config->speed_controller.right.kd_milli = SC_MOTOR_RIGHT_KD_MILLI;
  config->speed_controller.right.ff_milli = SC_MOTOR_RIGHT_FF_MILLI;
  config->cruise_mm_s = SC_MOTOR_CRUISE_SPEED_MM_S;
  config->calibration_valid = SC_MOTOR_CALIBRATION_VALID;
  config->tuning_valid = SC_MOTOR_TUNING_VALID;
  config->left_motor_sign = SC_MOTOR_LEFT_MOTOR_SIGN;
  config->right_motor_sign = SC_MOTOR_RIGHT_MOTOR_SIGN;
}

sc_app_status_t sc_app_init(sc_app_t *app, const sc_app_config_t *config,
                            uint32_t now_ms)
{
  sc_app_config_t defaults;
  const sc_app_config_t *effective = config;
  sc_tb6612_state_t tb_state;

  if (app == NULL)
  {
    return SC_APP_ERR_ARGUMENT;
  }
  *app = (sc_app_t){0};
  app->phase = SC_APP_PHASE_BOOT;
  app->last_service_ms = now_ms;
  if (effective == NULL)
  {
    sc_app_get_default_config(&defaults);
    effective = &defaults;
  }
  if (sc_app_config_structural_valid(effective) == 0u)
  {
    app->config = *effective;
    app->fault_bits = SC_APP_FAULT_CONFIG;
    app->stop_reason = SC_APP_STOP_INIT;
    app->phase = SC_APP_PHASE_FAULT;
    app->initialized = 1u;
    sc_safety_emergency_stop();
    return SC_APP_ERR_CONFIG;
  }

  app->config = *effective;
  if ((sc_led_init() != SC_LED_OK) ||
      (sc_button_init() != SC_BUTTON_OK) ||
      (sc_lf04_init() != SC_LF04_OK) ||
      (sc_tb6612_init() != SC_TB6612_OK) ||
      (sc_encoder_init() != SC_ENCODER_OK))
  {
    app->fault_bits = SC_APP_FAULT_INIT | SC_APP_FAULT_HARDWARE;
    app->stop_reason = SC_APP_STOP_INIT;
    app->phase = SC_APP_PHASE_FAULT;
    app->initialized = 1u;
    sc_safety_emergency_stop();
    return SC_APP_ERR_NOT_READY;
  }
  if ((sc_wheel_service_init(
         &app->wheel_service, &app->config.wheel_calibration) !=
       SC_WHEEL_SERVICE_OK) ||
      (sc_speed_controller_init(
         &app->speed_controller, &app->config.speed_controller) !=
       SC_SPEED_CONTROLLER_OK))
  {
    app->fault_bits = SC_APP_FAULT_CONFIG;
    app->stop_reason = SC_APP_STOP_INIT;
    app->phase = SC_APP_PHASE_FAULT;
    app->initialized = 1u;
    sc_safety_emergency_stop();
    return SC_APP_ERR_CONFIG;
  }
  sc_track_controller_init(&app->track, app->config.cruise_mm_s);
  if ((sc_tb6612_set_standby(0u) != SC_TB6612_OK) ||
      (sc_app_get_tb_state(app, &tb_state) != SC_TB6612_OK) ||
      (tb_state.mode != SC_TB6612_MODE_STANDBY))
  {
    app->fault_bits = SC_APP_FAULT_INIT | SC_APP_FAULT_TB6612;
    app->stop_reason = SC_APP_STOP_INIT;
    app->phase = SC_APP_PHASE_FAULT;
    app->initialized = 1u;
    sc_safety_emergency_stop();
    return SC_APP_ERR_NOT_READY;
  }

  app->software_stby = 0u;
  app->phase = SC_APP_PHASE_IDLE;
  app->initialized = 1u;
  return SC_APP_OK;
}

sc_app_status_t sc_app_service(sc_app_t *app, uint32_t now_ms)
{
  uint8_t line_updated;
  uint8_t encoder_updated;
  uint8_t button_events;
  uint32_t core_faults;
  uint32_t runtime_faults;
  uint32_t loop_interval_ms;

  if (app == NULL)
  {
    return SC_APP_ERR_ARGUMENT;
  }
  if (app->initialized == 0u)
  {
    return SC_APP_ERR_NOT_READY;
  }

  loop_interval_ms = (uint32_t)(now_ms - app->last_service_ms);
  app->last_service_ms = now_ms;
  if ((loop_interval_ms > SC_APP_SCHEDULE_LIMIT_MS) &&
      ((app->fault_bits & SC_APP_FATAL_MASK) == 0u))
  {
    sc_app_enter_fault(app, SC_APP_FAULT_SCHEDULE, SC_APP_STOP_SCHEDULE);
  }

  line_updated = sc_app_sample_line(app, now_ms);
  core_faults = sc_app_sample_encoder(app, now_ms, &encoder_updated);
  if ((core_faults & SC_APP_FAULT_HARDWARE) != 0u)
  {
    sc_app_enter_fault(app, SC_APP_FAULT_HARDWARE,
                       SC_APP_STOP_TB6612);
  }
  else if (((core_faults & SC_APP_FAULT_FEEDBACK) != 0u) &&
           (app->phase == SC_APP_PHASE_ACTIVE))
  {
    sc_app_enter_fault(app, SC_APP_FAULT_FEEDBACK,
                       SC_APP_STOP_FEEDBACK);
  }

  button_events = sc_button_poll(now_ms);
  sc_app_process_button(app, button_events, now_ms);

  if ((app->phase == SC_APP_PHASE_STOPPING) &&
      ((button_events & SC_BUTTON_EVENT_LONG_PRESS) == 0u))
  {
    app->phase = SC_APP_PHASE_IDLE;
  }

  if (app->phase == SC_APP_PHASE_FAULT)
  {
    sc_app_process_recovery(app, now_ms);
  }
  else if (app->phase == SC_APP_PHASE_ACTIVE)
  {
    runtime_faults =
      sc_app_update_motion(app, now_ms, line_updated, encoder_updated);
    if (runtime_faults != 0u)
    {
      sc_app_enter_fault(
        app, runtime_faults, sc_app_reason_for_fault(runtime_faults));
    }
  }
  else
  {
    sc_app_update_ready(app, now_ms);
  }

  app->sequence++;
  if ((app->fault_bits & SC_APP_FATAL_MASK) != 0u)
  {
    return SC_APP_FAULT_FATAL;
  }
  if (app->phase == SC_APP_PHASE_FAULT)
  {
    return SC_APP_FAULT_RECOVERABLE;
  }
  return SC_APP_OK;
}

sc_app_status_t sc_app_snapshot(const sc_app_t *app,
                                sc_app_snapshot_t *snapshot)
{
  if ((app == NULL) || (snapshot == NULL))
  {
    return SC_APP_ERR_ARGUMENT;
  }
  if (app->initialized == 0u)
  {
    return SC_APP_ERR_NOT_READY;
  }

  snapshot->phase = app->phase;
  snapshot->line = app->line;
  snapshot->encoder = app->encoder;
  snapshot->wheel = app->wheel;
  snapshot->target = app->target;
  snapshot->speed_output = app->speed_output;
  snapshot->left_pwm = sc_app_apply_motor_sign(
    app->speed_output.left_permille, app->config.left_motor_sign);
  snapshot->right_pwm = sc_app_apply_motor_sign(
    app->speed_output.right_permille, app->config.right_motor_sign);
  snapshot->fault_bits = app->fault_bits;
  snapshot->degraded_bits = app->degraded_bits;
  snapshot->stop_reason = app->stop_reason;
  snapshot->software_stby = app->software_stby;
  snapshot->calibration_valid = app->config.calibration_valid;
  snapshot->tuning_valid = app->config.tuning_valid;
  snapshot->sequence = app->sequence;
  snapshot->time_ms = app->last_service_ms;
  return SC_APP_OK;
}

sc_app_status_t sc_app_report_degraded(sc_app_t *app, uint32_t degraded_bits)
{
  if (app == NULL)
  {
    return SC_APP_ERR_ARGUMENT;
  }
  if (app->initialized == 0u)
  {
    return SC_APP_ERR_NOT_READY;
  }
  app->degraded_bits |= degraded_bits;
  return SC_APP_OK;
}
