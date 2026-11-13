/**
  ******************************************************************************
  * @file    sc_telemetry.c
  * @brief   正式固件 5 Hz 遥测、2 Hz OLED 与状态 LED。
  ******************************************************************************
  */

#include "sc_telemetry.h"

#include <stddef.h>

#include "sc_led.h"
#include "sc_ssd1306.h"
#include "sc_uart.h"

#define SC_TELEMETRY_FRAME_SIZE 192u
#define SC_TELEMETRY_LINE_SIZE  22u

typedef struct {
  char *data;
  size_t capacity;
  size_t length;
  uint8_t overflow;
} sc_telemetry_writer_t;

static char s_frame[SC_TELEMETRY_FRAME_SIZE];
static char s_line[SC_TELEMETRY_LINE_SIZE];
static uint32_t s_next_uart_ms;
static uint32_t s_next_oled_ms;
static uint32_t s_last_display_retry_count;

static void sc_telemetry_writer_init(sc_telemetry_writer_t *writer,
                                     char *buffer, size_t capacity)
{
  writer->data = buffer;
  writer->capacity = capacity;
  writer->length = 0u;
  writer->overflow = 0u;
  if (capacity != 0u)
  {
    buffer[0] = '\0';
  }
}

static void sc_telemetry_writer_char(sc_telemetry_writer_t *writer,
                                     char value)
{
  if ((writer->length + 1u) >= writer->capacity)
  {
    writer->overflow = 1u;
    return;
  }
  writer->data[writer->length++] = value;
  writer->data[writer->length] = '\0';
}

static void sc_telemetry_writer_text(sc_telemetry_writer_t *writer,
                                     const char *text)
{
  while (*text != '\0')
  {
    sc_telemetry_writer_char(writer, *text++);
  }
}

static void sc_telemetry_writer_u64(sc_telemetry_writer_t *writer,
                                    uint64_t value)
{
  char digits[20];
  size_t count = 0u;

  do
  {
    digits[count++] = (char)('0' + (value % 10u));
    value /= 10u;
  } while ((value != 0u) && (count < sizeof(digits)));
  while (count > 0u)
  {
    sc_telemetry_writer_char(writer, digits[--count]);
  }
}

static void sc_telemetry_writer_i64(sc_telemetry_writer_t *writer,
                                    int64_t value)
{
  uint64_t magnitude;

  if (value < 0)
  {
    sc_telemetry_writer_char(writer, '-');
    magnitude = (uint64_t)(-(value + 1)) + 1u;
  }
  else
  {
    magnitude = (uint64_t)value;
  }
  sc_telemetry_writer_u64(writer, magnitude);
}

static void sc_telemetry_writer_hex32(sc_telemetry_writer_t *writer,
                                      uint32_t value)
{
  static const char digits[] = "0123456789ABCDEF";
  int shift;

  sc_telemetry_writer_text(writer, "0x");
  for (shift = 28; shift >= 0; shift -= 4)
  {
    sc_telemetry_writer_char(
      writer, digits[(value >> (uint32_t)shift) & 0x0Fu]);
  }
}

static const char *sc_telemetry_phase_name(sc_app_phase_t phase)
{
  switch (phase)
  {
    case SC_APP_PHASE_BOOT:
      return "BOOT";
    case SC_APP_PHASE_IDLE:
      return "IDLE";
    case SC_APP_PHASE_READY:
      return "READY";
    case SC_APP_PHASE_ACTIVE:
      return "ACTIVE";
    case SC_APP_PHASE_STOPPING:
      return "STOPPING";
    case SC_APP_PHASE_FAULT:
      return "FAULT";
    default:
      return "UNKNOWN";
  }
}

static uint8_t sc_telemetry_build_frame(
  const sc_app_snapshot_t *snapshot, uint32_t now_ms)
{
  sc_telemetry_writer_t writer;

  sc_telemetry_writer_init(&writer, s_frame, sizeof(s_frame));
  sc_telemetry_writer_u64(&writer, now_ms);
  sc_telemetry_writer_char(&writer, ',');
  sc_telemetry_writer_text(&writer, sc_telemetry_phase_name(snapshot->phase));
  sc_telemetry_writer_char(&writer, ',');
  sc_telemetry_writer_u64(&writer, snapshot->line.sample.raw_levels);
  sc_telemetry_writer_char(&writer, ',');
  sc_telemetry_writer_u64(&writer, snapshot->line.black_mask);
  sc_telemetry_writer_char(&writer, ',');
  sc_telemetry_writer_i64(&writer, snapshot->target.left_mm_s);
  sc_telemetry_writer_char(&writer, ',');
  sc_telemetry_writer_i64(&writer, snapshot->target.right_mm_s);
  sc_telemetry_writer_char(&writer, ',');
  sc_telemetry_writer_i64(&writer, snapshot->wheel.left_speed_mm_s);
  sc_telemetry_writer_char(&writer, ',');
  sc_telemetry_writer_i64(&writer, snapshot->wheel.right_speed_mm_s);
  sc_telemetry_writer_char(&writer, ',');
  sc_telemetry_writer_i64(
    &writer, snapshot->left_pwm);
  sc_telemetry_writer_char(&writer, ',');
  sc_telemetry_writer_i64(
    &writer, snapshot->right_pwm);
  sc_telemetry_writer_char(&writer, ',');
  sc_telemetry_writer_u64(&writer, snapshot->software_stby);
  sc_telemetry_writer_char(&writer, ',');
  sc_telemetry_writer_hex32(&writer, snapshot->fault_bits);
  sc_telemetry_writer_char(&writer, ',');
  sc_telemetry_writer_hex32(&writer, snapshot->degraded_bits);
  if (writer.overflow == 0u)
  {
    sc_telemetry_writer_text(&writer, "\r\n");
  }
  return (uint8_t)(writer.overflow == 0u);
}

static uint8_t sc_telemetry_write_oled_line(
  uint8_t row, const char *prefix, int32_t first, int32_t second)
{
  sc_telemetry_writer_t writer;

  sc_telemetry_writer_init(&writer, s_line, sizeof(s_line));
  sc_telemetry_writer_text(&writer, prefix);
  sc_telemetry_writer_i64(&writer, first);
  if (second != INT32_MIN)
  {
    sc_telemetry_writer_text(&writer, " ");
    sc_telemetry_writer_i64(&writer, second);
  }
  return (uint8_t)((writer.overflow == 0u) &&
                   (sc_ssd1306_write_line(row, s_line) ==
                    SC_SSD1306_OK));
}

static uint8_t sc_telemetry_write_phase_line(sc_app_phase_t phase)
{
  sc_telemetry_writer_t writer;

  sc_telemetry_writer_init(&writer, s_line, sizeof(s_line));
  sc_telemetry_writer_text(&writer, "P:");
  sc_telemetry_writer_text(&writer, sc_telemetry_phase_name(phase));
  return (uint8_t)((writer.overflow == 0u) &&
                   (sc_ssd1306_write_line(0u, s_line) ==
                    SC_SSD1306_OK));
}

static uint8_t sc_telemetry_update_oled(
  const sc_app_snapshot_t *snapshot)
{
  uint8_t ok = 1u;

  ok &= sc_telemetry_write_phase_line(snapshot->phase);
  ok &= sc_telemetry_write_oled_line(
    1u, "LINE ", snapshot->line.black_mask,
    snapshot->line.sample.raw_levels);
  ok &= sc_telemetry_write_oled_line(
    2u, "TGT ", snapshot->target.left_mm_s,
    snapshot->target.right_mm_s);
  ok &= sc_telemetry_write_oled_line(
    3u, "SPD ", snapshot->wheel.left_speed_mm_s,
    snapshot->wheel.right_speed_mm_s);
  ok &= sc_telemetry_write_oled_line(
    4u, "PWM ", snapshot->left_pwm, snapshot->right_pwm);
  ok &= sc_telemetry_write_oled_line(
    5u, "STBY ", snapshot->software_stby, INT32_MIN);
  ok &= sc_telemetry_write_oled_line(
    6u, "STOP ", (int32_t)snapshot->stop_reason, INT32_MIN);
  ok &= sc_telemetry_write_oled_line(
    7u, "FAULT ", (int32_t)(snapshot->fault_bits & 0xFFFFu),
    (int32_t)(snapshot->fault_bits >> 16u));
  if (ok == 0u)
  {
    return 0u;
  }
  return (uint8_t)(sc_ssd1306_refresh() == SC_SSD1306_OK);
}

static void sc_telemetry_update_led(sc_app_phase_t phase, uint32_t now_ms)
{
  uint8_t on = 0u;

  switch (phase)
  {
    case SC_APP_PHASE_IDLE:
      on = 0u;
      break;
    case SC_APP_PHASE_READY:
      on = (uint8_t)(((now_ms / 500u) & 1u) != 0u);
      break;
    case SC_APP_PHASE_ACTIVE:
      on = 1u;
      break;
    case SC_APP_PHASE_FAULT:
      on = (uint8_t)(((now_ms / 100u) & 1u) != 0u);
      break;
    case SC_APP_PHASE_BOOT:
    case SC_APP_PHASE_STOPPING:
    default:
      on = 0u;
      break;
  }
  (void)sc_led_set(on);
}

void sc_telemetry_init(uint32_t now_ms)
{
  s_next_uart_ms = now_ms;
  s_next_oled_ms = now_ms;
  s_last_display_retry_count = 0u;
  s_frame[0] = '\0';
  s_line[0] = '\0';
}

sc_telemetry_status_t sc_telemetry_service(
  const sc_app_snapshot_t *snapshot, uint32_t now_ms,
  uint32_t *degraded_bits_out)
{
  sc_ssd1306_status_info_t display_status;
  sc_ssd1306_status_t display_status_result;
  uint32_t degraded = 0u;
  sc_telemetry_status_t result = SC_TELEMETRY_OK;

  if ((snapshot == NULL) || (degraded_bits_out == NULL))
  {
    return SC_TELEMETRY_ERR_ARGUMENT;
  }

  sc_telemetry_update_led(snapshot->phase, now_ms);

  if ((int32_t)(now_ms - s_next_uart_ms) >= 0)
  {
    if ((sc_telemetry_build_frame(snapshot, now_ms) == 0u) ||
        (sc_uart_write(s_frame) != SC_UART_OK))
    {
      degraded |= SC_APP_DEGRADED_TELEMETRY;
      result = SC_TELEMETRY_ERR_UART;
    }
    s_next_uart_ms = now_ms + SC_TELEMETRY_UART_PERIOD_MS;
  }

  if ((int32_t)(now_ms - s_next_oled_ms) >= 0)
  {
    if (sc_telemetry_update_oled(snapshot) == 0u)
    {
      degraded |= SC_APP_DEGRADED_DISPLAY;
      result = (result == SC_TELEMETRY_OK) ?
        SC_TELEMETRY_ERR_DISPLAY : result;
    }
    s_next_oled_ms = now_ms + SC_TELEMETRY_OLED_PERIOD_MS;
  }
  sc_ssd1306_service(now_ms);
  display_status_result = sc_ssd1306_get_status(&display_status);
  if ((display_status_result != SC_SSD1306_OK) ||
      ((display_status_result == SC_SSD1306_OK) &&
       ((display_status.last_status != SC_SSD1306_OK) ||
        (display_status.state == SC_SSD1306_STATE_RETRY_WAIT) ||
        (display_status.retry_count != s_last_display_retry_count))))
  {
    degraded |= SC_APP_DEGRADED_DISPLAY;
    result = (result == SC_TELEMETRY_OK) ?
      SC_TELEMETRY_ERR_DISPLAY : result;
  }
  if (display_status_result == SC_SSD1306_OK)
  {
    s_last_display_retry_count = display_status.retry_count;
  }

  *degraded_bits_out = degraded;
  return result;
}
