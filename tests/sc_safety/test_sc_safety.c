#include "sc_safety.h"
#include "stm32f1xx.h"

#include <stdio.h>
#include <string.h>

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

static int test_iwdg_configuration_and_feed(void)
{
  fake_safety_reset();
  CHECK(sc_safety_iwdg_init() == SC_SAFETY_OK);
  CHECK(fake_iwdg.PR == 0x04u);
  CHECK(fake_iwdg.RLR == 312u);
  CHECK(fake_iwdg.KR == 0xAAAAu);

  fake_iwdg.KR = 0u;
  sc_safety_iwdg_feed();
  CHECK(fake_iwdg.KR == 0xAAAAu);
  fake_iwdg.KR = 0u;
  CHECK(sc_safety_iwdg_init() == SC_SAFETY_OK);
  CHECK(fake_iwdg.KR == 0xAAAAu);
  return 0;
}

static int test_iwdg_timeout(void)
{
  fake_safety_reset();
  fake_iwdg.SR = IWDG_SR_PVU | IWDG_SR_RVU;

  CHECK(sc_safety_iwdg_init() == SC_SAFETY_ERR_IWDG_TIMEOUT);
  CHECK(fake_iwdg.KR == 0xAAAAu);
  CHECK(fake_iwdg.PR == 0x04u);
  CHECK(fake_iwdg.RLR == 312u);
  return 0;
}

typedef struct {
  const char *name;
  int (*run)(void);
} test_case_t;

static const test_case_t s_tests[] = {
  {"emergency_stop_direct_registers",
   test_emergency_stop_direct_registers},
  {"iwdg_configuration_and_feed", test_iwdg_configuration_and_feed},
  {"iwdg_timeout", test_iwdg_timeout}
};

int main(int argc, char **argv)
{
  size_t index;

  if (argc != 2)
  {
    return 2;
  }
  for (index = 0u; index < (sizeof(s_tests) / sizeof(s_tests[0])); index++)
  {
    if (strcmp(argv[1], s_tests[index].name) == 0)
    {
      return s_tests[index].run();
    }
  }
  return 2;
}
