/**
  ******************************************************************************
  * @file    sc_pid.h
  * @brief   并联式 PID，输单位为 PWM 千分比。
  *
  *          kp_milli: PWM 千分比 / (mm/s)
  *          ki_milli: PWM 千分比 / mm
  *          kd_milli: PWM 千分比 / (mm/s^2)
  *
  *          控制器使用实际 dt_ms，微分作用于测量值。计算与提交分为两步，
  *          使调用方能在斜坡和限幅得到实际提交值后执行条件抗饱和。
  ******************************************************************************
  */

#ifndef __SC_PID_H
#define __SC_PID_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
  SC_PID_OK = 0,
  SC_PID_ERR_ARGUMENT
} sc_pid_status_t;

typedef struct {
  int32_t kp_milli;
  int32_t ki_milli;
  int32_t kd_milli;
} sc_pid_gains_t;

typedef struct {
  sc_pid_gains_t gains;
  int64_t integral_error_ms;
  int32_t previous_measurement_mm_s;
  uint8_t initialized;
} sc_pid_t;

typedef struct {
  int32_t error_mm_s;
  int32_t measurement_mm_s;
  uint32_t dt_ms;
  int64_t p_term_permille;
  int64_t i_term_permille;
  int64_t d_term_permille;
  int64_t candidate_integral_error_ms;
  int64_t unbounded_output_permille;
  uint8_t clear_history;
} sc_pid_work_t;

/**
  * @brief  配置增益并清除积分、微分历史。
  */
sc_pid_status_t sc_pid_init(sc_pid_t *pid, const sc_pid_gains_t *gains);

/**
  * @brief  保留增益，清除积分与微分历史。
  */
void sc_pid_reset(sc_pid_t *pid);

/**
  * @brief  根据目标、测量值和实际 dt 计算未限幅 PID 输出。
  *         target_mm_s=0 时立即清除该轮积分与微分历史。
  */
sc_pid_status_t sc_pid_calculate(sc_pid_t *pid, int32_t target_mm_s,
                                 int32_t measurement_mm_s, uint32_t dt_ms,
                                 sc_pid_work_t *work);

/**
  * @brief  提交本轮历史和基于实际提交值的条件抗饱和结果。
  *         requested_total_permille 是限幅/斜坡前的总请求，包含前馈；
  *         actual_output_permille 是斜坡、限幅后的最终提交值。
  *         当实际值未达到请求值且积分变化会继续推向该方向时，冻结积分。
  */
sc_pid_status_t sc_pid_commit(sc_pid_t *pid, const sc_pid_work_t *work,
                              int64_t requested_total_permille,
                              int16_t actual_output_permille,
                              int16_t output_min_permille,
                              int16_t output_max_permille);

#ifdef __cplusplus
}
#endif

#endif /* __SC_PID_H */
