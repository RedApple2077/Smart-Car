/**
  ******************************************************************************
  * @file    sc_safety.c
  * @brief   寄存器级紧急停机实现。
  ******************************************************************************
  */

#include "sc_safety.h"

#include "stm32f1xx.h"

#define SC_SAFETY_GPIOA_LOW_PINS (GPIO_PIN_4 | GPIO_PIN_5)
#define SC_SAFETY_GPIOB_LOW_PINS \
  (GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_8)
#define SC_SAFETY_GPIO_CRL_OUTPUT_2MHZ 0x2u
#define SC_SAFETY_GPIO_NIBBLE_MASK     0xFu

void sc_safety_emergency_stop(void)
{
  uint32_t gpioa_crl_mask;
  uint32_t gpiob_crl_mask;
  uint32_t gpiob_crh_mask;

  /* 先开外设时钟再改输出锁存和模式，确保异常可发生在 GPIO 正常初始化前。 */
  RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN;
  RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;

  GPIOB->BRR = (uint32_t)SC_SAFETY_GPIOB_LOW_PINS;
  GPIOA->BRR = (uint32_t)SC_SAFETY_GPIOA_LOW_PINS;

  gpioa_crl_mask =
    (SC_SAFETY_GPIO_NIBBLE_MASK << 16u) |
    (SC_SAFETY_GPIO_NIBBLE_MASK << 20u);
  gpiob_crl_mask =
    (SC_SAFETY_GPIO_NIBBLE_MASK << 0u) |
    (SC_SAFETY_GPIO_NIBBLE_MASK << 4u);
  gpiob_crh_mask = SC_SAFETY_GPIO_NIBBLE_MASK << 0u;

  GPIOA->CRL =
    (GPIOA->CRL & ~gpioa_crl_mask) |
    (SC_SAFETY_GPIO_CRL_OUTPUT_2MHZ << 16u) |
    (SC_SAFETY_GPIO_CRL_OUTPUT_2MHZ << 20u);
  GPIOB->CRL =
    (GPIOB->CRL & ~gpiob_crl_mask) |
    (SC_SAFETY_GPIO_CRL_OUTPUT_2MHZ << 0u) |
    (SC_SAFETY_GPIO_CRL_OUTPUT_2MHZ << 4u);
  GPIOB->CRH =
    (GPIOB->CRH & ~gpiob_crh_mask) |
    (SC_SAFETY_GPIO_CRL_OUTPUT_2MHZ << 0u);

  TIM4->CCR1 = 0u;
  TIM4->CCR2 = 0u;
  __DSB();
}
