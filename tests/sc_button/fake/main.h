#ifndef TEST_FAKE_MAIN_H
#define TEST_FAKE_MAIN_H

#include <stdint.h>

typedef enum {
  GPIO_PIN_RESET = 0,
  GPIO_PIN_SET = 1
} GPIO_PinState;

typedef struct {
  uint32_t unused;
} GPIO_TypeDef;

typedef struct {
  uint32_t Pin;
  uint32_t Mode;
  uint32_t Pull;
  uint32_t Speed;
} GPIO_InitTypeDef;

#define GPIO_MODE_INPUT 0x00000000u
#define GPIO_PULLUP     0x00000001u

extern GPIO_TypeDef fake_gpio_b;

#define START_BTN_Pin ((uint16_t)0x0020u)
#define START_BTN_GPIO_Port (&fake_gpio_b)

uint32_t HAL_GetTick(void);
void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);

void fake_button_reset(void);
void fake_button_set_tick(uint32_t tick);
void fake_button_set_level(GPIO_PinState level);
uint32_t fake_button_init_count(void);

#endif /* TEST_FAKE_MAIN_H */
