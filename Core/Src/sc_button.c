/**
  ******************************************************************************
  * @file    sc_button.c
  * @brief   发车按键驱动实现：轮询采样、消抖和事件产生。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "sc_button.h"
#include "main.h"

/* Private types -------------------------------------------------------------*/
typedef struct {
  uint8_t raw_pressed;      /* 最近一次原始采样：1=按下（PB5 为低） */
  uint8_t stable_pressed;   /* 消抖后的稳定状态 */
  uint8_t long_reported;    /* 本次按住是否已上报长按 */
  uint32_t raw_change_ms;   /* 原始电平最近一次变化的时刻 */
  uint32_t press_ms;        /* 消抖后按下的时刻 */
} sc_button_ctx_t;

/* Private variables ---------------------------------------------------------*/
static sc_button_ctx_t s_button;

/* Private functions ---------------------------------------------------------*/
static uint8_t sc_button_read_pressed(void)
{
  return (HAL_GPIO_ReadPin(START_BTN_GPIO_Port, START_BTN_Pin) == GPIO_PIN_RESET)
             ? 1u
             : 0u;
}

/* Exported functions --------------------------------------------------------*/
sc_button_status_t sc_button_init(void)
{
  GPIO_InitTypeDef gpio = {0};

  gpio.Pin = START_BTN_Pin;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(START_BTN_GPIO_Port, &gpio);

  s_button.raw_pressed = sc_button_read_pressed();
  s_button.stable_pressed = 0u;
  s_button.long_reported = 0u;
  s_button.raw_change_ms = HAL_GetTick();
  s_button.press_ms = s_button.raw_change_ms;

  return SC_BUTTON_OK;
}

uint8_t sc_button_poll(uint32_t now_ms)
{
  uint8_t events = (uint8_t)SC_BUTTON_EVENT_NONE;
  uint8_t pressed = sc_button_read_pressed();

  /* 原始电平变化：重新开始消抖计时。 */
  if (pressed != s_button.raw_pressed)
  {
    s_button.raw_pressed = pressed;
    s_button.raw_change_ms = now_ms;
    return events;
  }

  /* 电平尚未稳定到消抖窗口。 */
  if ((uint32_t)(now_ms - s_button.raw_change_ms) < SC_BUTTON_DEBOUNCE_MS)
  {
    return events;
  }

  /* 稳定电平与上次不同：产生按下或释放事件。 */
  if (s_button.raw_pressed != s_button.stable_pressed)
  {
    s_button.stable_pressed = s_button.raw_pressed;

    if (s_button.stable_pressed != 0u)
    {
      s_button.press_ms = now_ms;
      s_button.long_reported = 0u;
      events |= (uint8_t)SC_BUTTON_EVENT_PRESSED;
    }
    else
    {
      s_button.long_reported = 0u;
      events |= (uint8_t)SC_BUTTON_EVENT_RELEASED;
    }
  }

  /* 按住达到门限后只上报一次长按。 */
  if ((s_button.stable_pressed != 0u) && (s_button.long_reported == 0u) &&
      ((uint32_t)(now_ms - s_button.press_ms) >= SC_BUTTON_LONG_PRESS_MS))
  {
    s_button.long_reported = 1u;
    events |= (uint8_t)SC_BUTTON_EVENT_LONG_PRESS;
  }

  return events;
}
