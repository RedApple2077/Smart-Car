/**
  ******************************************************************************
  * @file    sc_safety.h
  * @brief   不依赖 HAL 运行状态或句柄的紧急停机原语。
  ******************************************************************************
  */

#ifndef __SC_SAFETY_H
#define __SC_SAFETY_H

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief  直接关闭 TB6612 输出并清除 TIM4 两路比较值。
  *         该函数只访问 CMSIS 定义的 RCC、GPIO 和 TIM 寄存器，可由
  *         Error_Handler 及 NMI/HardFault/MemManage/BusFault/UsageFault
  *         在最早期调用；执行后不依赖 HAL 句柄或软件状态。
  */
void sc_safety_emergency_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* __SC_SAFETY_H */
