/**
  ******************************************************************************
  * @file    sc_speed_controller.h
  * @brief   左右独立双轮速度控制，输出 PWM 千分比。
  *          本模块不访问 HAL、GPIO、TIM 或 TB6612。
  ******************************************************************************
  */

#ifndef __SC_SPEED_CONTROLLER_H
#define __SC_SPEED_CONTROLLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "sc_pid.h"
#include "sc_wheel_service.h"

#define SC_SPEED_OUTPUT_LIMIT_PERMILLE   564
#define SC_SPEED_OUTPUT_RAMP_PERMILLE_S  8000
#define SC_SPEED_TARGET_RAMP_MM_S2       2000

typedef enum {
  SC_SPEED_CONTROLLER_OK = 0,
  SC_SPEED_CONTROLLER_WAIT,
  SC_SPEED_CONTROLLER_STOPPED,
  SC_SPEED_CONTROLLER_ERR_ARGUMENT,
  SC_SPEED_CONTROLLER_ERR_CONFIG,
  SC_SPEED_CONTROLLER_ERR_NOT_READY,
  SC_SPEED_CONTROLLER_ERR_FEEDBACK
} sc_speed_controller_status_t;

typedef enum {
  SC_SPEED_CONTROLLER_MODE_STOPPED = 0,
  SC_SPEED_CONTROLLER_MODE_RUNNING
} sc_speed_controller_mode_t;

typedef struct {
  int32_t kp_milli; /* 1000 = 1 PWM 千分比 / (mm/s) */
  int32_t ki_milli; /* 1000 = 1 PWM 千分比 / mm */
  int32_t kd_milli; /* 1000 = 1 PWM 千分比 / (mm/s^2) */
  int32_t ff_milli; /* 1000 = 1 PWM 千分比 / (mm/s) */
} sc_speed_gains_t;

typedef struct {
  sc_speed_gains_t left;
  sc_speed_gains_t right;
} sc_speed_controller_config_t;

typedef struct {
  int16_t left_permille;
  int16_t right_permille;
  int32_t left_target_ramped_mm_s;
  int32_t right_target_ramped_mm_s;
  int64_t left_requested_permille;
  int64_t right_requested_permille;
  uint8_t feedback_valid;
} sc_speed_controller_output_t;

typedef struct {
  sc_pid_t pid;
  int32_t target_ramped_mm_s;
  int16_t applied_permille;
  int64_t requested_permille;
} sc_speed_controller_side_t;

typedef struct {
  sc_speed_controller_config_t config;
  sc_speed_controller_side_t left;
  sc_speed_controller_side_t right;
  uint32_t last_feedback_time_ms;
  uint32_t last_feedback_sequence;
  sc_speed_controller_mode_t mode;
  uint8_t feedback_initialized;
  uint8_t initialized;
} sc_speed_controller_t;

sc_speed_controller_status_t sc_speed_controller_init(
  sc_speed_controller_t *controller,
  const sc_speed_controller_config_t *config);

sc_speed_controller_status_t sc_speed_controller_set_mode(
  sc_speed_controller_t *controller, sc_speed_controller_mode_t mode,
  sc_speed_controller_output_t *output);

sc_speed_controller_status_t sc_speed_controller_reset(
  sc_speed_controller_t *controller,
  sc_speed_controller_output_t *output);

sc_speed_controller_status_t sc_speed_controller_stop(
  sc_speed_controller_t *controller,
  sc_speed_controller_output_t *output);

/**
  * @brief  使用新且有效的 Wheel Service 快照推进双轮速度控制。
  *         PID dt 取相邻有效反馈的 sample_time_ms 差；首帧只建立时序基线。
  *         反馈缺失、陈旧或任一侧无效时两路输出置零并返回 FEEDBACK。
  */
sc_speed_controller_status_t sc_speed_controller_step(
  sc_speed_controller_t *controller, int32_t left_target_mm_s,
  int32_t right_target_mm_s, const sc_wheel_feedback_t *feedback,
  uint32_t now_ms, sc_speed_controller_output_t *output);

#ifdef __cplusplus
}
#endif

#endif /* __SC_SPEED_CONTROLLER_H */
