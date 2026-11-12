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
  uint8_t recovery_sample_count;
  uint8_t recovery_has_sequence;
  uint8_t recovering;
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
 *         短时丢线沿最后一次有效偏移方向低速找线；未知方向、超时、
 *         无效线型或未完成连续三帧重捕时停止或保持 recovering。
 *         重捕候选要求 sequence 新鲜、相邻样本间隔 1..30 ms，首末至少 20 ms；
 *         首次白帧的 1000 ms 总期限不重开。
 */
sc_track_status_t sc_track_controller_step(sc_track_controller_t *controller,
                                           const sc_line_state_t *line,
                                           uint32_t now_ms,
                                           sc_wheel_target_t *target_out);

#ifdef __cplusplus
}
#endif

#endif /* __SC_TRACK_H */
