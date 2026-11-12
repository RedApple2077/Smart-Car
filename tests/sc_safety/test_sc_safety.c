#include "sc_safety.h"
#include "stm32f1xx.h"

#include <stdio.h>

#define CHECK(expression)                                                     \
  do                                                                          \
  {                                                                           \
    if (!(expression))                                                        \
    {                                                                         \
      fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__,     \
              #expression);                                                   \
      return 1;                                                               \
    }                                                                         \
  } while (0)

static int test_emergency_stop_direct_registers(void)
{
  uint32_t expected_apb2;
  uint32_t expected_apb1;

  fake_safety_reset();
  expected_apb2 = fake_rcc.APB2ENR |
                  RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN;
  expected_apb1 = fake_rcc.APB1ENR | RCC_APB1ENR_TIM4EN;

  sc_safety_emergency_stop();

  CHECK(fake_rcc.APB2ENR == expected_apb2);
  CHECK(fake_rcc.APB1ENR == expected_apb1);
  CHECK(fake_gpio_a.BRR ==
        (uint32_t)(GPIO_PIN_4 | GPIO_PIN_5));
  CHECK(fake_gpio_b.BRR ==
        (uint32_t)(GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_8));
  CHECK((fake_gpio_a.CRL & 0x00FF0000u) == 0x00220000u);
  CHECK((fake_gpio_a.CRL & 0xFF00FFFFu) == 0xFF00FFFFu);
  CHECK((fake_gpio_b.CRL & 0x000000FFu) == 0x00000022u);
  CHECK((fake_gpio_b.CRL & 0xFFFFFF00u) == 0xFFFFFF00u);
  CHECK((fake_gpio_b.CRH & 0x0000000Fu) == 0x00000002u);
  CHECK((fake_gpio_b.CRH & 0xFFFFFFF0u) == 0xFFFFFFF0u);
  CHECK(fake_tim4.CCR1 == 0u);
  CHECK(fake_tim4.CCR2 == 0u);
  CHECK(fake_safety_dsb_count() == 1u);
  return 0;
}

int main(void)
{
  return test_emergency_stop_direct_registers();
}
