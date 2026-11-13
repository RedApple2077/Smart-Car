#include "fake_telemetry.h"
#include "sc_telemetry.h"

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

static sc_app_snapshot_t snapshot(sc_app_phase_t phase)
{
  sc_app_snapshot_t value = {0};

  value.phase = phase;
  value.line.sample.raw_levels = 0x09u;
  value.line.sample.valid = 1u;
  value.line.black_mask = 0x06u;
  value.wheel.left_speed_mm_s = 120;
  value.wheel.right_speed_mm_s = 121;
  value.target.left_mm_s = 396;
  value.target.right_mm_s = 396;
  value.speed_output.left_permille = 220;
  value.speed_output.right_permille = 221;
  value.left_pwm = -220;
  value.right_pwm = 221;
  value.software_stby = 1u;
  return value;
}

static int test_uart_rate_and_csv_frame(void)
{
  sc_app_snapshot_t app_snapshot = snapshot(SC_APP_PHASE_ACTIVE);
  uint32_t degraded = 0u;
  const char *frame;
  const char *cursor;
  size_t fields = 1u;

  fake_telemetry_reset();
  sc_telemetry_init(0u);
  CHECK(sc_telemetry_service(&app_snapshot, 0u, &degraded) ==
        SC_TELEMETRY_OK);
  CHECK(fake_uart_call_count() == 1u);
  CHECK(degraded == 0u);
  frame = fake_uart_last();
  CHECK(strstr(frame, "0,ACTIVE,9,6,396,396,120,121,-220,221,1,") != NULL);
  for (cursor = frame; *cursor != '\0'; cursor++)
  {
    if (*cursor == ',')
    {
      fields++;
    }
  }
  CHECK(fields == 13u);

  CHECK(sc_telemetry_service(&app_snapshot, 199u, &degraded) ==
        SC_TELEMETRY_OK);
  CHECK(fake_uart_call_count() == 1u);
  CHECK(sc_telemetry_service(&app_snapshot, 200u, &degraded) ==
        SC_TELEMETRY_OK);
  CHECK(fake_uart_call_count() == 2u);
  return 0;
}

static int test_uart_failure_only_degrades(void)
{
  sc_app_snapshot_t app_snapshot = snapshot(SC_APP_PHASE_READY);
  uint32_t degraded = 0u;

  fake_telemetry_reset();
  sc_telemetry_init(0u);
  fake_uart_fail_next();
  CHECK(sc_telemetry_service(&app_snapshot, 0u, &degraded) ==
        SC_TELEMETRY_ERR_UART);
  CHECK((degraded & SC_APP_DEGRADED_TELEMETRY) != 0u);
  CHECK(fake_uart_call_count() == 1u);
  CHECK(sc_telemetry_service(&app_snapshot, 100u, &degraded) ==
        SC_TELEMETRY_OK);
  CHECK(fake_uart_call_count() == 1u);
  return 0;
}

static int test_oled_rate_and_led(void)
{
  sc_app_snapshot_t app_snapshot = snapshot(SC_APP_PHASE_IDLE);
  uint32_t degraded = 0u;

  fake_telemetry_reset();
  sc_telemetry_init(0u);
  CHECK(sc_telemetry_service(&app_snapshot, 0u, &degraded) ==
        SC_TELEMETRY_OK);
  CHECK(fake_oled_write_count() == 8u);
  CHECK(fake_oled_refresh_count() == 1u);
  CHECK(strcmp(fake_oled_line(0u), "P:IDLE") == 0);
  CHECK(fake_led_last() == 0u);

  app_snapshot.phase = SC_APP_PHASE_ACTIVE;
  (void)sc_telemetry_service(&app_snapshot, 250u, &degraded);
  CHECK(fake_oled_refresh_count() == 1u);
  CHECK(fake_led_last() == 1u);

  (void)sc_telemetry_service(&app_snapshot, 500u, &degraded);
  CHECK(fake_oled_write_count() == 16u);
  CHECK(fake_oled_refresh_count() == 2u);
  CHECK(fake_oled_service_count() == 3u);
  return 0;
}

static int test_display_failure_degrades(void)
{
  sc_app_snapshot_t app_snapshot = snapshot(SC_APP_PHASE_FAULT);
  uint32_t degraded = 0u;

  fake_telemetry_reset();
  sc_telemetry_init(0u);
  fake_oled_fail_next_write();
  CHECK(sc_telemetry_service(&app_snapshot, 0u, &degraded) ==
        SC_TELEMETRY_ERR_DISPLAY);
  CHECK((degraded & SC_APP_DEGRADED_DISPLAY) != 0u);
  CHECK(fake_led_last() == 0u);
  return 0;
}

static int test_async_display_failure_degrades(void)
{
  sc_app_snapshot_t app_snapshot = snapshot(SC_APP_PHASE_ACTIVE);
  uint32_t degraded = 0u;

  fake_telemetry_reset();
  sc_telemetry_init(0u);
  fake_oled_fail_next_service();
  CHECK(sc_telemetry_service(&app_snapshot, 0u, &degraded) ==
        SC_TELEMETRY_ERR_DISPLAY);
  CHECK((degraded & SC_APP_DEGRADED_DISPLAY) != 0u);
  CHECK(fake_oled_status()->retry_count == 1u);
  CHECK(fake_oled_status()->last_status == SC_SSD1306_ERR_BUS);
  return 0;
}

static int test_arguments(void)
{
  sc_app_snapshot_t app_snapshot = snapshot(SC_APP_PHASE_IDLE);
  uint32_t degraded;

  CHECK(sc_telemetry_service(NULL, 0u, &degraded) ==
        SC_TELEMETRY_ERR_ARGUMENT);
  CHECK(sc_telemetry_service(&app_snapshot, 0u, NULL) ==
        SC_TELEMETRY_ERR_ARGUMENT);
  return 0;
}

static int test_led_phase_patterns(void)
{
  sc_app_snapshot_t app_snapshot = snapshot(SC_APP_PHASE_READY);
  uint32_t degraded = 0u;

  fake_telemetry_reset();
  sc_telemetry_init(0u);
  CHECK(sc_telemetry_service(&app_snapshot, 499u, &degraded) ==
        SC_TELEMETRY_OK);
  CHECK(fake_led_last() == 0u);
  CHECK(sc_telemetry_service(&app_snapshot, 500u, &degraded) ==
        SC_TELEMETRY_OK);
  CHECK(fake_led_last() == 1u);
  CHECK(sc_telemetry_service(&app_snapshot, 1000u, &degraded) ==
        SC_TELEMETRY_OK);
  CHECK(fake_led_last() == 0u);

  app_snapshot.phase = SC_APP_PHASE_FAULT;
  CHECK(sc_telemetry_service(&app_snapshot, 1100u, &degraded) ==
        SC_TELEMETRY_OK);
  CHECK(fake_led_last() == 1u);
  CHECK(sc_telemetry_service(&app_snapshot, 1200u, &degraded) ==
        SC_TELEMETRY_OK);
  CHECK(fake_led_last() == 0u);
  return 0;
}

static int test_uart_period_wraps(void)
{
  sc_app_snapshot_t app_snapshot = snapshot(SC_APP_PHASE_ACTIVE);
  uint32_t degraded = 0u;
  uint32_t start_ms = UINT32_MAX - 99u;

  fake_telemetry_reset();
  sc_telemetry_init(start_ms);
  CHECK(sc_telemetry_service(&app_snapshot, start_ms, &degraded) ==
        SC_TELEMETRY_OK);
  CHECK(fake_uart_call_count() == 1u);
  CHECK(sc_telemetry_service(&app_snapshot, UINT32_MAX, &degraded) ==
        SC_TELEMETRY_OK);
  CHECK(fake_uart_call_count() == 1u);
  CHECK(sc_telemetry_service(&app_snapshot, 100u, &degraded) ==
        SC_TELEMETRY_OK);
  CHECK(fake_uart_call_count() == 2u);
  return 0;
}

typedef struct {
  const char *name;
  int (*run)(void);
} test_case_t;

static const test_case_t s_tests[] = {
  {"uart_rate_and_csv_frame", test_uart_rate_and_csv_frame},
  {"uart_failure_only_degrades", test_uart_failure_only_degrades},
  {"oled_rate_and_led", test_oled_rate_and_led},
  {"display_failure_degrades", test_display_failure_degrades},
  {"async_display_failure_degrades",
   test_async_display_failure_degrades},
  {"arguments", test_arguments},
  {"led_phase_patterns", test_led_phase_patterns},
  {"uart_period_wraps", test_uart_period_wraps}
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
