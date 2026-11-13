#include "stm32f1xx.h"

RCC_TypeDef fake_rcc;
GPIO_TypeDef fake_gpio_a;
GPIO_TypeDef fake_gpio_b;
TIM_TypeDef fake_tim4;
IWDG_TypeDef fake_iwdg;

static uint32_t s_dsb_count;

void __DSB(void)
{
  s_dsb_count++;
}

void fake_safety_reset(void)
{
  fake_rcc.APB2ENR = 0x00A50000u;
  fake_rcc.APB1ENR = 0x005A0000u;
  fake_gpio_a.CRL = 0xFFFFFFFFu;
  fake_gpio_a.CRH = 0xFFFFFFFFu;
  fake_gpio_b.CRL = 0xFFFFFFFFu;
  fake_gpio_b.CRH = 0xFFFFFFFFu;
  fake_gpio_a.BRR = 0u;
  fake_gpio_b.BRR = 0u;
  fake_tim4.CCR1 = 123u;
  fake_tim4.CCR2 = 456u;
  fake_iwdg.KR = 0u;
  fake_iwdg.PR = 0x07u;
  fake_iwdg.RLR = 0x0FFFu;
  fake_iwdg.SR = 0u;
  s_dsb_count = 0u;
}

uint32_t fake_safety_dsb_count(void)
{
  return s_dsb_count;
}
