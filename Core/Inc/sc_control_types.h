/**
  ******************************************************************************
  * @file    sc_control_types.h
  * @brief   控制层共享的有符号轮目标类型。
  ******************************************************************************
  */

#ifndef __SC_CONTROL_TYPES_H
#define __SC_CONTROL_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct {
  int32_t left_mm_s;
  int32_t right_mm_s;
} sc_wheel_target_t;

#ifdef __cplusplus
}
#endif

#endif /* __SC_CONTROL_TYPES_H */
