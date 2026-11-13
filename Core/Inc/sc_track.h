/**
  ******************************************************************************
  * @file    sc_track.h
  * @brief   四路红外离散循迹的有符号轮速目标策略。
  ******************************************************************************
  */

#ifndef __SC_TRACK_H
#define __SC_TRACK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "sc_control_types.h"
#include "sc_line_service.h"

typedef enum {
  SC_TRACK_OK = 0,
  SC_TRACK_RECOVERING,
  SC_TRACK_ERR_ARGUMENT,
  SC_TRACK_ERR_INVALID_LINE,
  SC_TRACK_ERR_LOST
} sc_track_status_t;

typedef struct {
  int32_t cruise_mm_s;
  int8_t last_turn_direction; /* -1=left, 0=unknown, 1=right */
  uint32_t lost_since_ms;
  uint32_t recovery_first_sample_time_ms;
  uint32_t recovery_last_sample_time_ms;
  uint32_t recovery_last_sequence;
  uint32_t crossing_start_ms;
  uint32_t crossing_entry_last_sample_time_ms;
  uint32_t crossing_entry_last_sequence;
  uint32_t crossing_center_first_sample_time_ms;
  uint32_t crossing_center_last_sample_time_ms;
  uint32_t crossing_center_last_sequence;
  uint32_t crossing_ordinary_last_sample_time_ms;
  uint32_t crossing_ordinary_last_sequence;
  uint32_t crossing_white_start_ms;
  uint32_t crossing_cooldown_start_ms;
  uint8_t recovery_sample_count;
  uint8_t recovery_has_sequence;
  uint8_t recovering;
  uint8_t crossing_entry_sample_count;
  uint8_t crossing_entry_has_sequence;
  uint8_t crossing_active;
  uint8_t crossing_center_sample_count;
  uint8_t crossing_center_has_sequence;
  uint8_t crossing_ordinary_sample_count;
  uint8_t crossing_ordinary_has_sequence;
  uint8_t crossing_ordinary_last_mask;
  uint8_t crossing_white_active;
  uint8_t crossing_cooldown_active;
} sc_track_controller_t;

/**
  * @brief  以指定巡航速度初始化策略并清除方向、丢线与重捕状态。
  */
void sc_track_controller_init(sc_track_controller_t *controller,
                              int32_t cruise_mm_s);

/**
  * @brief  发车资格：仅非空、有效 sample 且 black_mask==0x06 时返回 1。
  */
uint8_t sc_track_start_eligible(const sc_line_state_t *line);

/**
 * @brief  根据 LF04 掩码生成左右有符号轮速目标。
 *         普通已知方向全白时沿最后一次有效偏移方向持续低速找线，不设
 *         超时；未知方向立即 lost，无效线型立即报错。
 *         重捕候选要求 sequence 新鲜、相邻样本间隔 1..30 ms，首末至少 20 ms；
 *         重捕期间 junction 不参与候选且不重开首次丢线时间戳。
 *         十字候选要求连续三帧 junction、sequence 新鲜且相邻间隔 1..30 ms；
 *         未确认时输出中心目标；确认后固定最多直行 500 ms，连续三帧中心
 *         或普通样本才恢复普通策略，十字内全白超过 100 ms 立即停车。
 */
sc_track_status_t sc_track_controller_step(sc_track_controller_t *controller,
                                           const sc_line_state_t *line,
                                           uint32_t now_ms,
                                           sc_wheel_target_t *target_out);

#ifdef __cplusplus
}
#endif

#endif /* __SC_TRACK_H */
