/**
  ******************************************************************************
  * @file    sc_led.c
  * @brief   状态 LED 驱动实现。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "sc_led.h"
#include "main.h"

/* Exported functions --------------------------------------------------------*/
sc_status_t sc_led_init(void)
{
  GPIO_InitTypeDef gpio = {0};

  gpio.Pin = STATUS_LED_Pin;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(STATUS_LED_GPIO_Port, &gpio);

  /* 上电默认熄灭，等应用层具备状态提示能力后再点亮（文档 8.1）。 */
  return sc_led_set(0u);
}

sc_status_t sc_led_set(uint8_t on)
{
  HAL_GPIO_WritePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin,
                    (on != 0u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  return SC_OK;
}
