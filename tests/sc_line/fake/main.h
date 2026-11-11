#ifndef TEST_SC_LINE_FAKE_MAIN_H
#define TEST_SC_LINE_FAKE_MAIN_H

#include <stdint.h>

typedef struct {
  volatile uint32_t CRH;
  volatile uint32_t IDR;
  volatile uint32_t ODR;
} GPIO_TypeDef;

typedef struct {
  volatile uint32_t APB2ENR;
} RCC_TypeDef;

extern GPIO_TypeDef fake_gpio_b;
extern RCC_TypeDef fake_rcc;

#define GPIOB (&fake_gpio_b)
#define RCC (&fake_rcc)

#define RCC_APB2ENR_IOPBEN ((uint32_t)0x00000008u)

#define GPIO_PIN_12 ((uint16_t)0x1000u)
#define GPIO_PIN_13 ((uint16_t)0x2000u)
#define GPIO_PIN_14 ((uint16_t)0x4000u)
#define GPIO_PIN_15 ((uint16_t)0x8000u)

#define LF04_O1_Pin GPIO_PIN_12
#define LF04_O1_GPIO_Port GPIOB
#define LF04_O2_Pin GPIO_PIN_13
#define LF04_O2_GPIO_Port GPIOB
#define LF04_O3_Pin GPIO_PIN_14
#define LF04_O3_GPIO_Port GPIOB
#define LF04_O4_Pin GPIO_PIN_15
#define LF04_O4_GPIO_Port GPIOB

uint32_t HAL_GetTick(void);

#endif /* TEST_SC_LINE_FAKE_MAIN_H */
