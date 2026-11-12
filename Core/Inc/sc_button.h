/**
  ******************************************************************************
  * @file    sc_button.h
  * @brief   发车按键驱动。
  *          接线：PB5 内部上拉输入，按键另一端接 GND，按下为低（文档 4.7）。
  *          消抖、长按判定在 sc_button_poll() 中完成，不使用 GPIO 中断
  *          （文档 8.1）。驱动只输出事件，不启动电机。
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __SC_BUTTON_H
#define __SC_BUTTON_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

/* Exported constants --------------------------------------------------------*/
#define SC_BUTTON_DEBOUNCE_MS   20u    /* 消抖稳定窗口 */
#define SC_BUTTON_LONG_PRESS_MS 3000u  /* 长按判定门限，文档 8.1 要求至少 3 s */

/* Exported types ------------------------------------------------------------*/
typedef enum {
  SC_BUTTON_OK = 0
} sc_button_status_t;

/* sc_button_poll() 返回的事件位，可同时置位。 */
typedef enum {
  SC_BUTTON_EVENT_NONE       = 0,
  SC_BUTTON_EVENT_PRESSED    = (1u << 0),
  SC_BUTTON_EVENT_RELEASED   = (1u << 1),
  SC_BUTTON_EVENT_LONG_PRESS = (1u << 2)
} sc_button_event_t;

/*
 * 初始化时稳定态直接取当前原始电平。若上电时按键已经按下，随后第一次
 * 释放会返回 RELEASED 但没有前置 PRESSED；应用只能把一次
 * PRESSED -> RELEASED 序列识别为新的短按。
 */

/* Exported functions prototypes ---------------------------------------------*/
sc_button_status_t sc_button_init(void);
uint8_t sc_button_poll(uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* __SC_BUTTON_H */
