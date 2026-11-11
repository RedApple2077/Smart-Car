/**
  ******************************************************************************
  * @file    sc_line_service.h
  * @brief   LF04 原始电平到黑线掩码和线路语义的纯软件转换。
  ******************************************************************************
  */

#ifndef __SC_LINE_SERVICE_H
#define __SC_LINE_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "sc_lf04.h"

typedef enum {
  SC_LINE_OK = 0,
  SC_LINE_ERR_ARGUMENT,
  SC_LINE_ERR_NOT_READY,
  SC_LINE_ERR_INVALID_DATA
} sc_line_status_t;

typedef enum {
  SC_LINE_INVALID = 0,
  SC_LINE_STRAIGHT,
  SC_LINE_LEFT_RIGHT_ANGLE_CANDIDATE,
  SC_LINE_RIGHT_RIGHT_ANGLE_CANDIDATE,
  SC_LINE_LEFT_LARGE_BEND_CANDIDATE,
  SC_LINE_RIGHT_LARGE_BEND_CANDIDATE,
  SC_LINE_LEFT_FINE_ADJUST_CANDIDATE,
  SC_LINE_RIGHT_FINE_ADJUST_CANDIDATE,
  SC_LINE_JUNCTION_CANDIDATE,
  SC_LINE_LOST_CANDIDATE,
  SC_LINE_UNKNOWN_PATTERN
} sc_line_class_t;

typedef struct {
  sc_line_sample_t sample;
  uint8_t black_mask;       /* bit0=O1..bit3=O4；检测到黑线为 1 */
  sc_line_class_t semantic_state;
} sc_line_state_t;

/**
  * @brief  将一份原始样本转换为黑线掩码和线路语义并发布缓存。
  *         无效数据也会覆盖缓存，避免保留上一帧有效分类。
  */
sc_line_status_t sc_line_service_update(const sc_line_sample_t *sample);

/**
  * @brief  复制最近发布的线路状态，不访问 GPIO。
  */
sc_line_status_t sc_line_service_get_latest(sc_line_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* __SC_LINE_SERVICE_H */
