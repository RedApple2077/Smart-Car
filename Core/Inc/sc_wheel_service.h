/**
  ******************************************************************************
  * @file    sc_wheel_service.h
  * @brief   双编码器累计计数到有符号轮速的窗口服务。
  ******************************************************************************
  */

#ifndef __SC_WHEEL_SERVICE_H
#define __SC_WHEEL_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "sc_encoder.h"

#define SC_WHEEL_WINDOW_SAMPLES        5u
#define SC_WHEEL_HISTORY_SAMPLES       (SC_WHEEL_WINDOW_SAMPLES + 1u)
#define SC_WHEEL_MAX_SNAPSHOT_AGE_MS   30u

typedef struct {
  int8_t left_encoder_sign;
  int8_t right_encoder_sign;
  uint32_t left_counts_per_rev;
  uint32_t right_counts_per_rev;
  int32_t left_wheel_diameter_um;
  int32_t right_wheel_diameter_um;
} sc_wheel_calibration_t;

typedef struct {
  int64_t left_total_count;
  int64_t right_total_count;
  int64_t left_window_delta_count;
  int64_t right_window_delta_count;
  uint16_t left_raw_count;
  uint16_t right_raw_count;
  int32_t left_speed_mm_s;
  int32_t right_speed_mm_s;
  uint32_t sample_time_ms;
  uint32_t sequence;
  uint32_t left_window_span_ms;
  uint32_t right_window_span_ms;
  uint8_t left_valid;
  uint8_t right_valid;
  uint8_t valid;
} sc_wheel_feedback_t;

typedef enum {
  SC_WHEEL_SERVICE_OK = 0,
  SC_WHEEL_SERVICE_WARMUP,
  SC_WHEEL_SERVICE_ERR_ARGUMENT,
  SC_WHEEL_SERVICE_ERR_CALIBRATION,
  SC_WHEEL_SERVICE_ERR_NOT_READY,
  SC_WHEEL_SERVICE_ERR_DUPLICATE_SEQUENCE,
  SC_WHEEL_SERVICE_ERR_RESET_FRAME,
  SC_WHEEL_SERVICE_ERR_STALE,
  SC_WHEEL_SERVICE_ERR_SNAPSHOT,
  SC_WHEEL_SERVICE_ERR_LOST_SYNC
} sc_wheel_service_status_t;

typedef struct {
  int64_t total_count[SC_WHEEL_HISTORY_SAMPLES];
  uint32_t sample_time_ms[SC_WHEEL_HISTORY_SAMPLES];
  uint8_t next_index;
  uint8_t sample_count;
} sc_wheel_window_t;

typedef struct {
  sc_wheel_calibration_t calibration;
  sc_wheel_window_t left;
  sc_wheel_window_t right;
  uint32_t last_sequence;
  uint32_t last_sample_time_ms;
  uint8_t has_snapshot;
  uint8_t initialized;
} sc_wheel_service_t;

/**
  * @brief  校验并保存左右编码器标定，清除窗口状态。
  */
sc_wheel_service_status_t sc_wheel_service_init(
  sc_wheel_service_t *service, const sc_wheel_calibration_t *calibration);

/**
  * @brief  推入一个编码器快照并生成约 50 ms 窗口速度。
  *         窗口由一个基线快照和随后 5 个非零时差有效快照组成。
  *         任一输出侧仅在自身窗口完整时置 valid；overall valid 要求左右均有效。
  */
sc_wheel_service_status_t sc_wheel_service_update(
  sc_wheel_service_t *service, const sc_encoder_snapshot_t *snapshot,
  const sc_wheel_calibration_t *calibration, uint32_t now_ms,
  sc_wheel_feedback_t *feedback);

#ifdef __cplusplus
}
#endif

#endif /* __SC_WHEEL_SERVICE_H */
