/**
  ******************************************************************************
  * @file    sc_bt_bench.c
  * @brief   HC-05 串口台架命令层实现。
  ******************************************************************************
  */

#if !defined(SC_BUILD_HC05_BENCH) || (SC_BUILD_HC05_BENCH != 1)
#error "sc_bt_bench.c may only be compiled in the HC-05 bench firmware"
#endif

#include "sc_bt_bench.h"

#include <stddef.h>

#include "sc_encoder.h"
#include "sc_tb6612.h"
#include "sc_uart.h"

#define SC_BT_BENCH_RX_CHUNK        16u
#define SC_BT_BENCH_RX_BUDGET       64u
#define SC_BT_BENCH_RESPONSE_SIZE   224u
#define SC_BT_BENCH_TOKEN_CAPACITY  5u
#define SC_BT_BENCH_HEARTBEAT_MS    1000u
#define SC_BT_BENCH_MOTOR_MAX       150
#define SC_BT_BENCH_DURATION_MIN    100
#define SC_BT_BENCH_DURATION_MAX    2000
#define SC_BT_BENCH_MOTOR_MAX_COMMAND SC_TB6612_COMMAND_MAX

typedef struct {
  char *data;
  size_t capacity;
  size_t length;
  uint8_t overflow;
} sc_bt_bench_writer_t;

static uint8_t s_initialized;
static char s_line[SC_BT_BENCH_MAX_LINE_BYTES + 1u];
static uint8_t s_line_length;
static uint8_t s_discarding;
static uint8_t s_armed;
static uint32_t s_arm_deadline_ms;
static uint8_t s_motor_active;
static uint32_t s_motor_deadline_ms;
static uint32_t s_next_heartbeat_ms;
static sc_uart_stats_t s_uart_stats;
static char s_response[SC_BT_BENCH_RESPONSE_SIZE];

static uint8_t sc_bt_bench_time_reached(uint32_t now_ms,
                                        uint32_t deadline_ms)
{
  return (uint8_t)((uint32_t)(now_ms - deadline_ms) <=
                   (UINT32_MAX / 2u));
}

static uint8_t sc_bt_bench_text_equal(const char *left, const char *right)
{
  while ((*left != '\0') && (*right != '\0'))
  {
    if (*left != *right)
    {
      return 0u;
    }
    left++;
    right++;
  }
  return (uint8_t)((*left == '\0') && (*right == '\0'));
}

static void sc_bt_bench_writer_init(sc_bt_bench_writer_t *writer,
                                    char *data, size_t capacity)
{
  writer->data = data;
  writer->capacity = capacity;
  writer->length = 0u;
  writer->overflow = 0u;
  if (capacity != 0u)
  {
    data[0] = '\0';
  }
}

static void sc_bt_bench_writer_char(sc_bt_bench_writer_t *writer, char value)
{
  if ((writer->length + 1u) >= writer->capacity)
  {
    writer->overflow = 1u;
    return;
  }
  writer->data[writer->length++] = value;
  writer->data[writer->length] = '\0';
}

static void sc_bt_bench_writer_text(sc_bt_bench_writer_t *writer,
                                    const char *text)
{
  while (*text != '\0')
  {
    sc_bt_bench_writer_char(writer, *text++);
  }
}

static void sc_bt_bench_writer_u64(sc_bt_bench_writer_t *writer,
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
    sc_bt_bench_writer_char(writer, digits[--count]);
  }
}

static void sc_bt_bench_writer_i64(sc_bt_bench_writer_t *writer,
                                   int64_t value)
{
  uint64_t magnitude;

  if (value < 0)
  {
    sc_bt_bench_writer_char(writer, '-');
    magnitude = (uint64_t)(-(value + 1)) + 1u;
  }
  else
  {
    magnitude = (uint64_t)value;
  }
  sc_bt_bench_writer_u64(writer, magnitude);
}

static void sc_bt_bench_writer_finish(sc_bt_bench_writer_t *writer)
{
  if (writer->overflow == 0u)
  {
    sc_bt_bench_writer_text(writer, "\r\n");
  }
}

static void sc_bt_bench_send(const char *text)
{
  (void)sc_uart_write(SC_UART_USART2, text);
}

static void sc_bt_bench_reset_parser(void)
{
  s_line_length = 0u;
  s_discarding = 0u;
  s_line[0] = '\0';
}

static sc_tb6612_status_t sc_bt_bench_safe_stop(void)
{
  sc_tb6612_status_t status;

  s_armed = 0u;
  s_arm_deadline_ms = 0u;
  s_motor_active = 0u;
  s_motor_deadline_ms = 0u;
  status = sc_tb6612_set_standby(0u);
  return status;
}

static uint8_t sc_bt_bench_uart_error_changed(
  const sc_uart_stats_t *current)
{
  return (uint8_t)(
    (current->rx_overflow_bytes != s_uart_stats.rx_overflow_bytes) ||
    (current->rx_ore_events != s_uart_stats.rx_ore_events) ||
    (current->rx_fe_events != s_uart_stats.rx_fe_events) ||
    (current->rx_ne_events != s_uart_stats.rx_ne_events) ||
    (current->rx_pe_events != s_uart_stats.rx_pe_events) ||
    (current->rx_error_bytes != s_uart_stats.rx_error_bytes) ||
    (current->rx_rearm_failures != s_uart_stats.rx_rearm_failures));
}

static void sc_bt_bench_uart_fault(void)
{
  s_line_length = 0u;
  s_line[0] = '\0';
  s_discarding = 1u;
  (void)sc_bt_bench_safe_stop();
  sc_bt_bench_send("ERR UART\r\n");
}

static uint8_t sc_bt_bench_split(char *line, const char **tokens,
                                 size_t *token_count)
{
  char *cursor = line;
  size_t count = 0u;

  while (*cursor != '\0')
  {
    while ((*cursor == ' ') || (*cursor == '\t'))
    {
      cursor++;
    }
    if (*cursor == '\0')
    {
      break;
    }
    if (count >= SC_BT_BENCH_TOKEN_CAPACITY)
    {
      return 0u;
    }
    tokens[count++] = cursor;
    while ((*cursor != '\0') && (*cursor != ' ') && (*cursor != '\t'))
    {
      cursor++;
    }
    if (*cursor != '\0')
    {
      *cursor++ = '\0';
    }
  }
  *token_count = count;
  return 1u;
}

static uint8_t sc_bt_bench_parse_i32(const char *text, int32_t minimum,
                                     int32_t maximum, int32_t *value)
{
  uint8_t negative = 0u;
  uint32_t magnitude = 0u;
  uint32_t limit;
  uint32_t digit;

  if ((text == NULL) || (*text == '\0'))
  {
    return 0u;
  }
  if ((*text == '+') || (*text == '-'))
  {
    negative = (uint8_t)(*text == '-');
    text++;
  }
  if (*text == '\0')
  {
    return 0u;
  }

  limit = (negative != 0u) ?
    ((uint32_t)(-(minimum + 1)) + 1u) : (uint32_t)maximum;
  while (*text != '\0')
  {
    if ((*text < '0') || (*text > '9'))
    {
      return 0u;
    }
    digit = (uint32_t)(*text - '0');
    if (magnitude > ((limit - digit) / 10u))
    {
      return 0u;
    }
    magnitude = (magnitude * 10u) + digit;
    text++;
  }

  if (negative != 0u)
  {
    *value = -(int32_t)magnitude;
  }
  else
  {
    *value = (int32_t)magnitude;
  }
  return (uint8_t)((*value >= minimum) && (*value <= maximum));
}

static const char *sc_bt_bench_mode_name(sc_tb6612_mode_t mode)
{
  switch (mode)
  {
    case SC_TB6612_MODE_STANDBY:
      return "STANDBY";
    case SC_TB6612_MODE_ENABLED:
      return "ENABLED";
    case SC_TB6612_MODE_FAULT:
      return "FAULT";
    case SC_TB6612_MODE_UNINITIALIZED:
    default:
      return "UNINIT";
  }
}

static uint8_t sc_bt_bench_build_status(const sc_tb6612_state_t *tb,
                                        const sc_encoder_snapshot_t *encoder)
{
  sc_bt_bench_writer_t writer;

  sc_bt_bench_writer_init(&writer, s_response, sizeof(s_response));
  sc_bt_bench_writer_text(&writer, "STATUS mode=");
  sc_bt_bench_writer_text(&writer, sc_bt_bench_mode_name(tb->mode));
  sc_bt_bench_writer_text(&writer, " stby=");
  sc_bt_bench_writer_u64(&writer, tb->standby);
  sc_bt_bench_writer_text(&writer, " L=");
  sc_bt_bench_writer_i64(&writer, tb->left_command);
  sc_bt_bench_writer_text(&writer, " R=");
  sc_bt_bench_writer_i64(&writer, tb->right_command);
  sc_bt_bench_writer_text(&writer, " Lraw=");
  sc_bt_bench_writer_u64(&writer, encoder->left.raw_count);
  sc_bt_bench_writer_text(&writer, " Ltotal=");
  sc_bt_bench_writer_i64(&writer, encoder->left.total_count);
  sc_bt_bench_writer_text(&writer, " Ldelta=");
  sc_bt_bench_writer_i64(&writer, encoder->left.delta_count);
  sc_bt_bench_writer_text(&writer, " Lvalid=");
  sc_bt_bench_writer_u64(&writer, encoder->left.valid);
  sc_bt_bench_writer_text(&writer, " Rraw=");
  sc_bt_bench_writer_u64(&writer, encoder->right.raw_count);
  sc_bt_bench_writer_text(&writer, " Rtotal=");
  sc_bt_bench_writer_i64(&writer, encoder->right.total_count);
  sc_bt_bench_writer_text(&writer, " Rdelta=");
  sc_bt_bench_writer_i64(&writer, encoder->right.delta_count);
  sc_bt_bench_writer_text(&writer, " Rvalid=");
  sc_bt_bench_writer_u64(&writer, encoder->right.valid);
  sc_bt_bench_writer_finish(&writer);
  return (uint8_t)(writer.overflow == 0u);
}

static uint8_t sc_bt_bench_build_heartbeat(const sc_tb6612_state_t *tb)
{
  sc_bt_bench_writer_t writer;

  sc_bt_bench_writer_init(&writer, s_response, sizeof(s_response));
  sc_bt_bench_writer_text(&writer, "HB mode=");
  sc_bt_bench_writer_text(&writer, sc_bt_bench_mode_name(tb->mode));
  sc_bt_bench_writer_text(&writer, " stby=");
  sc_bt_bench_writer_u64(&writer, tb->standby);
  sc_bt_bench_writer_text(&writer, " L=");
  sc_bt_bench_writer_i64(&writer, tb->left_command);
  sc_bt_bench_writer_text(&writer, " R=");
  sc_bt_bench_writer_i64(&writer, tb->right_command);
  sc_bt_bench_writer_finish(&writer);
  return (uint8_t)(writer.overflow == 0u);
}

static void sc_bt_bench_command_error(const char *response)
{
  (void)sc_bt_bench_safe_stop();
  sc_bt_bench_send(response);
}

static void sc_bt_bench_handle_ping(size_t token_count)
{
  if (token_count != 1u)
  {
    sc_bt_bench_command_error("ERR CMD\r\n");
    return;
  }
  sc_bt_bench_send("OK PONG\r\n");
}

static void sc_bt_bench_handle_status(size_t token_count)
{
  sc_tb6612_state_t tb;
  sc_encoder_snapshot_t encoder;

  if (token_count != 1u)
  {
    sc_bt_bench_command_error("ERR CMD\r\n");
    return;
  }
  if ((sc_tb6612_get_state(&tb) != SC_TB6612_OK) ||
      (sc_encoder_get_latest(&encoder) != SC_ENCODER_OK))
  {
    sc_bt_bench_command_error("ERR IO\r\n");
    return;
  }
  if (sc_bt_bench_build_status(&tb, &encoder) == 0u)
  {
    sc_bt_bench_command_error("ERR LINE\r\n");
    return;
  }
  sc_bt_bench_send(s_response);
}

static void sc_bt_bench_handle_arm(uint32_t now_ms, size_t token_count)
{
  sc_tb6612_state_t state;

  if (token_count != 1u)
  {
    sc_bt_bench_command_error("ERR CMD\r\n");
    return;
  }
  if (s_motor_active != 0u)
  {
    sc_bt_bench_command_error("ERR ACTIVE\r\n");
    return;
  }
  if ((sc_tb6612_set_standby(1u) != SC_TB6612_OK) ||
      (sc_tb6612_get_state(&state) != SC_TB6612_OK) ||
      (state.mode != SC_TB6612_MODE_ENABLED) ||
      (state.enabled == 0u) || (state.standby == 0u))
  {
    sc_bt_bench_command_error("ERR MOTOR\r\n");
    return;
  }
  s_armed = 1u;
  s_arm_deadline_ms = now_ms + SC_BT_BENCH_ARM_WINDOW_MS;
  sc_bt_bench_send("OK ARMED 5000\r\n");
}

static void sc_bt_bench_handle_motor(const char **tokens,
                                     size_t token_count, uint32_t now_ms)
{
  int32_t speed;
  int32_t duration;
  int16_t left;
  int16_t right;
  sc_tb6612_status_t status;

  if (token_count != 4u)
  {
    sc_bt_bench_command_error("ERR ARG\r\n");
    return;
  }
  if ((!sc_bt_bench_text_equal(tokens[1], "L")) &&
      (!sc_bt_bench_text_equal(tokens[1], "R")))
  {
    sc_bt_bench_command_error("ERR ARG\r\n");
    return;
  }
  if ((!sc_bt_bench_parse_i32(tokens[2], -SC_BT_BENCH_MOTOR_MAX,
                              SC_BT_BENCH_MOTOR_MAX, &speed)) ||
      (speed == 0) ||
      (!sc_bt_bench_parse_i32(tokens[3], SC_BT_BENCH_DURATION_MIN,
                              SC_BT_BENCH_DURATION_MAX, &duration)))
  {
    sc_bt_bench_command_error("ERR ARG\r\n");
    return;
  }
  if (s_motor_active != 0u)
  {
    sc_bt_bench_command_error("ERR ACTIVE\r\n");
    return;
  }
  if ((s_armed == 0u) ||
      sc_bt_bench_time_reached(now_ms, s_arm_deadline_ms))
  {
    sc_bt_bench_command_error("ERR ARM\r\n");
    return;
  }

  left = sc_bt_bench_text_equal(tokens[1], "L") ? (int16_t)speed : 0;
  right = sc_bt_bench_text_equal(tokens[1], "R") ? (int16_t)speed : 0;
  status = sc_tb6612_apply(left, right);
  if (status != SC_TB6612_OK)
  {
    sc_bt_bench_command_error("ERR MOTOR\r\n");
    return;
  }

  s_armed = 0u;
  s_arm_deadline_ms = 0u;
  s_motor_active = 1u;
  s_motor_deadline_ms = now_ms + (uint32_t)duration;
  sc_bt_bench_send("OK MOTOR\r\n");
}

static void sc_bt_bench_handle_stop(size_t token_count)
{
  sc_tb6612_status_t status;

  if (token_count != 1u)
  {
    sc_bt_bench_command_error("ERR CMD\r\n");
    return;
  }
  status = sc_bt_bench_safe_stop();
  if (status == SC_TB6612_OK)
  {
    sc_bt_bench_send("OK STOP\r\n");
  }
  else
  {
    sc_bt_bench_send("ERR MOTOR\r\n");
  }
}

static void sc_bt_bench_handle_reset(uint32_t now_ms, size_t token_count)
{
  sc_tb6612_state_t state;

  if (token_count != 1u)
  {
    sc_bt_bench_command_error("ERR CMD\r\n");
    return;
  }
  if (sc_tb6612_get_state(&state) != SC_TB6612_OK)
  {
    sc_bt_bench_command_error("ERR IO\r\n");
    return;
  }
  if ((s_motor_active != 0u) || (s_armed != 0u) ||
      (state.enabled != 0u) || (state.mode != SC_TB6612_MODE_STANDBY))
  {
    sc_bt_bench_command_error("ERR ACTIVE\r\n");
    return;
  }
  if (sc_encoder_reset_counts(now_ms) != SC_ENCODER_OK)
  {
    sc_bt_bench_command_error("ERR ENCODER\r\n");
    return;
  }
  sc_bt_bench_send("OK RESET\r\n");
}

static void sc_bt_bench_dispatch(char *line, uint32_t now_ms)
{
  const char *tokens[SC_BT_BENCH_TOKEN_CAPACITY];
  size_t token_count = 0u;

  if (sc_bt_bench_split(line, tokens, &token_count) == 0u)
  {
    sc_bt_bench_command_error("ERR CMD\r\n");
    return;
  }
  if (token_count == 0u)
  {
    return;
  }
  if (sc_bt_bench_text_equal(tokens[0], "PING"))
  {
    sc_bt_bench_handle_ping(token_count);
  }
  else if (sc_bt_bench_text_equal(tokens[0], "STATUS"))
  {
    sc_bt_bench_handle_status(token_count);
  }
  else if (sc_bt_bench_text_equal(tokens[0], "ARM"))
  {
    sc_bt_bench_handle_arm(now_ms, token_count);
  }
  else if (sc_bt_bench_text_equal(tokens[0], "MOTOR"))
  {
    sc_bt_bench_handle_motor(tokens, token_count, now_ms);
  }
  else if (sc_bt_bench_text_equal(tokens[0], "STOP"))
  {
    sc_bt_bench_handle_stop(token_count);
  }
  else if (sc_bt_bench_text_equal(tokens[0], "RESET"))
  {
    sc_bt_bench_handle_reset(now_ms, token_count);
  }
  else if (sc_bt_bench_text_equal(tokens[0], "HELP"))
  {
    if (token_count == 1u)
    {
      sc_bt_bench_send("OK HELP PING STATUS ARM MOTOR STOP RESET\r\n");
    }
    else
    {
      sc_bt_bench_command_error("ERR CMD\r\n");
    }
  }
  else
  {
    sc_bt_bench_command_error("ERR CMD\r\n");
  }
}

static void sc_bt_bench_process_byte(uint8_t byte, uint32_t now_ms)
{
  if ((byte == (uint8_t)'\r') || (byte == (uint8_t)'\n'))
  {
    if (s_discarding == 0u)
    {
      if (s_line_length != 0u)
      {
        s_line[s_line_length] = '\0';
        sc_bt_bench_dispatch(s_line, now_ms);
      }
    }
    sc_bt_bench_reset_parser();
    return;
  }
  if (s_discarding != 0u)
  {
    return;
  }
  if ((byte != (uint8_t)'\t') &&
      ((byte < (uint8_t)' ') || (byte > (uint8_t)'~')))
  {
    s_line_length = 0u;
    s_line[0] = '\0';
    s_discarding = 1u;
    sc_bt_bench_command_error("ERR CHAR\r\n");
    return;
  }
  if (s_line_length >= SC_BT_BENCH_MAX_LINE_BYTES)
  {
    s_line_length = 0u;
    s_discarding = 1u;
    sc_bt_bench_command_error("ERR LINE\r\n");
    return;
  }
  s_line[s_line_length++] = (char)byte;
}

void sc_bt_bench_init(uint32_t now_ms)
{
  s_initialized = 0u;
  sc_bt_bench_reset_parser();
  s_armed = 0u;
  s_arm_deadline_ms = 0u;
  s_motor_active = 0u;
  s_motor_deadline_ms = 0u;
  s_next_heartbeat_ms = now_ms + SC_BT_BENCH_HEARTBEAT_MS;
  (void)sc_tb6612_set_standby(0u);
  if (sc_uart_get_stats(SC_UART_USART2, &s_uart_stats) != SC_UART_OK)
  {
    s_uart_stats = (sc_uart_stats_t){0};
  }
  s_initialized = 1u;
}

void sc_bt_bench_service(uint32_t now_ms)
{
  sc_uart_stats_t current_stats;
  uint8_t chunk[SC_BT_BENCH_RX_CHUNK];
  size_t received;
  size_t total_received = 0u;
  size_t index;
  uint8_t activity = 0u;
  sc_tb6612_state_t tb;
  sc_tb6612_status_t stop_status;

  if (s_initialized == 0u)
  {
    return;
  }

  if (sc_uart_get_stats(SC_UART_USART2, &current_stats) != SC_UART_OK)
  {
    sc_bt_bench_uart_fault();
    return;
  }
  if (sc_bt_bench_uart_error_changed(&current_stats) != 0u)
  {
    s_uart_stats = current_stats;
    sc_bt_bench_uart_fault();
    return;
  }

  if ((s_motor_active != 0u) &&
      sc_bt_bench_time_reached(now_ms, s_motor_deadline_ms))
  {
    stop_status = sc_bt_bench_safe_stop();
    if (stop_status == SC_TB6612_OK)
    {
      sc_bt_bench_send("OK STOP AUTO\r\n");
    }
    else
    {
      sc_bt_bench_send("ERR MOTOR\r\n");
    }
    activity = 1u;
  }
  if ((s_armed != 0u) &&
      sc_bt_bench_time_reached(now_ms, s_arm_deadline_ms))
  {
    stop_status = sc_bt_bench_safe_stop();
    if (stop_status != SC_TB6612_OK)
    {
      sc_bt_bench_send("ERR MOTOR\r\n");
    }
    activity = 1u;
  }

  while (total_received < SC_BT_BENCH_RX_BUDGET)
  {
    if (sc_uart_read(SC_UART_USART2, chunk, sizeof(chunk),
                     &received) != SC_UART_OK)
    {
      sc_bt_bench_uart_fault();
      return;
    }
    if (received == 0u)
    {
      break;
    }
    activity = 1u;
    total_received += received;
    for (index = 0u; index < received; index++)
    {
      sc_bt_bench_process_byte(chunk[index], now_ms);
    }
  }

  if ((sc_bt_bench_time_reached(now_ms, s_next_heartbeat_ms)) &&
      (activity == 0u))
  {
    s_next_heartbeat_ms = now_ms + SC_BT_BENCH_HEARTBEAT_MS;
    if (sc_tb6612_get_state(&tb) == SC_TB6612_OK)
    {
      if (sc_bt_bench_build_heartbeat(&tb) != 0u)
      {
        sc_bt_bench_send(s_response);
      }
    }
    else
    {
      (void)sc_bt_bench_safe_stop();
      sc_bt_bench_send("ERR IO\r\n");
    }
  }
}
