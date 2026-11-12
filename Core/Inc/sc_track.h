/**
  ******************************************************************************
  * @file    sc_track.h
  * @brief   四路红外离散循迹的低速差速控制策略。
  ******************************************************************************
  */

#ifndef __SC_TRACK_H
#define __SC_TRACK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "sc_line_service.h"

typedef enum {
  SC_TRACK_OK = 0,
  SC_TRACK_RECOVERING,
  SC_TRACK_ERR_ARGUMENT,
  SC_TRACK_ERR_INVALID_LINE,
  SC_TRACK_ERR_LOST
} sc_track_status_t;

typedef struct {
  int16_t left_permille;
  int16_t right_permille;
} sc_track_command_t;

typedef struct {
  int8_t last_turn_direction; /* -1=left, 0=unknown, 1=right */
  uint32_t lost_since_ms;
  uint8_t recovering;
} sc_track_controller_t;

/** Reset remembered steering direction and pending loss recovery. */
void sc_track_controller_init(sc_track_controller_t *controller);

/**
 * @brief  根据已校验的 LF04 语义生成前进差速命令。
 *         短时丢线沿最后一次有效偏移方向低速找线；超时、未知或无效线型停止。
 */
sc_track_status_t sc_track_controller_step(sc_track_controller_t *controller,
                                           const sc_line_state_t *line,
                                           uint32_t now_ms,
                                           sc_track_command_t *command);

#ifdef __cplusplus
}
#endif

#endif /* __SC_TRACK_H */
