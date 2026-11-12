#include "fake_telemetry.h"

#include <string.h>

#include "sc_led.h"
#include "sc_ssd1306.h"
#include "sc_uart.h"

static char s_uart_last[256];
static size_t s_uart_calls;
static uint8_t s_uart_fail;
static char s_oled_lines[SC_SSD1306_PAGES][SC_SSD1306_LINE_CHARS + 1u];
static size_t s_oled_writes;
static size_t s_oled_refreshes;
static size_t s_oled_services;
static uint8_t s_oled_fail_write;
static uint8_t s_oled_fail_refresh;
static uint8_t s_oled_fail_service;
static sc_ssd1306_status_info_t s_oled_status;
static uint32_t s_led_calls;
static uint8_t s_led_value;

void fake_telemetry_reset(void)
{
  memset(s_uart_last, 0, sizeof(s_uart_last));
  s_uart_calls = 0u;
  s_uart_fail = 0u;
  memset(s_oled_lines, 0, sizeof(s_oled_lines));
  s_oled_writes = 0u;
  s_oled_refreshes = 0u;
  s_oled_services = 0u;
  s_oled_fail_write = 0u;
  s_oled_fail_refresh = 0u;
  s_oled_fail_service = 0u;
  s_oled_status = (sc_ssd1306_status_info_t){
    .state = SC_SSD1306_STATE_IDLE,
    .dirty_pages = 0u,
    .refresh_pending = 0u,
    .retry_count = 0u,
    .last_status = SC_SSD1306_OK
  };
  s_led_calls = 0u;
  s_led_value = 0u;
}

void fake_uart_fail_next(void)
{
  s_uart_fail = 1u;
}

size_t fake_uart_call_count(void)
{
  return s_uart_calls;
}

const char *fake_uart_last(void)
{
  return s_uart_last;
}

sc_uart_status_t sc_uart_write(const char *text)
{
  if (text == NULL)
  {
    return SC_UART_ERR_ARGUMENT;
  }
  s_uart_calls++;
  if (s_uart_fail != 0u)
  {
    s_uart_fail = 0u;
    return SC_UART_ERR_BUFFER_FULL;
  }
  strncpy(s_uart_last, text, sizeof(s_uart_last) - 1u);
  s_uart_last[sizeof(s_uart_last) - 1u] = '\0';
  return SC_UART_OK;
}

void fake_oled_fail_next_write(void)
{
  s_oled_fail_write = 1u;
}

void fake_oled_fail_next_refresh(void)
{
  s_oled_fail_refresh = 1u;
}

void fake_oled_fail_next_service(void)
{
  s_oled_fail_service = 1u;
}

size_t fake_oled_write_count(void)
{
  return s_oled_writes;
}

size_t fake_oled_refresh_count(void)
{
  return s_oled_refreshes;
}

size_t fake_oled_service_count(void)
{
  return s_oled_services;
}

const char *fake_oled_line(uint8_t row)
{
  return (row < SC_SSD1306_PAGES) ? s_oled_lines[row] : "";
}

sc_ssd1306_status_t sc_ssd1306_write_line(uint8_t row, const char *text)
{
  if ((row >= SC_SSD1306_PAGES) || (text == NULL))
  {
    return SC_SSD1306_ERR_ARGUMENT;
  }
  s_oled_writes++;
  if (s_oled_fail_write != 0u)
  {
    s_oled_fail_write = 0u;
    return SC_SSD1306_ERR_BUS;
  }
  strncpy(s_oled_lines[row], text, SC_SSD1306_LINE_CHARS);
  s_oled_lines[row][SC_SSD1306_LINE_CHARS] = '\0';
  return SC_SSD1306_OK;
}

sc_ssd1306_status_t sc_ssd1306_refresh(void)
{
  s_oled_refreshes++;
  if (s_oled_fail_refresh != 0u)
  {
    s_oled_fail_refresh = 0u;
    return SC_SSD1306_ERR_BUS;
  }
  return SC_SSD1306_OK;
}

void sc_ssd1306_service(uint32_t now_ms)
{
  (void)now_ms;
  s_oled_services++;
  if (s_oled_fail_service != 0u)
  {
    s_oled_fail_service = 0u;
    s_oled_status.state = SC_SSD1306_STATE_RETRY_WAIT;
    s_oled_status.retry_count++;
    s_oled_status.last_status = SC_SSD1306_ERR_BUS;
  }
  else if (s_oled_status.state == SC_SSD1306_STATE_RETRY_WAIT)
  {
    s_oled_status.state = SC_SSD1306_STATE_IDLE;
    s_oled_status.last_status = SC_SSD1306_OK;
  }
}

sc_ssd1306_status_t sc_ssd1306_get_status(
  sc_ssd1306_status_info_t *status)
{
  if (status == NULL)
  {
    return SC_SSD1306_ERR_ARGUMENT;
  }
  *status = s_oled_status;
  return SC_SSD1306_OK;
}

const sc_ssd1306_status_info_t *fake_oled_status(void)
{
  return &s_oled_status;
}

sc_led_status_t sc_led_set(uint8_t on)
{
  s_led_calls++;
  s_led_value = on;
  return SC_LED_OK;
}

uint32_t fake_led_call_count(void)
{
  return s_led_calls;
}

uint8_t fake_led_last(void)
{
  return s_led_value;
}
