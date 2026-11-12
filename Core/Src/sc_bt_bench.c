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
#include "sc_line_service.h"
#include "sc_motor_defaults.h"
#include "sc_speed_controller.h"
#include "sc_tb6612.h"
#include "sc_uart.h"
#include "sc_wheel_service.h"

#define SC_BT_BENCH_RX_CHUNK        16u
#define SC_BT_BENCH_RX_BUDGET       128u
#define SC_BT_BENCH_RESPONSE_SIZE   512u
#define SC_BT_BENCH_TOKEN_CAPACITY  6u
#define SC_BT_BENCH_HEARTBEAT_MS    1000u
#define SC_BT_BENCH_MOTOR_MAX       150
#define SC_BT_BENCH_RAW_MAX         564
#define SC_BT_BENCH_SPEED_MAX       500
#define SC_BT_BENCH_DURATION_MIN    100
#define SC_BT_BENCH_DURATION_MAX    2000
#define SC_BT_BENCH_MOTOR_MAX_COMMAND SC_TB6612_COMMAND_MAX
#define SC_BT_BENCH_CAPTURE_PERIOD_MS  10u
#define SC_BT_BENCH_CAPTURE_CAPACITY   256u
#define SC_BT_BENCH_CAPTURE_EXPORT_MAX 3u
#define SC_BT_BENCH_FAULT_TB         (1u << 0)
#define SC_BT_BENCH_FAULT_FEEDBACK   (1u << 1)
#define SC_BT_BENCH_FAULT_UART       (1u << 2)
#define SC_BT_BENCH_FAULT_CONTROL    (1u << 3)

typedef enum {
  SC_BT_BENCH_EXPERIMENT_NONE = 0,
  SC_BT_BENCH_EXPERIMENT_MOTOR,
  SC_BT_BENCH_EXPERIMENT_RAW,
  SC_BT_BENCH_EXPERIMENT_SPEED
} sc_bt_bench_experiment_mode_t;

typedef struct {
  uint32_t time_ms;
  int32_t target_left_mm_s;
  int32_t target_right_mm_s;
  int32_t speed_left_mm_s;
  int32_t speed_right_mm_s;
  int16_t pwm_left;
  int16_t pwm_right;
  uint16_t fault_bits;
  uint8_t experiment_mode;
  uint8_t controller_mode;
} sc_bt_bench_capture_record_t;

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
static uint8_t s_experiment_mode;
static int32_t s_target_left_mm_s;
static int32_t s_target_right_mm_s;
static uint32_t s_next_heartbeat_ms;
static sc_uart_stats_t s_uart_stats;
static char s_response[SC_BT_BENCH_RESPONSE_SIZE];
static sc_wheel_calibration_t s_calibration;
static sc_wheel_service_t s_wheel_service;
static sc_wheel_feedback_t s_wheel;
static sc_speed_controller_t s_speed_controller;
static sc_speed_controller_output_t s_speed_output;
static uint32_t s_last_encoder_sequence;
static uint8_t s_encoder_has_sequence;
static uint8_t s_feedback_updated;
static uint8_t s_speed_ready;
static uint8_t s_wheel_ready;
static sc_bt_bench_capture_record_t s_capture[
  SC_BT_BENCH_CAPTURE_CAPACITY];
static uint16_t s_capture_count;
static uint32_t s_capture_start_ms;
static uint32_t s_capture_last_sample_ms;
static uint8_t s_capture_has_sample;
static uint8_t s_capture_export_active;
static uint16_t s_capture_export_index;
static uint16_t s_last_fault_bits;
static uint32_t s_current_service_ms;

static sc_tb6612_status_t sc_bt_bench_safe_stop(void);
static void sc_bt_bench_capture_sample(uint32_t now_ms);

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

static void sc_bt_bench_writer_hex8(sc_bt_bench_writer_t *writer,
                                    uint8_t value)
{
  static const char digits[] = "0123456789ABCDEF";

  sc_bt_bench_writer_text(writer, "0x");
  sc_bt_bench_writer_char(writer, digits[(value >> 4u) & 0x0Fu]);
  sc_bt_bench_writer_char(writer, digits[value & 0x0Fu]);
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

static sc_uart_status_t sc_bt_bench_write(const char *text)
{
  return sc_uart_write(text);
}

static void sc_bt_bench_send(const char *text)
{
  if (sc_bt_bench_write(text) != SC_UART_OK)
  {
    s_last_fault_bits |= SC_BT_BENCH_FAULT_UART;
    sc_bt_bench_capture_sample(s_current_service_ms);
    (void)sc_bt_bench_safe_stop();
  }
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
  s_experiment_mode = SC_BT_BENCH_EXPERIMENT_NONE;
  s_target_left_mm_s = 0;
  s_target_right_mm_s = 0;
  s_capture_export_active = 0u;

  status = SC_TB6612_OK;
  if (s_speed_ready != 0u)
  {
    if (sc_speed_controller_stop(
          &s_speed_controller, &s_speed_output) !=
        SC_SPEED_CONTROLLER_STOPPED)
    {
      status = SC_TB6612_ERR_HAL;
    }
  }
  if (sc_tb6612_set_standby(0u) != SC_TB6612_OK)
  {
    status = SC_TB6612_ERR_HAL;
  }
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
    (current->rx_rearm_failures != s_uart_stats.rx_rearm_failures) ||
    (current->tx_start_failures != s_uart_stats.tx_start_failures));
}

static void sc_bt_bench_uart_fault(void)
{
  s_line_length = 0u;
  s_line[0] = '\0';
  s_discarding = 1u;
  s_last_fault_bits |= SC_BT_BENCH_FAULT_UART;
  sc_bt_bench_capture_sample(s_current_service_ms);
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

static uint8_t sc_bt_bench_parse_side_mask(const char *text,
                                           uint8_t *side_mask)
{
  if (sc_bt_bench_text_equal(text, "L"))
  {
    *side_mask = SC_SPEED_CONTROLLER_SIDE_LEFT;
    return 1u;
  }
  if (sc_bt_bench_text_equal(text, "R"))
  {
    *side_mask = SC_SPEED_CONTROLLER_SIDE_RIGHT;
    return 1u;
  }
  if (sc_bt_bench_text_equal(text, "B"))
  {
    *side_mask = SC_SPEED_CONTROLLER_SIDE_BOTH;
    return 1u;
  }
  return 0u;
}

static const char *sc_bt_bench_experiment_name(uint8_t mode)
{
  switch (mode)
  {
    case SC_BT_BENCH_EXPERIMENT_MOTOR:
      return "MOTOR";
    case SC_BT_BENCH_EXPERIMENT_RAW:
      return "RAW";
    case SC_BT_BENCH_EXPERIMENT_SPEED:
      return "SPEED";
    default:
      return "NONE";
  }
}

static uint8_t sc_bt_bench_update_feedback(uint32_t now_ms)
{
  sc_encoder_snapshot_t snapshot;
  sc_wheel_service_status_t wheel_status;

  s_feedback_updated = 0u;
  if (s_wheel_ready == 0u)
  {
    return 0u;
  }
  if (sc_encoder_get_latest(&snapshot) != SC_ENCODER_OK)
  {
    return 0u;
  }
  if ((s_encoder_has_sequence != 0u) &&
      (snapshot.sequence == s_last_encoder_sequence))
  {
    return 1u;
  }

  s_last_encoder_sequence = snapshot.sequence;
  s_encoder_has_sequence = 1u;
  wheel_status = sc_wheel_service_update(
    &s_wheel_service, &snapshot, &s_calibration, now_ms, &s_wheel);
  (void)wheel_status;
  s_feedback_updated = 1u;
  return 1u;
}

static uint8_t sc_bt_bench_speed_feedback_fresh(uint32_t now_ms)
{
  return (uint8_t)(
    (s_wheel_ready != 0u) &&
    (s_wheel.valid != 0u) &&
    ((uint32_t)(now_ms - s_wheel.sample_time_ms) <=
     SC_WHEEL_MAX_SNAPSHOT_AGE_MS));
}

static void sc_bt_bench_reset_capture(
  uint8_t mode, uint32_t now_ms, int32_t left_target,
  int32_t right_target)
{
  s_capture_count = 0u;
  s_capture_start_ms = now_ms;
  s_capture_last_sample_ms = now_ms;
  s_capture_has_sample = 0u;
  s_capture_export_active = 0u;
  s_capture_export_index = 0u;
  s_last_fault_bits = 0u;
  s_experiment_mode = mode;
  s_target_left_mm_s = left_target;
  s_target_right_mm_s = right_target;
}

static void sc_bt_bench_capture_sample(uint32_t now_ms)
{
  sc_bt_bench_capture_record_t *record;
  sc_tb6612_state_t tb_state;
  uint16_t fault_bits = s_last_fault_bits;

  if ((s_motor_active == 0u) ||
      ((s_capture_has_sample != 0u) &&
       ((uint32_t)(now_ms - s_capture_last_sample_ms) <
        SC_BT_BENCH_CAPTURE_PERIOD_MS)))
  {
    return;
  }
  if (s_capture_count >= SC_BT_BENCH_CAPTURE_CAPACITY)
  {
    return;
  }
  if (sc_tb6612_get_state(&tb_state) != SC_TB6612_OK)
  {
    tb_state = (sc_tb6612_state_t){0};
    fault_bits |= SC_BT_BENCH_FAULT_TB;
  }
  if (tb_state.mode == SC_TB6612_MODE_FAULT)
  {
    fault_bits |= SC_BT_BENCH_FAULT_TB;
  }

  record = &s_capture[s_capture_count++];
  record->time_ms = now_ms - s_capture_start_ms;
  record->target_left_mm_s = s_target_left_mm_s;
  record->target_right_mm_s = s_target_right_mm_s;
  record->speed_left_mm_s = s_wheel.left_speed_mm_s;
  record->speed_right_mm_s = s_wheel.right_speed_mm_s;
  record->pwm_left = tb_state.left_command;
  record->pwm_right = tb_state.right_command;
  record->fault_bits = fault_bits;
  record->experiment_mode = s_experiment_mode;
  record->controller_mode =
    (uint8_t)s_speed_controller.mode;
  s_capture_last_sample_ms = now_ms;
  s_capture_has_sample = 1u;
}

static uint8_t sc_bt_bench_build_capture_line(uint16_t index)
{
  const sc_bt_bench_capture_record_t *record;
  sc_bt_bench_writer_t writer;

  if (index >= s_capture_count)
  {
    return 0u;
  }
  record = &s_capture[index];
  sc_bt_bench_writer_init(&writer, s_response, sizeof(s_response));
  sc_bt_bench_writer_text(&writer, "CAP,");
  sc_bt_bench_writer_u64(&writer, record->time_ms);
  sc_bt_bench_writer_char(&writer, ',');
  sc_bt_bench_writer_text(
    &writer, sc_bt_bench_experiment_name(record->experiment_mode));
  sc_bt_bench_writer_char(&writer, ',');
  sc_bt_bench_writer_i64(&writer, record->target_left_mm_s);
  sc_bt_bench_writer_char(&writer, ',');
  sc_bt_bench_writer_i64(&writer, record->target_right_mm_s);
  sc_bt_bench_writer_char(&writer, ',');
  sc_bt_bench_writer_i64(&writer, record->speed_left_mm_s);
  sc_bt_bench_writer_char(&writer, ',');
  sc_bt_bench_writer_i64(&writer, record->speed_right_mm_s);
  sc_bt_bench_writer_char(&writer, ',');
  sc_bt_bench_writer_i64(&writer, record->pwm_left);
  sc_bt_bench_writer_char(&writer, ',');
  sc_bt_bench_writer_i64(&writer, record->pwm_right);
  sc_bt_bench_writer_char(&writer, ',');
  sc_bt_bench_writer_u64(&writer, record->fault_bits);
  sc_bt_bench_writer_char(&writer, ',');
  sc_bt_bench_writer_u64(&writer, record->controller_mode);
  sc_bt_bench_writer_finish(&writer);
  return (uint8_t)(writer.overflow == 0u);
}

static uint8_t sc_bt_bench_service_capture_export(void)
{
  sc_uart_status_t status;
  uint8_t sent = 0u;
  uint8_t count;

  if (s_capture_export_active == 0u)
  {
    return 0u;
  }
  for (count = 0u;
       (count < SC_BT_BENCH_CAPTURE_EXPORT_MAX) &&
       (s_capture_export_active != 0u);
       count++)
  {
    if (s_capture_export_index < s_capture_count)
    {
      if (sc_bt_bench_build_capture_line(
            s_capture_export_index) == 0u)
      {
        s_last_fault_bits |= SC_BT_BENCH_FAULT_UART;
        sc_bt_bench_uart_fault();
        return sent;
      }
      status = sc_bt_bench_write(s_response);
      if ((status == SC_UART_ERR_BUFFER_FULL) ||
          (status == SC_UART_ERR_BUSY))
      {
        return sent;
      }
      if (status != SC_UART_OK)
      {
        s_last_fault_bits |= SC_BT_BENCH_FAULT_UART;
        sc_bt_bench_uart_fault();
        return sent;
      }
      s_capture_export_index++;
      sent = 1u;
      continue;
    }

    status = sc_bt_bench_write("OK CAPTURE END\r\n");
    if ((status == SC_UART_ERR_BUFFER_FULL) ||
        (status == SC_UART_ERR_BUSY))
    {
      return sent;
    }
    if (status != SC_UART_OK)
    {
      s_last_fault_bits |= SC_BT_BENCH_FAULT_UART;
      sc_bt_bench_uart_fault();
      return sent;
    }
    s_capture_export_active = 0u;
    sent = 1u;
  }
  return sent;
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
  sc_bt_bench_writer_text(&writer, " Ltarget=");
  sc_bt_bench_writer_i64(&writer, s_target_left_mm_s);
  sc_bt_bench_writer_text(&writer, " Rtarget=");
  sc_bt_bench_writer_i64(&writer, s_target_right_mm_s);
  sc_bt_bench_writer_text(&writer, " Lspeed=");
  sc_bt_bench_writer_i64(&writer, s_wheel.left_speed_mm_s);
  sc_bt_bench_writer_text(&writer, " Rspeed=");
  sc_bt_bench_writer_i64(&writer, s_wheel.right_speed_mm_s);
  sc_bt_bench_writer_text(&writer, " Lwheelvalid=");
  sc_bt_bench_writer_u64(&writer, s_wheel.left_valid);
  sc_bt_bench_writer_text(&writer, " Rwheelvalid=");
  sc_bt_bench_writer_u64(&writer, s_wheel.right_valid);
  sc_bt_bench_writer_text(&writer, " Lkp=");
  sc_bt_bench_writer_i64(&writer, s_speed_controller.config.left.kp_milli);
  sc_bt_bench_writer_text(&writer, " Lki=");
  sc_bt_bench_writer_i64(&writer, s_speed_controller.config.left.ki_milli);
  sc_bt_bench_writer_text(&writer, " Lkd=");
  sc_bt_bench_writer_i64(&writer, s_speed_controller.config.left.kd_milli);
  sc_bt_bench_writer_text(&writer, " Lff=");
  sc_bt_bench_writer_i64(&writer, s_speed_controller.config.left.ff_milli);
  sc_bt_bench_writer_text(&writer, " Rkp=");
  sc_bt_bench_writer_i64(&writer, s_speed_controller.config.right.kp_milli);
  sc_bt_bench_writer_text(&writer, " Rki=");
  sc_bt_bench_writer_i64(&writer, s_speed_controller.config.right.ki_milli);
  sc_bt_bench_writer_text(&writer, " Rkd=");
  sc_bt_bench_writer_i64(&writer, s_speed_controller.config.right.kd_milli);
  sc_bt_bench_writer_text(&writer, " Rff=");
  sc_bt_bench_writer_i64(&writer, s_speed_controller.config.right.ff_milli);
  sc_bt_bench_writer_text(&writer, " CAP=");
  sc_bt_bench_writer_u64(&writer, s_capture_count);
  sc_bt_bench_writer_text(&writer, " PARAM=UNVERIFIED");
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

static const char *sc_bt_bench_line_name(sc_line_class_t state)
{
  switch (state)
  {
    case SC_LINE_INVALID:
      return "SC_LINE_INVALID";
    case SC_LINE_STRAIGHT:
      return "SC_LINE_STRAIGHT";
    case SC_LINE_LEFT_RIGHT_ANGLE_CANDIDATE:
      return "SC_LINE_LEFT_RIGHT_ANGLE_CANDIDATE";
    case SC_LINE_RIGHT_RIGHT_ANGLE_CANDIDATE:
      return "SC_LINE_RIGHT_RIGHT_ANGLE_CANDIDATE";
    case SC_LINE_LEFT_LARGE_BEND_CANDIDATE:
      return "SC_LINE_LEFT_LARGE_BEND_CANDIDATE";
    case SC_LINE_RIGHT_LARGE_BEND_CANDIDATE:
      return "SC_LINE_RIGHT_LARGE_BEND_CANDIDATE";
    case SC_LINE_LEFT_FINE_ADJUST_CANDIDATE:
      return "SC_LINE_LEFT_FINE_ADJUST_CANDIDATE";
    case SC_LINE_RIGHT_FINE_ADJUST_CANDIDATE:
      return "SC_LINE_RIGHT_FINE_ADJUST_CANDIDATE";
    case SC_LINE_JUNCTION_CANDIDATE:
      return "SC_LINE_JUNCTION_CANDIDATE";
    case SC_LINE_LOST_CANDIDATE:
      return "SC_LINE_LOST_CANDIDATE";
    case SC_LINE_UNKNOWN_PATTERN:
      return "SC_LINE_UNKNOWN_PATTERN";
    default:
      return NULL;
  }
}

static uint8_t sc_bt_bench_build_line(const sc_line_state_t *line,
                                      uint32_t now_ms)
{
  sc_bt_bench_writer_t writer;
  const char *state_name =
    sc_bt_bench_line_name(line->semantic_state);
  int bit;

  if (state_name == NULL)
  {
    return 0u;
  }

  sc_bt_bench_writer_init(&writer, s_response, sizeof(s_response));
  sc_bt_bench_writer_text(&writer, "LINE raw=");
  for (bit = 0; bit < 4; bit++)
  {
    sc_bt_bench_writer_char(
      &writer, (char)('0' + ((line->sample.raw_levels >> bit) & 0x01u)));
  }
  sc_bt_bench_writer_text(&writer, " raw_hex=");
  sc_bt_bench_writer_hex8(&writer, line->sample.raw_levels & 0x0Fu);
  sc_bt_bench_writer_text(&writer, " black_mask=");
  sc_bt_bench_writer_hex8(&writer, line->black_mask);
  sc_bt_bench_writer_text(&writer, " state=");
  sc_bt_bench_writer_text(&writer, state_name);
  sc_bt_bench_writer_text(&writer, " valid=");
  sc_bt_bench_writer_u64(&writer, line->sample.valid);
  sc_bt_bench_writer_text(&writer, " t=");
  sc_bt_bench_writer_u64(&writer, line->sample.sample_time_ms);
  sc_bt_bench_writer_text(&writer, " seq=");
  sc_bt_bench_writer_u64(&writer, line->sample.sequence);
  sc_bt_bench_writer_text(&writer, " age=");
  sc_bt_bench_writer_u64(
    &writer, (uint32_t)(now_ms - line->sample.sample_time_ms));
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

static void sc_bt_bench_handle_line(size_t token_count, uint32_t now_ms)
{
  sc_line_state_t line;

  if (token_count != 1u)
  {
    sc_bt_bench_command_error("ERR CMD\r\n");
    return;
  }
  if (sc_line_service_get_latest(&line) != SC_LINE_OK)
  {
    sc_bt_bench_command_error("ERR IO\r\n");
    return;
  }
  if ((line.sample.valid == 0u) ||
      ((line.sample.raw_levels & 0xF0u) != 0u) ||
      (line.semantic_state == SC_LINE_INVALID))
  {
    sc_bt_bench_command_error("ERR IO\r\n");
    return;
  }
  if (((int)line.semantic_state < (int)SC_LINE_INVALID) ||
      ((int)line.semantic_state > (int)SC_LINE_UNKNOWN_PATTERN))
  {
    sc_bt_bench_command_error("ERR IO\r\n");
    return;
  }
  if (sc_bt_bench_build_line(&line, now_ms) == 0u)
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
  sc_bt_bench_reset_capture(
    SC_BT_BENCH_EXPERIMENT_MOTOR, now_ms, 0, 0);
  sc_bt_bench_capture_sample(now_ms);
  sc_bt_bench_send("OK MOTOR\r\n");
}

static void sc_bt_bench_handle_raw(const char **tokens,
                                   size_t token_count, uint32_t now_ms)
{
  uint8_t side_mask;
  int32_t pwm;
  int32_t duration;
  int16_t left;
  int16_t right;

  if (token_count != 4u)
  {
    sc_bt_bench_command_error("ERR ARG\r\n");
    return;
  }
  if ((!sc_bt_bench_parse_side_mask(tokens[1], &side_mask)) ||
      (!sc_bt_bench_parse_i32(tokens[2], -SC_BT_BENCH_RAW_MAX,
                              SC_BT_BENCH_RAW_MAX, &pwm)) ||
      (pwm == 0) ||
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

  left = ((side_mask & SC_SPEED_CONTROLLER_SIDE_LEFT) != 0u) ?
    (int16_t)pwm : 0;
  right = ((side_mask & SC_SPEED_CONTROLLER_SIDE_RIGHT) != 0u) ?
    (int16_t)pwm : 0;
  if (sc_tb6612_apply(left, right) != SC_TB6612_OK)
  {
    s_last_fault_bits |= SC_BT_BENCH_FAULT_TB;
    sc_bt_bench_command_error("ERR MOTOR\r\n");
    return;
  }

  s_armed = 0u;
  s_arm_deadline_ms = 0u;
  s_motor_active = 1u;
  s_motor_deadline_ms = now_ms + (uint32_t)duration;
  sc_bt_bench_reset_capture(
    SC_BT_BENCH_EXPERIMENT_RAW, now_ms, 0, 0);
  sc_bt_bench_capture_sample(now_ms);
  sc_bt_bench_send("OK RAW PARAM=UNVERIFIED\r\n");
}

static void sc_bt_bench_handle_speed(const char **tokens,
                                     size_t token_count, uint32_t now_ms)
{
  uint8_t side_mask;
  int32_t target;
  int32_t duration;

  if (token_count != 4u)
  {
    sc_bt_bench_command_error("ERR ARG\r\n");
    return;
  }
  if ((!sc_bt_bench_parse_side_mask(tokens[1], &side_mask)) ||
      (!sc_bt_bench_parse_i32(tokens[2], -SC_BT_BENCH_SPEED_MAX,
                              SC_BT_BENCH_SPEED_MAX, &target)) ||
      (target == 0) ||
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
  if ((s_speed_ready == 0u) ||
      (sc_bt_bench_speed_feedback_fresh(now_ms) == 0u))
  {
    s_last_fault_bits |= SC_BT_BENCH_FAULT_FEEDBACK;
    sc_bt_bench_command_error("ERR FEEDBACK\r\n");
    return;
  }
  if ((sc_speed_controller_reset(
         &s_speed_controller, &s_speed_output) !=
       SC_SPEED_CONTROLLER_STOPPED) ||
      (sc_speed_controller_set_mode(
         &s_speed_controller, SC_SPEED_CONTROLLER_MODE_RUNNING,
         &s_speed_output) != SC_SPEED_CONTROLLER_OK) ||
      (sc_tb6612_apply(0, 0) != SC_TB6612_OK))
  {
    s_last_fault_bits |= SC_BT_BENCH_FAULT_CONTROL;
    sc_bt_bench_command_error("ERR SPEED\r\n");
    return;
  }

  s_armed = 0u;
  s_arm_deadline_ms = 0u;
  s_motor_active = 1u;
  s_motor_deadline_ms = now_ms + (uint32_t)duration;
  sc_bt_bench_reset_capture(
    SC_BT_BENCH_EXPERIMENT_SPEED, now_ms,
    ((side_mask & SC_SPEED_CONTROLLER_SIDE_LEFT) != 0u) ? target : 0,
    ((side_mask & SC_SPEED_CONTROLLER_SIDE_RIGHT) != 0u) ? target : 0);
  sc_bt_bench_capture_sample(now_ms);
  sc_bt_bench_send("OK SPEED PARAM=UNVERIFIED\r\n");
}

static void sc_bt_bench_handle_pid(const char **tokens,
                                   size_t token_count)
{
  sc_tb6612_state_t tb_state;
  sc_bt_bench_writer_t writer;
  uint8_t side_mask;
  int32_t kp;
  int32_t ki;
  int32_t kd;

  if (token_count != 5u)
  {
    sc_bt_bench_command_error("ERR ARG\r\n");
    return;
  }
  if ((!sc_bt_bench_parse_side_mask(tokens[1], &side_mask)) ||
      (!sc_bt_bench_parse_i32(
         tokens[2], -SC_SPEED_GAIN_LIMIT_MILLI,
         SC_SPEED_GAIN_LIMIT_MILLI, &kp)) ||
      (!sc_bt_bench_parse_i32(
         tokens[3], -SC_SPEED_GAIN_LIMIT_MILLI,
         SC_SPEED_GAIN_LIMIT_MILLI, &ki)) ||
      (!sc_bt_bench_parse_i32(
         tokens[4], -SC_SPEED_GAIN_LIMIT_MILLI,
         SC_SPEED_GAIN_LIMIT_MILLI, &kd)))
  {
    sc_bt_bench_command_error("ERR ARG\r\n");
    return;
  }
  if ((s_motor_active != 0u) || (s_armed != 0u) ||
      (sc_tb6612_get_state(&tb_state) != SC_TB6612_OK) ||
      (tb_state.enabled != 0u) ||
      (tb_state.mode != SC_TB6612_MODE_STANDBY) ||
      (s_speed_controller.mode != SC_SPEED_CONTROLLER_MODE_STOPPED))
  {
    sc_bt_bench_command_error(
      s_motor_active != 0u ? "ERR ACTIVE\r\n" : "ERR MOTOR\r\n");
    return;
  }
  if (sc_speed_controller_update_pid(
        &s_speed_controller, side_mask, kp, ki, kd) !=
      SC_SPEED_CONTROLLER_OK)
  {
    sc_bt_bench_command_error("ERR PID\r\n");
    return;
  }

  sc_bt_bench_writer_init(&writer, s_response, sizeof(s_response));
  sc_bt_bench_writer_text(&writer, "OK PID ");
  sc_bt_bench_writer_text(
    &writer, (side_mask == SC_SPEED_CONTROLLER_SIDE_BOTH) ? "B" :
      ((side_mask == SC_SPEED_CONTROLLER_SIDE_LEFT) ? "L" : "R"));
  sc_bt_bench_writer_text(&writer, " Lkp=");
  sc_bt_bench_writer_i64(&writer, s_speed_controller.config.left.kp_milli);
  sc_bt_bench_writer_text(&writer, " Lki=");
  sc_bt_bench_writer_i64(&writer, s_speed_controller.config.left.ki_milli);
  sc_bt_bench_writer_text(&writer, " Lkd=");
  sc_bt_bench_writer_i64(&writer, s_speed_controller.config.left.kd_milli);
  sc_bt_bench_writer_text(&writer, " Lff=");
  sc_bt_bench_writer_i64(&writer, s_speed_controller.config.left.ff_milli);
  sc_bt_bench_writer_text(&writer, " Rkp=");
  sc_bt_bench_writer_i64(&writer, s_speed_controller.config.right.kp_milli);
  sc_bt_bench_writer_text(&writer, " Rki=");
  sc_bt_bench_writer_i64(&writer, s_speed_controller.config.right.ki_milli);
  sc_bt_bench_writer_text(&writer, " Rkd=");
  sc_bt_bench_writer_i64(&writer, s_speed_controller.config.right.kd_milli);
  sc_bt_bench_writer_text(&writer, " Rff=");
  sc_bt_bench_writer_i64(&writer, s_speed_controller.config.right.ff_milli);
  sc_bt_bench_writer_text(&writer, " PARAM=RAM_ONLY");
  sc_bt_bench_writer_finish(&writer);
  if (writer.overflow != 0u)
  {
    sc_bt_bench_command_error("ERR PID\r\n");
    return;
  }
  sc_bt_bench_send(s_response);
}

static void sc_bt_bench_handle_capture(size_t token_count)
{
  sc_tb6612_state_t tb_state;

  if (token_count != 1u)
  {
    sc_bt_bench_command_error("ERR CMD\r\n");
    return;
  }
  if ((s_motor_active != 0u) || (s_armed != 0u) ||
      (s_capture_export_active != 0u) ||
      (sc_tb6612_get_state(&tb_state) != SC_TB6612_OK) ||
      (tb_state.enabled != 0u) ||
      (tb_state.mode != SC_TB6612_MODE_STANDBY))
  {
    sc_bt_bench_command_error(
      s_motor_active != 0u ? "ERR ACTIVE\r\n" : "ERR CAPTURE\r\n");
    return;
  }
  if (s_capture_count == 0u)
  {
    sc_bt_bench_command_error("ERR CAPTURE\r\n");
    return;
  }
  s_capture_export_index = 0u;
  s_capture_export_active = 1u;
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

static int16_t sc_bt_bench_apply_motor_sign(int16_t value, int8_t sign)
{
  return (sign < 0) ? (int16_t)-value : value;
}

static void sc_bt_bench_service_motion(uint32_t now_ms)
{
  sc_speed_controller_status_t speed_status;
  int16_t left_pwm;
  int16_t right_pwm;

  if ((s_motor_active == 0u) ||
      (s_experiment_mode != SC_BT_BENCH_EXPERIMENT_SPEED))
  {
    return;
  }
  if (sc_bt_bench_speed_feedback_fresh(now_ms) == 0u)
  {
    s_last_fault_bits |= SC_BT_BENCH_FAULT_FEEDBACK;
    sc_bt_bench_capture_sample(now_ms);
    (void)sc_bt_bench_safe_stop();
    sc_bt_bench_send("ERR FEEDBACK\r\n");
    return;
  }
  if (s_feedback_updated == 0u)
  {
    return;
  }

  speed_status = sc_speed_controller_step(
    &s_speed_controller, s_target_left_mm_s, s_target_right_mm_s,
    &s_wheel, now_ms, &s_speed_output);
  if ((speed_status != SC_SPEED_CONTROLLER_OK) &&
      (speed_status != SC_SPEED_CONTROLLER_WAIT))
  {
    s_last_fault_bits |= SC_BT_BENCH_FAULT_CONTROL;
    sc_bt_bench_capture_sample(now_ms);
    (void)sc_bt_bench_safe_stop();
    sc_bt_bench_send("ERR SPEED\r\n");
    return;
  }
  left_pwm = sc_bt_bench_apply_motor_sign(
    s_speed_output.left_permille, SC_MOTOR_LEFT_MOTOR_SIGN);
  right_pwm = sc_bt_bench_apply_motor_sign(
    s_speed_output.right_permille, SC_MOTOR_RIGHT_MOTOR_SIGN);
  if (sc_tb6612_apply(left_pwm, right_pwm) != SC_TB6612_OK)
  {
    s_last_fault_bits |= SC_BT_BENCH_FAULT_TB;
    sc_bt_bench_capture_sample(now_ms);
    (void)sc_bt_bench_safe_stop();
    sc_bt_bench_send("ERR MOTOR\r\n");
  }
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
  else if ((sc_bt_bench_text_equal(tokens[0], "LINE")) ||
           (sc_bt_bench_text_equal(tokens[0], "LF")))
  {
    sc_bt_bench_handle_line(token_count, now_ms);
  }
  else if (sc_bt_bench_text_equal(tokens[0], "ARM"))
  {
    sc_bt_bench_handle_arm(now_ms, token_count);
  }
  else if (sc_bt_bench_text_equal(tokens[0], "MOTOR"))
  {
    sc_bt_bench_handle_motor(tokens, token_count, now_ms);
  }
  else if (sc_bt_bench_text_equal(tokens[0], "RAW"))
  {
    sc_bt_bench_handle_raw(tokens, token_count, now_ms);
  }
  else if (sc_bt_bench_text_equal(tokens[0], "SPEED"))
  {
    sc_bt_bench_handle_speed(tokens, token_count, now_ms);
  }
  else if (sc_bt_bench_text_equal(tokens[0], "PID"))
  {
    sc_bt_bench_handle_pid(tokens, token_count);
  }
  else if (sc_bt_bench_text_equal(tokens[0], "CAPTURE"))
  {
    sc_bt_bench_handle_capture(token_count);
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
      sc_bt_bench_send(
        "OK HELP PING STATUS LINE LF ARM MOTOR RAW SPEED PID CAPTURE "
        "STOP RESET\r\n");
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
  sc_speed_controller_config_t speed_config;

  s_initialized = 0u;
  sc_bt_bench_reset_parser();
  s_armed = 0u;
  s_arm_deadline_ms = 0u;
  s_motor_active = 0u;
  s_motor_deadline_ms = 0u;
  s_experiment_mode = SC_BT_BENCH_EXPERIMENT_NONE;
  s_target_left_mm_s = 0;
  s_target_right_mm_s = 0;
  s_last_encoder_sequence = 0u;
  s_encoder_has_sequence = 0u;
  s_feedback_updated = 0u;
  s_capture_count = 0u;
  s_capture_start_ms = now_ms;
  s_capture_last_sample_ms = now_ms;
  s_capture_has_sample = 0u;
  s_capture_export_active = 0u;
  s_capture_export_index = 0u;
  s_last_fault_bits = 0u;
  s_calibration = (sc_wheel_calibration_t){
    .left_encoder_sign = SC_MOTOR_LEFT_ENCODER_SIGN,
    .right_encoder_sign = SC_MOTOR_RIGHT_ENCODER_SIGN,
    .left_counts_per_rev = SC_MOTOR_LEFT_COUNTS_PER_REV,
    .right_counts_per_rev = SC_MOTOR_RIGHT_COUNTS_PER_REV,
    .left_wheel_diameter_um = SC_MOTOR_LEFT_WHEEL_DIAMETER_UM,
    .right_wheel_diameter_um = SC_MOTOR_RIGHT_WHEEL_DIAMETER_UM
  };
  speed_config.left = (sc_speed_gains_t){
    .kp_milli = SC_MOTOR_LEFT_KP_MILLI,
    .ki_milli = SC_MOTOR_LEFT_KI_MILLI,
    .kd_milli = SC_MOTOR_LEFT_KD_MILLI,
    .ff_milli = SC_MOTOR_LEFT_FF_MILLI
  };
  speed_config.right = (sc_speed_gains_t){
    .kp_milli = SC_MOTOR_RIGHT_KP_MILLI,
    .ki_milli = SC_MOTOR_RIGHT_KI_MILLI,
    .kd_milli = SC_MOTOR_RIGHT_KD_MILLI,
    .ff_milli = SC_MOTOR_RIGHT_FF_MILLI
  };
  s_wheel_ready = (uint8_t)(
    sc_wheel_service_init(&s_wheel_service, &s_calibration) ==
    SC_WHEEL_SERVICE_OK);
  s_speed_ready = (uint8_t)(
    sc_speed_controller_init(&s_speed_controller, &speed_config) ==
    SC_SPEED_CONTROLLER_OK);
  s_next_heartbeat_ms = now_ms + SC_BT_BENCH_HEARTBEAT_MS;
  (void)sc_tb6612_set_standby(0u);
  if (sc_uart_get_stats(&s_uart_stats) != SC_UART_OK)
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
  s_current_service_ms = now_ms;

  if (sc_uart_get_stats(&current_stats) != SC_UART_OK)
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

  if (sc_bt_bench_update_feedback(now_ms) == 0u)
  {
    if (s_motor_active != 0u)
    {
      s_last_fault_bits |= SC_BT_BENCH_FAULT_FEEDBACK;
      (void)sc_bt_bench_safe_stop();
      sc_bt_bench_send("ERR IO\r\n");
      activity = 1u;
    }
  }

  if ((s_motor_active != 0u) &&
      sc_bt_bench_time_reached(now_ms, s_motor_deadline_ms))
  {
    sc_bt_bench_capture_sample(now_ms);
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
    if (sc_uart_read(chunk, sizeof(chunk), &received) != SC_UART_OK)
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

  sc_bt_bench_service_motion(now_ms);
  sc_bt_bench_capture_sample(now_ms);
  if (sc_bt_bench_service_capture_export() != 0u)
  {
    activity = 1u;
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
