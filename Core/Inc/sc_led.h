/**
  ******************************************************************************
  * @file    sc_led.h
  * @brief   状态 LED 驱动。
  *          接线：PB9 推挽输出 -> LED 阳极，LED 阴极 -> 510Ω -> GND，
  *          高电平点亮（文档 4.7）。最终亮灭语义由应用层决定（文档 8.1）。
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __SC_LED_H
#define __SC_LED_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

/* Exported types ------------------------------------------------------------*/
typedef enum {
  SC_LED_OK = 0
} sc_led_status_t;

/* Exported functions prototypes ---------------------------------------------*/
sc_led_status_t sc_led_init(void);
sc_led_status_t sc_led_set(uint8_t on);

#ifdef __cplusplus
}
#endif

#endif /* __SC_LED_H */
