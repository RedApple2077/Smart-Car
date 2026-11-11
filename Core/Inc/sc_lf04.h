/**
  ******************************************************************************
  * @file    sc_lf04.h
  * @brief   LF04 四路反射式循迹传感器采样驱动。
  ******************************************************************************
  */

#ifndef __SC_LF04_H
#define __SC_LF04_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
  SC_LF04_OK = 0,
  SC_LF04_ERR_ARGUMENT,
  SC_LF04_ERR_CONFIG,
  SC_LF04_ERR_NOT_READY
} sc_lf04_status_t;

typedef struct {
  uint8_t raw_levels;       /* bit0=O1..bit3=O4；0=黑线，1=白底 */
  uint32_t sample_time_ms;
  uint32_t sequence;        /* 成功采样序号，允许 uint32_t 回绕 */
  uint8_t valid;
} sc_line_sample_t;

typedef struct {
  uint8_t initialized;
  sc_lf04_status_t last_status;
  uint32_t config_failures;
  uint32_t sample_failures;
  uint32_t samples;
} sc_lf04_state_t;

/**
  * @brief  校验 CubeMX 已配置的 PB12..PB15 上拉输入并完成首次采样。
  *         不重复调用 HAL_GPIO_Init()；成功后重复调用保留快照和统计。
  */
sc_lf04_status_t sc_lf04_init(void);

/**
  * @brief  校验配置并读取一次 GPIOB->IDR，发布原始四路电平快照。
  *         成功样本推进序号；失败样本 valid=0，不推进成功序号。
  */
sc_lf04_status_t sc_lf04_sample(uint32_t now_ms, sc_line_sample_t *sample);

/**
  * @brief  复制最近快照，不重新读取 GPIO。
  */
sc_lf04_status_t sc_lf04_get_latest(sc_line_sample_t *sample);

/**
  * @brief  获取驱动状态与失败统计。
  */
sc_lf04_status_t sc_lf04_get_status(sc_lf04_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* __SC_LF04_H */
