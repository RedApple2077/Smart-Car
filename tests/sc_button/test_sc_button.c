#include "main.h"
#include "sc_button.h"

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

static int test_initial_held_release_has_no_press(void)
{
  fake_button_reset();
  fake_button_set_level(GPIO_PIN_RESET);
  fake_button_set_tick(0u);

  CHECK(sc_button_init() == SC_BUTTON_OK);
  CHECK(fake_button_init_count() == 1u);
  CHECK(sc_button_poll(0u) == SC_BUTTON_EVENT_NONE);
  CHECK(sc_button_poll(20u) == SC_BUTTON_EVENT_NONE);
  CHECK(sc_button_poll(5000u) == SC_BUTTON_EVENT_NONE);

  fake_button_set_level(GPIO_PIN_SET);
  CHECK(sc_button_poll(5000u) == SC_BUTTON_EVENT_NONE);
  CHECK(sc_button_poll(5019u) == SC_BUTTON_EVENT_NONE);
  CHECK(sc_button_poll(5020u) == SC_BUTTON_EVENT_RELEASED);
  CHECK(sc_button_poll(5021u) == SC_BUTTON_EVENT_NONE);
  return 0;
}

static int test_press_release_sequence(void)
{
  fake_button_reset();
  fake_button_set_tick(0u);

  CHECK(sc_button_init() == SC_BUTTON_OK);
  CHECK(sc_button_poll(20u) == SC_BUTTON_EVENT_NONE);

  fake_button_set_level(GPIO_PIN_RESET);
  CHECK(sc_button_poll(30u) == SC_BUTTON_EVENT_NONE);
  CHECK(sc_button_poll(49u) == SC_BUTTON_EVENT_NONE);
  CHECK(sc_button_poll(50u) == SC_BUTTON_EVENT_PRESSED);

  fake_button_set_level(GPIO_PIN_SET);
  CHECK(sc_button_poll(60u) == SC_BUTTON_EVENT_NONE);
  CHECK(sc_button_poll(79u) == SC_BUTTON_EVENT_NONE);
  CHECK(sc_button_poll(80u) == SC_BUTTON_EVENT_RELEASED);
  return 0;
}

static int test_long_press_emitted_once(void)
{
  uint8_t events;

  fake_button_reset();
  CHECK(sc_button_init() == SC_BUTTON_OK);

  fake_button_set_level(GPIO_PIN_RESET);
  CHECK(sc_button_poll(30u) == SC_BUTTON_EVENT_NONE);
  CHECK(sc_button_poll(50u) == SC_BUTTON_EVENT_PRESSED);
  CHECK(sc_button_poll(3049u) == SC_BUTTON_EVENT_NONE);
  CHECK(sc_button_poll(3050u) == SC_BUTTON_EVENT_LONG_PRESS);
  CHECK(sc_button_poll(3051u) == SC_BUTTON_EVENT_NONE);

  fake_button_set_level(GPIO_PIN_SET);
  CHECK(sc_button_poll(3100u) == SC_BUTTON_EVENT_NONE);
  events = sc_button_poll(3120u);
  CHECK((events & SC_BUTTON_EVENT_RELEASED) != 0u);
  CHECK((events & SC_BUTTON_EVENT_LONG_PRESS) == 0u);
  return 0;
}

typedef struct {
  const char *name;
  int (*run)(void);
} test_case_t;

static const test_case_t s_tests[] = {
  {"initial_held_release_has_no_press",
   test_initial_held_release_has_no_press},
  {"press_release_sequence", test_press_release_sequence},
  {"long_press_emitted_once", test_long_press_emitted_once}
};

int main(int argc, char **argv)
{
  size_t index;

  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <test-case>\n", argv[0]);
    return 2;
  }

  for (index = 0u; index < (sizeof(s_tests) / sizeof(s_tests[0])); index++)
  {
    if (strcmp(argv[1], s_tests[index].name) == 0)
    {
      return s_tests[index].run();
    }
  }

  fprintf(stderr, "unknown test case: %s\n", argv[1]);
  return 2;
}
