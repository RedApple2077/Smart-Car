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

typedef enum {
  SC_SAFETY_OK = 0,
  SC_SAFETY_ERR_IWDG_TIMEOUT /* PR/RLR 更新位在有限等待内未清零 */
} sc_safety_status_t;

/*
 * STM32F1 LSI 名义频率 40 kHz；IWDG /64、重装 312：
 * 312 * 64 / 40000 = 0.4992 s 名义期限。实际期限随 LSI 容差变化。
 * 初始化先启动 IWDG 以开启 LSI，再配置 PR/RLR，并有限等待更新完成。
 */
sc_safety_status_t sc_safety_iwdg_init(void);
void sc_safety_iwdg_feed(void);

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
