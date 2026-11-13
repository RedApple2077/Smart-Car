/**
  ******************************************************************************
  * @file    sc_app.h
  * @brief   正式固件应用状态机、启动门、故障恢复和唯一执行器提交。
  ******************************************************************************
  */

#ifndef __SC_APP_H
#define __SC_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "sc_control_types.h"
#include "sc_encoder.h"
#include "sc_line_service.h"
#include "sc_speed_controller.h"
#include "sc_track.h"
#include "sc_wheel_service.h"

#define SC_APP_FAULT_LINE_INVALID    (1u << 0)
#define SC_APP_FAULT_LINE_LOST       (1u << 1)
#define SC_APP_FAULT_FEEDBACK        (1u << 2)
#define SC_APP_FAULT_CONTROL         (1u << 3)
#define SC_APP_FAULT_SCHEDULE        (1u << 4)
#define SC_APP_FAULT_STALL_LEFT      (1u << 5)
#define SC_APP_FAULT_STALL_RIGHT     (1u << 6)
#define SC_APP_FAULT_TB6612          (1u << 7)
#define SC_APP_FAULT_HARDWARE        (1u << 8)
#define SC_APP_FAULT_DIRECTION_LEFT  (1u << 9)
#define SC_APP_FAULT_DIRECTION_RIGHT (1u << 10)
#define SC_APP_FAULT_CONFIG          (1u << 11)
#define SC_APP_FAULT_INIT            (1u << 12)

#define SC_APP_FATAL_MASK \
  (SC_APP_FAULT_TB6612 | SC_APP_FAULT_HARDWARE | \
   SC_APP_FAULT_DIRECTION_LEFT | SC_APP_FAULT_DIRECTION_RIGHT | \
   SC_APP_FAULT_CONFIG | SC_APP_FAULT_INIT)

#define SC_APP_DEGRADED_IMU        (1u << 0)
#define SC_APP_DEGRADED_DISPLAY    (1u << 1)
#define SC_APP_DEGRADED_TELEMETRY  (1u << 2)

#define SC_APP_LINE_PERIOD_MS         10u
#define SC_APP_START_CENTER_SAMPLES   6u
#define SC_APP_START_CENTER_SPAN_MS   50u
#define SC_APP_MAX_SAMPLE_GAP_MS      30u
#define SC_APP_SCHEDULE_LIMIT_MS      100u
#define SC_APP_RECOVERY_HEALTH_MS     200u
#define SC_APP_STALL_TARGET_MM_S      50
#define SC_APP_STALL_PWM_PERMILLE     150
#define SC_APP_STALL_TIME_MS          300u
/* 固定窗口至少完成理论进展的 20%。 */
#define SC_APP_STALL_PROGRESS_DIVISOR 5u
#define SC_APP_REVERSE_SAMPLES        3u

typedef enum {
  SC_APP_OK = 0,
  SC_APP_ERR_ARGUMENT,
  SC_APP_ERR_CONFIG,
  SC_APP_ERR_NOT_READY,
  SC_APP_FAULT_RECOVERABLE,
  SC_APP_FAULT_FATAL
} sc_app_status_t;

typedef enum {
  SC_APP_PHASE_BOOT = 0,
  SC_APP_PHASE_IDLE,
  SC_APP_PHASE_READY,
  SC_APP_PHASE_ACTIVE,
  SC_APP_PHASE_STOPPING,
  SC_APP_PHASE_FAULT
} sc_app_phase_t;

typedef enum {
  SC_APP_STOP_NONE = 0,
  SC_APP_STOP_START_REJECTED,
  SC_APP_STOP_LONG_PRESS,
  SC_APP_STOP_LINE_INVALID,
  SC_APP_STOP_LINE_LOST,
  SC_APP_STOP_FEEDBACK,
  SC_APP_STOP_CONTROL,
  SC_APP_STOP_SCHEDULE,
  SC_APP_STOP_STALL_LEFT,
  SC_APP_STOP_STALL_RIGHT,
  SC_APP_STOP_DIRECTION_LEFT,
  SC_APP_STOP_DIRECTION_RIGHT,
  SC_APP_STOP_TB6612,
  SC_APP_STOP_INIT
} sc_app_stop_reason_t;

typedef struct {
  sc_wheel_calibration_t wheel_calibration;
  sc_speed_controller_config_t speed_controller;
  int32_t cruise_mm_s;
  uint8_t calibration_valid;
  uint8_t tuning_valid;
  int8_t left_motor_sign;
  int8_t right_motor_sign;
} sc_app_config_t;

typedef struct {
  sc_app_phase_t phase;
  sc_line_state_t line;
  sc_encoder_snapshot_t encoder;
  sc_wheel_feedback_t wheel;
  sc_wheel_target_t target;
  sc_speed_controller_output_t speed_output;
  int16_t left_pwm;
  int16_t right_pwm;
  uint32_t fault_bits;
  uint32_t degraded_bits;
  sc_app_stop_reason_t stop_reason;
  uint8_t software_stby;
  uint8_t calibration_valid;
  uint8_t tuning_valid;
  uint32_t sequence;
  uint32_t time_ms;
} sc_app_snapshot_t;

typedef struct {
  sc_app_config_t config;
  sc_app_phase_t phase;
  sc_app_stop_reason_t stop_reason;
  uint32_t fault_bits;
  uint32_t degraded_bits;
  uint32_t sequence;
  uint32_t last_service_ms;
  uint32_t last_line_sample_ms;
  uint32_t last_encoder_sequence;
  uint32_t last_feedback_sequence;
  uint32_t last_valid_feedback_ms;
  uint32_t center_first_time_ms;
  uint32_t center_last_time_ms;
  uint32_t center_last_sequence;
  uint32_t recovery_healthy_since_ms;
  uint32_t stall_window_start_ms[2];
  int64_t stall_window_start_count[2];
  int32_t stall_window_target_mm_s[2];
  uint8_t center_sample_count;
  uint8_t center_has_sequence;
  uint8_t encoder_has_sequence;
  uint8_t feedback_has_sequence;
  uint8_t line_sample_initialized;
  uint8_t button_press_seen;
  uint8_t long_press_consumed;
  uint8_t recovery_health_active;
  uint8_t reverse_samples[2];
  uint8_t stall_active[2];
  uint8_t software_stby;
  uint8_t initialized;
  sc_line_state_t line;
  sc_encoder_snapshot_t encoder;
  sc_wheel_feedback_t wheel;
  sc_wheel_target_t target;
  sc_speed_controller_output_t speed_output;
  sc_wheel_service_t wheel_service;
  sc_track_controller_t track;
  sc_speed_controller_t speed_controller;
} sc_app_t;

void sc_app_get_default_config(sc_app_config_t *config);

sc_app_status_t sc_app_init(sc_app_t *app, const sc_app_config_t *config,
                            uint32_t now_ms);

sc_app_status_t sc_app_service(sc_app_t *app, uint32_t now_ms);

sc_app_status_t sc_app_snapshot(const sc_app_t *app,
                                sc_app_snapshot_t *snapshot);

sc_app_status_t sc_app_report_degraded(sc_app_t *app, uint32_t degraded_bits);

#ifdef __cplusplus
}
#endif

#endif /* __SC_APP_H */
