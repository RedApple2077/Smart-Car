/**
  ******************************************************************************
  * @file    sc_tb6612.h
  * @brief   TB6612FNG 双路电机输出驱动。
  ******************************************************************************
  */

#ifndef __SC_TB6612_H
#define __SC_TB6612_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define SC_TB6612_COMMAND_MAX 1000

typedef enum {
  SC_TB6612_OK = 0,
  SC_TB6612_ERR_ARGUMENT,
  SC_TB6612_ERR_CONFIG,
  SC_TB6612_ERR_NOT_READY,
  SC_TB6612_ERR_BUSY,
  SC_TB6612_ERR_FAULT,
  SC_TB6612_ERR_HAL
} sc_tb6612_status_t;

typedef enum {
  SC_TB6612_MODE_UNINITIALIZED = 0,
  SC_TB6612_MODE_STANDBY,
  SC_TB6612_MODE_ENABLED,
  SC_TB6612_MODE_FAULT
} sc_tb6612_mode_t;

typedef struct {
  uint8_t initialized;
  uint8_t enabled;
  uint8_t standby;
  sc_tb6612_mode_t mode;
  sc_tb6612_status_t last_status;
  int16_t left_command;
  int16_t right_command;
  uint32_t left_ccr;
  uint32_t right_ccr;
  uint32_t rejected_requests;
  uint32_t pwm_start_failures;
  uint32_t rollback_count;
  uint32_t safe_stop_count;
  uint32_t update_failures;
  uint32_t last_hal_error;
} sc_tb6612_state_t;

/**
  * @brief  校验并使用 CubeMX 已初始化的 TIM4 双路 PWM。
  *         初始化后保持 STBY 为低。成功后的重复调用为幂等操作，
  *         不重新启动定时器或清除统计。
  */
sc_tb6612_status_t sc_tb6612_init(void);

/**
  * @brief  使能或撤销 TB6612 的 STBY。
  *         使能时先装载双路零命令高阻滑行状态，再释放 STBY。
  */
sc_tb6612_status_t sc_tb6612_set_standby(uint8_t enable);

/**
  * @brief  设置左右两路电气输出命令，范围均为 [-1000, 1000]。
  *         正值表示 IN1 高、IN2 低，负值相反；不表示装配后的车轮前进方向。
  *         仅允许在健康 ENABLED 状态调用。
  */
sc_tb6612_status_t sc_tb6612_apply(int16_t left_permille,
                                   int16_t right_permille);

/**
  * @brief  获取驱动状态快照。记录的是软件请求和寄存器值，不代表芯片
  *         实际输出、接线或电机负载已经得到验证。
  */
sc_tb6612_status_t sc_tb6612_get_state(sc_tb6612_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* __SC_TB6612_H */
