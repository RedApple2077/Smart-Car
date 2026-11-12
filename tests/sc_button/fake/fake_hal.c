#include "main.h"

#include <stddef.h>

GPIO_TypeDef fake_gpio_b;

static uint32_t s_tick;
static GPIO_PinState s_level;
static uint32_t s_init_count;

uint32_t HAL_GetTick(void)
{
  return s_tick;
}

void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init)
{
  if ((port != NULL) && (init != NULL))
  {
    s_init_count++;
  }
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
  if ((port != START_BTN_GPIO_Port) || (pin != START_BTN_Pin))
  {
    return GPIO_PIN_SET;
  }
  return s_level;
}

void fake_button_reset(void)
{
  s_tick = 0u;
  s_level = GPIO_PIN_SET;
  s_init_count = 0u;
}

void fake_button_set_tick(uint32_t tick)
{
  s_tick = tick;
}

void fake_button_set_level(GPIO_PinState level)
{
  s_level = level;
}

uint32_t fake_button_init_count(void)
{
  return s_init_count;
}
