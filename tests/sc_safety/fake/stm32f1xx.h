#ifndef TEST_FAKE_STM32F1XX_H
#define TEST_FAKE_STM32F1XX_H

#include <stdint.h>

#define RCC_APB2ENR_IOPAEN (1u << 2)
#define RCC_APB2ENR_IOPBEN (1u << 3)
#define RCC_APB1ENR_TIM4EN (1u << 2)

#define GPIO_PIN_0 ((uint16_t)0x0001u)
#define GPIO_PIN_1 ((uint16_t)0x0002u)
#define GPIO_PIN_4 ((uint16_t)0x0010u)
#define GPIO_PIN_5 ((uint16_t)0x0020u)
#define GPIO_PIN_8 ((uint16_t)0x0100u)

typedef struct {
  volatile uint32_t APB2ENR;
  volatile uint32_t APB1ENR;
} RCC_TypeDef;

typedef struct {
  volatile uint32_t CRL;
  volatile uint32_t CRH;
  volatile uint32_t IDR;
  volatile uint32_t ODR;
  volatile uint32_t BSRR;
  volatile uint32_t BRR;
  volatile uint32_t LCKR;
} GPIO_TypeDef;

typedef struct {
  volatile uint32_t CCR1;
  volatile uint32_t CCR2;
} TIM_TypeDef;

typedef struct {
  volatile uint32_t KR;
  volatile uint32_t PR;
  volatile uint32_t RLR;
  volatile uint32_t SR;
} IWDG_TypeDef;

#define IWDG_SR_PVU (1u << 0)
#define IWDG_SR_RVU (1u << 1)

extern RCC_TypeDef fake_rcc;
extern GPIO_TypeDef fake_gpio_a;
extern GPIO_TypeDef fake_gpio_b;
extern TIM_TypeDef fake_tim4;
extern IWDG_TypeDef fake_iwdg;

#define RCC (&fake_rcc)
#define GPIOA (&fake_gpio_a)
#define GPIOB (&fake_gpio_b)
#define TIM4 (&fake_tim4)
#define IWDG (&fake_iwdg)

void __DSB(void);

void fake_safety_reset(void);
uint32_t fake_safety_dsb_count(void);

#endif /* TEST_FAKE_STM32F1XX_H */
