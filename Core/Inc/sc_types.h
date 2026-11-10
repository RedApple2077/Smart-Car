/**
  ******************************************************************************
  * @file    sc_types.h
  * @brief   跨层公共数据类型，对应《循迹小车硬件接线与软件API规划》7.2。
  *          这里只定义当前显示/后续服务需要的运行态类型；配置结构在
  *          Config Service 模块中再补充。
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __SC_TYPES_H
#define __SC_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

/* Exported types ------------------------------------------------------------*/
typedef enum {
  SC_SIDE_LEFT = 0,
  SC_SIDE_RIGHT = 1
} sc_side_t;

typedef struct {
  uint8_t raw_levels;       /* 低四位，bit0=O1..bit3=O4；0=黑，1=白 */
  uint32_t sample_time_ms;
  uint8_t valid;
} sc_line_sample_t;

typedef enum {
  SC_LINE_INVALID = 0,
  SC_LINE_STRAIGHT,
  SC_LINE_LEFT_RIGHT_ANGLE_CANDIDATE,    /* 左直角弯候选 */
  SC_LINE_RIGHT_RIGHT_ANGLE_CANDIDATE,   /* 右直角弯候选 */
  SC_LINE_LEFT_LARGE_BEND_CANDIDATE,     /* 左大弯候选 */
  SC_LINE_RIGHT_LARGE_BEND_CANDIDATE,    /* 右大弯候选 */
  SC_LINE_LEFT_FINE_ADJUST_CANDIDATE,    /* 左微调候选 */
  SC_LINE_RIGHT_FINE_ADJUST_CANDIDATE,   /* 右微调候选 */
  SC_LINE_JUNCTION_CANDIDATE,
  SC_LINE_LOST_CANDIDATE,
  SC_LINE_UNKNOWN_PATTERN
} sc_line_class_t;

typedef struct {
  sc_line_sample_t sample;
  uint8_t black_mask;       /* 低四位，bit0=O1..bit3=O4；检测到黑为1 */
  sc_line_class_t semantic_state;
  uint32_t sample_time_ms;
  uint8_t valid;
} sc_line_state_t;

typedef struct {
  int32_t left_count;
  int32_t right_count;
  int32_t left_delta;
  int32_t right_delta;
  int32_t left_speed_mm_s;
  int32_t right_speed_mm_s;
  uint32_t sample_time_ms;
  uint8_t valid;
} sc_wheel_feedback_t;

typedef struct {
  int32_t gyro_z_mdps;          /* Driver 为测量值；Service 输出时扣除零偏 */
  int32_t heading_delta_mdeg;   /* Driver 固定置 0；仅 Service 填写 */
  uint32_t sample_time_ms;
  uint8_t calibrated;
  uint8_t valid;
} sc_imu_sample_t;

typedef enum {
  SC_RUN_IDLE = 0,
  SC_RUN_READY,
  SC_RUN_ACTIVE,
  SC_RUN_STOPPING,
  SC_RUN_FAULT
} sc_run_phase_t;

typedef struct {
  uint32_t timestamp_ms;
  sc_run_phase_t phase;
  sc_line_state_t line;
  sc_wheel_feedback_t wheels;
  sc_imu_sample_t imu;
  int16_t left_pwm_permille;
  int16_t right_pwm_permille;
  uint16_t fault_bits;
  uint8_t actuator_enabled;
} sc_run_state_t;

#ifdef __cplusplus
}
#endif

#endif /* __SC_TYPES_H */
