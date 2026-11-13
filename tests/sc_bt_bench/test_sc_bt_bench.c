#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fake_bench.h"
#include "sc_bt_bench.h"

#define CHECK(condition) \
  do { \
    if (!(condition)) { \
      fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
              __FILE__, __LINE__, #condition); \
      return 1; \
    } \
  } while (0)

static int start_bench(uint32_t now_ms)
{
  fake_bench_reset();
  sc_bt_bench_init(now_ms);
  fake_bench_clear_tx();
  fake_bench_clear_calls();
  return 0;
}

static void inject_text(const char *text)
{
  fake_bench_inject_rx(text, strlen(text));
}

static int prime_wheel_feedback(void)
{
  uint32_t now_ms;

  for (now_ms = 0u; now_ms <= 50u; now_ms += 10u)
  {
    fake_bench_advance_encoder(now_ms, -20, 20);
    sc_bt_bench_service(now_ms);
  }
  fake_bench_clear_tx();
  fake_bench_clear_calls();
  return 0;
}

static uint16_t capture_row_fault(const char *line)
{
  const char *cursor = line;
  int comma_count = 0;

  if (strncmp(line, "CAP,", 4u) != 0)
  {
    return UINT16_MAX;
  }
  while (*cursor != '\0')
  {
    if (*cursor == ',')
    {
      comma_count++;
      if (comma_count == 9)
      {
        return (uint16_t)strtoul(cursor + 1, NULL, 10);
      }
    }
    cursor++;
  }
  return UINT16_MAX;
}

static int export_capture_fault_mask(uint32_t start_ms, uint16_t *fault_mask)
{
  size_t capture_lines = 0u;
  size_t loops = 0u;
  uint16_t faults = 0u;
  int ended = 0;

  inject_text("CAPTURE\r\n");
  while ((loops < 30u) && (ended == 0))
  {
    size_t index;

    sc_bt_bench_service(start_ms + (uint32_t)loops);
    for (index = 0u; index < fake_bench_tx_count(); index++)
    {
      const char *line = fake_bench_tx_at(index);

      if (strncmp(line, "CAP,", 4u) == 0)
      {
        capture_lines++;
        faults |= capture_row_fault(line);
      }
      else if (strcmp(line, "OK CAPTURE END\r\n") == 0)
      {
        ended = 1;
      }
    }
    fake_bench_clear_tx();
    loops++;
  }
  *fault_mask = faults;
  return (capture_lines != 0u) && (ended != 0);
}

static int tx_equals(size_t index, const char *expected)
{
  return strcmp(fake_bench_tx_at(index), expected) == 0;
}

static int test_init_safe_standby(void)
{
  sc_tb6612_state_t *tb;

  fake_bench_reset();
  tb = fake_bench_tb_state();
  tb->mode = SC_TB6612_MODE_ENABLED;
  tb->enabled = 1u;
  tb->standby = 1u;
  tb->left_command = 100;
  tb->right_command = -100;
  sc_bt_bench_init(0u);
  CHECK(fake_bench_standby_calls() == 1u);
  CHECK(fake_bench_last_standby_enable() == 0u);
  CHECK(tb->mode == SC_TB6612_MODE_STANDBY);
  CHECK(tb->standby == 0u);
  CHECK(tb->enabled == 0u);
  CHECK(tb->left_command == 0);
  CHECK(tb->right_command == 0);
  return 0;
}

static int test_ping_exact(void)
{
  CHECK(start_bench(0u) == 0);
  inject_text("PING\r\n");
  sc_bt_bench_service(0u);
  CHECK(fake_bench_tx_count() == 1u);
  CHECK(tx_equals(0u, "OK PONG\r\n"));
  return 0;
}

static int test_status_fields(void)
{
  sc_tb6612_state_t *tb;
  sc_encoder_snapshot_t *encoder;
  const char *response;

  CHECK(start_bench(0u) == 0);
  tb = fake_bench_tb_state();
  tb->mode = SC_TB6612_MODE_ENABLED;
  tb->enabled = 1u;
  tb->standby = 1u;
  tb->left_command = -25;
  tb->right_command = 75;
  encoder = fake_bench_encoder_snapshot();
  encoder->left.raw_count = UINT16_C(0xFFFF);
  encoder->left.total_count = INT64_MIN;
  encoder->left.delta_count = INT32_MIN;
  encoder->left.valid = 0u;
  encoder->right.raw_count = 42u;
  encoder->right.total_count = 123456789;
  encoder->right.delta_count = -17;
  encoder->right.valid = 1u;

  inject_text("STATUS\r\n");
  sc_bt_bench_service(0u);
  CHECK(fake_bench_tx_count() == 1u);
  response = fake_bench_tx_at(0u);
  CHECK(strstr(response, "STATUS mode=ENABLED stby=1 L=-25 R=75") != NULL);
  CHECK(strstr(response, "Lraw=65535") != NULL);
  CHECK(strstr(response, "Ltotal=-9223372036854775808") != NULL);
  CHECK(strstr(response, "Ldelta=-2147483648") != NULL);
  CHECK(strstr(response, "Lvalid=0") != NULL);
  CHECK(strstr(response, "Rraw=42") != NULL);
  CHECK(strstr(response, "Rtotal=123456789") != NULL);
  CHECK(strstr(response, "Rdelta=-17") != NULL);
  CHECK(strstr(response, "Rvalid=1 ") != NULL);
  CHECK(strstr(response, "Ltarget=0") != NULL);
  CHECK(strstr(response, "Rspeed=0") != NULL);
  CHECK(strstr(response, "Lkp=100") != NULL);
  CHECK(strstr(response, "Lki=750") != NULL);
  CHECK(strstr(response, "Lff=803") != NULL);
  CHECK(strstr(response, "Rkp=100") != NULL);
  CHECK(strstr(response, "Rki=750") != NULL);
  CHECK(strstr(response, "Rff=803") != NULL);
  CHECK(strstr(response, "CAP=0") != NULL);
  CHECK(strstr(response, "PARAM=UNVERIFIED") != NULL);
  CHECK(fake_bench_standby_calls() == 0u);
  return 0;
}

static int test_line_exact(void)
{
  sc_line_state_t *line;

  CHECK(start_bench(100u) == 0);
  line = fake_bench_line_state();
  line->sample.raw_levels = 0x08u;
  line->sample.sample_time_ms = 100u;
  line->sample.sequence = 42u;
  line->sample.valid = 1u;
  line->black_mask = 0x07u;
  line->semantic_state = SC_LINE_LEFT_RIGHT_ANGLE_CANDIDATE;

  inject_text("LINE\r\n");
  sc_bt_bench_service(123u);
  CHECK(fake_bench_tx_count() == 1u);
  CHECK(tx_equals(
    0u,
    "LINE raw=0001 raw_hex=0x08 black_mask=0x07 "
    "state=SC_LINE_LEFT_RIGHT_ANGLE_CANDIDATE valid=1 "
    "t=100 seq=42 age=23\r\n"));
  CHECK(fake_bench_standby_calls() == 0u);
  CHECK(fake_bench_apply_calls() == 0u);
  return 0;
}

static int test_line_alias_fragmented(void)
{
  CHECK(start_bench(0u) == 0);
  inject_text("L");
  sc_bt_bench_service(0u);
  CHECK(fake_bench_tx_count() == 0u);
  inject_text("F\r");
  sc_bt_bench_service(10u);
  CHECK(fake_bench_tx_count() == 1u);
  CHECK(strncmp(fake_bench_tx_at(0u), "LINE raw=1001 ", 14u) == 0);
  CHECK(strstr(fake_bench_tx_at(0u),
               "state=SC_LINE_STRAIGHT valid=1") != NULL);
  CHECK(fake_bench_standby_calls() == 0u);
  return 0;
}

static int test_line_candidate_states_valid(void)
{
  static const struct {
    uint8_t raw;
    uint8_t mask;
    sc_line_class_t state;
    const char *name;
  } cases[] = {
    {0x0Fu, 0x00u, SC_LINE_LOST_CANDIDATE, "SC_LINE_LOST_CANDIDATE"},
    {0x00u, 0x0Fu, SC_LINE_JUNCTION_CANDIDATE,
     "SC_LINE_JUNCTION_CANDIDATE"},
    {0x02u, 0x0Du, SC_LINE_UNKNOWN_PATTERN, "SC_LINE_UNKNOWN_PATTERN"}
  };
  sc_line_state_t *line;
  size_t index;

  CHECK(start_bench(0u) == 0);
  line = fake_bench_line_state();
  for (index = 0u; index < (sizeof(cases) / sizeof(cases[0])); index++)
  {
    fake_bench_clear_tx();
    fake_bench_clear_calls();
    line->sample.raw_levels = cases[index].raw;
    line->black_mask = cases[index].mask;
    line->sample.valid = 1u;
    line->semantic_state = cases[index].state;
    inject_text("LINE\r\n");
    sc_bt_bench_service((uint32_t)(index * 10u));
    CHECK(fake_bench_tx_count() == 1u);
    CHECK(strncmp(fake_bench_tx_at(0u), "LINE raw=", 9u) == 0);
    CHECK(strstr(fake_bench_tx_at(0u), cases[index].name) != NULL);
    CHECK(strstr(fake_bench_tx_at(0u), "valid=1") != NULL);
    CHECK(fake_bench_standby_calls() == 0u);
    CHECK(fake_bench_apply_calls() == 0u);
  }
  return 0;
}

static int test_line_does_not_extend_motion(void)
{
  CHECK(start_bench(0u) == 0);
  inject_text("ARM\r\nMOTOR L 100 100\r\n");
  sc_bt_bench_service(0u);
  CHECK(fake_bench_apply_calls() == 1u);
  fake_bench_clear_calls();
  fake_bench_clear_tx();

  inject_text("LINE\r\n");
  sc_bt_bench_service(50u);
  CHECK(fake_bench_tx_count() == 1u);
  CHECK(strncmp(fake_bench_tx_at(0u), "LINE ", 5u) == 0);
  CHECK(fake_bench_standby_calls() == 0u);
  CHECK(fake_bench_apply_calls() == 0u);

  sc_bt_bench_service(99u);
  CHECK(fake_bench_standby_calls() == 0u);
  sc_bt_bench_service(100u);
  CHECK(fake_bench_standby_calls() == 1u);
  CHECK(fake_bench_last_standby_enable() == 0u);
  CHECK(tx_equals(1u, "OK STOP AUTO\r\n"));
  return 0;
}

static int test_line_invalid_snapshot_safe_stop(void)
{
  sc_line_state_t *line;

  CHECK(start_bench(0u) == 0);
  line = fake_bench_line_state();
  line->sample.valid = 0u;
  line->black_mask = 0u;
  line->semantic_state = SC_LINE_INVALID;
  inject_text("LINE\r\n");
  sc_bt_bench_service(10u);
  CHECK(fake_bench_standby_calls() == 1u);
  CHECK(fake_bench_last_standby_enable() == 0u);
  CHECK(fake_bench_tx_count() == 1u);
  CHECK(tx_equals(0u, "ERR IO\r\n"));

  fake_bench_clear_calls();
  fake_bench_clear_tx();
  line->sample.valid = 1u;
  line->sample.raw_levels = 0x19u;
  line->black_mask = 0x06u;
  line->semantic_state = SC_LINE_STRAIGHT;
  inject_text("LF\r\n");
  sc_bt_bench_service(20u);
  CHECK(fake_bench_standby_calls() == 1u);
  CHECK(tx_equals(0u, "ERR IO\r\n"));
  return 0;
}

static int test_line_unavailable_and_bad_argument(void)
{
  CHECK(start_bench(0u) == 0);
  fake_bench_fail_line_snapshot();
  inject_text("LINE\r\n");
  sc_bt_bench_service(10u);
  CHECK(fake_bench_standby_calls() == 1u);
  CHECK(fake_bench_last_standby_enable() == 0u);
  CHECK(tx_equals(0u, "ERR IO\r\n"));

  fake_bench_clear_calls();
  fake_bench_clear_tx();
  inject_text("LINE extra\r\n");
  sc_bt_bench_service(20u);
  CHECK(fake_bench_standby_calls() == 1u);
  CHECK(tx_equals(0u, "ERR CMD\r\n"));

  fake_bench_clear_calls();
  fake_bench_clear_tx();
  fake_bench_line_state()->semantic_state = (sc_line_class_t)99;
  inject_text("LINE\r\n");
  sc_bt_bench_service(30u);
  CHECK(fake_bench_standby_calls() == 1u);
  CHECK(tx_equals(0u, "ERR IO\r\n"));
  return 0;
}

static int test_line_help_and_tx_full(void)
{
  CHECK(start_bench(0u) == 0);
  inject_text("HELP\r\n");
  sc_bt_bench_service(0u);
  CHECK(fake_bench_tx_count() == 1u);
  CHECK(tx_equals(
    0u, "OK HELP PING STATUS LINE LF ARM MOTOR RAW SPEED PID CAPTURE "
        "STOP RESET\r\n"));

  fake_bench_clear_tx();
  fake_bench_clear_calls();
  inject_text("ARM\r\nMOTOR L 100 100\r\n");
  sc_bt_bench_service(10u);
  fake_bench_clear_tx();
  fake_bench_clear_calls();
  fake_bench_fail_next_write();
  inject_text("LINE\r\n");
  sc_bt_bench_service(50u);
  CHECK(fake_bench_tx_count() == 0u);
  CHECK(fake_bench_standby_calls() == 1u);
  CHECK(fake_bench_last_standby_enable() == 0u);
  CHECK(fake_bench_tb_state()->mode == SC_TB6612_MODE_STANDBY);
  sc_bt_bench_service(110u);
  CHECK(fake_bench_tx_count() == 0u);
  return 0;
}

static int test_fragmented_crlf(void)
{
  CHECK(start_bench(0u) == 0);
  inject_text("PI");
  sc_bt_bench_service(0u);
  CHECK(fake_bench_tx_count() == 0u);
  inject_text("NG\r");
  sc_bt_bench_service(10u);
  CHECK(fake_bench_tx_count() == 1u);
  CHECK(tx_equals(0u, "OK PONG\r\n"));
  inject_text("\n");
  sc_bt_bench_service(20u);
  CHECK(fake_bench_tx_count() == 1u);
  return 0;
}

static int test_max_line_exact(void)
{
  char line[SC_BT_BENCH_MAX_LINE_BYTES + 3u];
  size_t index;

  CHECK(start_bench(0u) == 0);
  for (index = 0u; index < SC_BT_BENCH_MAX_LINE_BYTES; index++)
  {
    line[index] = 'A';
  }
  line[SC_BT_BENCH_MAX_LINE_BYTES] = '\r';
  line[SC_BT_BENCH_MAX_LINE_BYTES + 1u] = '\n';
  line[SC_BT_BENCH_MAX_LINE_BYTES + 2u] = '\0';
  fake_bench_inject_rx(line, strlen(line));
  sc_bt_bench_service(0u);
  CHECK(fake_bench_tx_count() == 1u);
  CHECK(tx_equals(0u, "ERR CMD\r\n"));
  CHECK(fake_bench_standby_calls() == 1u);
  CHECK(fake_bench_last_standby_enable() == 0u);
  return 0;
}

static int test_overlong_safe_stop(void)
{
  char line[SC_BT_BENCH_MAX_LINE_BYTES + 4u];
  size_t index;

  CHECK(start_bench(0u) == 0);
  for (index = 0u; index <= SC_BT_BENCH_MAX_LINE_BYTES; index++)
  {
    line[index] = 'A';
  }
  line[SC_BT_BENCH_MAX_LINE_BYTES + 1u] = '\r';
  line[SC_BT_BENCH_MAX_LINE_BYTES + 2u] = '\n';
  line[SC_BT_BENCH_MAX_LINE_BYTES + 3u] = '\0';
  fake_bench_inject_rx(line, strlen(line));
  sc_bt_bench_service(0u);
  CHECK(fake_bench_tx_count() == 1u);
  CHECK(tx_equals(0u, "ERR LINE\r\n"));
  CHECK(fake_bench_standby_calls() == 1u);
  CHECK(fake_bench_last_standby_enable() == 0u);
  return 0;
}

static int test_overlong_resync(void)
{
  char stream[SC_BT_BENCH_MAX_LINE_BYTES + 1u + 12u];
  size_t index;
  size_t command_offset = SC_BT_BENCH_MAX_LINE_BYTES + 1u;

  CHECK(start_bench(0u) == 0);
  for (index = 0u; index < command_offset; index++)
  {
    stream[index] = 'A';
  }
  memcpy(&stream[command_offset], "PING\r\nPING\r\n", 12u);
  fake_bench_inject_rx(stream, sizeof(stream));
  sc_bt_bench_service(0u);
  CHECK(fake_bench_tx_count() == 2u);
  CHECK(tx_equals(0u, "ERR LINE\r\n"));
  CHECK(tx_equals(1u, "OK PONG\r\n"));
  CHECK(fake_bench_standby_calls() == 1u);
  CHECK(fake_bench_last_standby_enable() == 0u);
  return 0;
}

static int test_invalid_byte_safe_stop(void)
{
  static const uint8_t invalid_stream[] = {
    'P', 'I', 'N', 'G', 0u, 'j', 'u', 'n', 'k', '\r', '\n'
  };
  sc_tb6612_state_t *tb;

  CHECK(start_bench(0u) == 0);
  fake_bench_inject_rx((const char *)invalid_stream,
                       sizeof(invalid_stream));
  sc_bt_bench_service(0u);
  CHECK(fake_bench_tx_count() == 1u);
  CHECK(tx_equals(0u, "ERR CHAR\r\n"));
  CHECK(fake_bench_standby_calls() == 1u);
  CHECK(fake_bench_last_standby_enable() == 0u);
  tb = fake_bench_tb_state();
  CHECK(tb->mode == SC_TB6612_MODE_STANDBY);
  CHECK(tb->standby == 0u);

  fake_bench_clear_tx();
  inject_text("PING\r\n");
  sc_bt_bench_service(10u);
  CHECK(fake_bench_tx_count() == 1u);
  CHECK(tx_equals(0u, "OK PONG\r\n"));
  return 0;
}

static int test_arm_expiry_boundary(void)
{
  sc_tb6612_state_t *tb;

  CHECK(start_bench(0u) == 0);
  inject_text("ARM\r\n");
  sc_bt_bench_service(0u);
  CHECK(fake_bench_tx_count() == 1u);
  CHECK(tx_equals(0u, "OK ARMED 5000\r\n"));
  CHECK(fake_bench_standby_calls() == 1u);
  CHECK(fake_bench_last_standby_enable() == 1u);
  tb = fake_bench_tb_state();
  CHECK(tb->mode == SC_TB6612_MODE_ENABLED);

  sc_bt_bench_service(4999u);
  CHECK(fake_bench_standby_calls() == 1u);
  CHECK(tb->mode == SC_TB6612_MODE_ENABLED);
  sc_bt_bench_service(5000u);
  CHECK(fake_bench_standby_calls() == 2u);
  CHECK(fake_bench_last_standby_enable() == 0u);
  CHECK(tb->mode == SC_TB6612_MODE_STANDBY);
  CHECK(tb->standby == 0u);
  return 0;
}

static int test_arm_tick_wrap(void)
{
  const uint32_t start = UINT32_MAX - 1999u;
  sc_tb6612_state_t *tb;

  CHECK(start_bench(start) == 0);
  inject_text("ARM\r\n");
  sc_bt_bench_service(start);
  CHECK(fake_bench_standby_calls() == 1u);
  CHECK(fake_bench_last_standby_enable() == 1u);
  fake_bench_clear_calls();

  sc_bt_bench_service((uint32_t)(start + 4999u));
  CHECK(fake_bench_standby_calls() == 0u);
  tb = fake_bench_tb_state();
  CHECK(tb->mode == SC_TB6612_MODE_ENABLED);

  sc_bt_bench_service((uint32_t)(start + 5000u));
  CHECK(fake_bench_standby_calls() == 1u);
  CHECK(fake_bench_last_standby_enable() == 0u);
  CHECK(tb->mode == SC_TB6612_MODE_STANDBY);
  return 0;
}

static int test_motor_bounds_single_wheel(void)
{
  sc_tb6612_state_t *tb;

  CHECK(start_bench(0u) == 0);
  inject_text("ARM\r\nMOTOR L +150 100\r\n");
  sc_bt_bench_service(0u);
  CHECK(fake_bench_apply_calls() == 1u);
  CHECK(fake_bench_last_left() == 150);
  CHECK(fake_bench_last_right() == 0);
  CHECK(fake_bench_tx_count() == 2u);
  CHECK(tx_equals(0u, "OK ARMED 5000\r\n"));
  CHECK(tx_equals(1u, "OK MOTOR\r\n"));

  sc_bt_bench_service(99u);
  CHECK(fake_bench_standby_calls() == 1u);
  sc_bt_bench_service(100u);
  CHECK(fake_bench_standby_calls() == 2u);
  CHECK(fake_bench_last_standby_enable() == 0u);
  tb = fake_bench_tb_state();
  CHECK(tb->mode == SC_TB6612_MODE_STANDBY);

  fake_bench_clear_calls();
  fake_bench_clear_tx();
  inject_text("ARM\r\nMOTOR R -150 2000\r\n");
  sc_bt_bench_service(100u);
  CHECK(fake_bench_apply_calls() == 1u);
  CHECK(fake_bench_last_left() == 0);
  CHECK(fake_bench_last_right() == -150);
  sc_bt_bench_service(2099u);
  CHECK(fake_bench_standby_calls() == 1u);
  sc_bt_bench_service(2100u);
  CHECK(fake_bench_standby_calls() == 2u);
  CHECK(fake_bench_last_standby_enable() == 0u);
  return 0;
}

static int test_motor_bounds_rejected(void)
{
  CHECK(start_bench(0u) == 0);

  inject_text("ARM\r\nMOTOR L 151 100\r\n");
  sc_bt_bench_service(0u);
  CHECK(fake_bench_apply_calls() == 0u);
  CHECK(fake_bench_standby_calls() == 2u);
  CHECK(fake_bench_last_standby_enable() == 0u);
  CHECK(tx_equals(1u, "ERR ARG\r\n"));

  fake_bench_clear_calls();
  fake_bench_clear_tx();
  inject_text("ARM\r\nMOTOR L -151 100\r\n");
  sc_bt_bench_service(10u);
  CHECK(fake_bench_apply_calls() == 0u);
  CHECK(fake_bench_standby_calls() == 2u);
  CHECK(tx_equals(1u, "ERR ARG\r\n"));

  fake_bench_clear_calls();
  fake_bench_clear_tx();
  inject_text("ARM\r\nMOTOR L 100 99\r\n");
  sc_bt_bench_service(20u);
  CHECK(fake_bench_apply_calls() == 0u);
  CHECK(fake_bench_standby_calls() == 2u);
  CHECK(tx_equals(1u, "ERR ARG\r\n"));

  fake_bench_clear_calls();
  fake_bench_clear_tx();
  inject_text("ARM\r\nMOTOR L 100 2001\r\n");
  sc_bt_bench_service(30u);
  CHECK(fake_bench_apply_calls() == 0u);
  CHECK(fake_bench_standby_calls() == 2u);
  CHECK(tx_equals(1u, "ERR ARG\r\n"));
  return 0;
}

static int test_repeated_motion_safe_stop(void)
{
  CHECK(start_bench(0u) == 0);
  inject_text("ARM\r\nMOTOR L 100 1000\r\n");
  sc_bt_bench_service(0u);
  CHECK(fake_bench_apply_calls() == 1u);
  fake_bench_clear_tx();
  fake_bench_clear_calls();

  inject_text("MOTOR R 50 100\r\n");
  sc_bt_bench_service(10u);
  CHECK(fake_bench_apply_calls() == 0u);
  CHECK(fake_bench_standby_calls() == 1u);
  CHECK(fake_bench_last_standby_enable() == 0u);
  CHECK(fake_bench_tx_count() == 1u);
  CHECK(tx_equals(0u, "ERR ACTIVE\r\n"));
  return 0;
}

static int test_unarmed_motion_safe_stop(void)
{
  CHECK(start_bench(0u) == 0);
  inject_text("MOTOR L 100 100\r\n");
  sc_bt_bench_service(0u);
  CHECK(fake_bench_apply_calls() == 0u);
  CHECK(fake_bench_standby_calls() == 1u);
  CHECK(fake_bench_last_standby_enable() == 0u);
  CHECK(fake_bench_tx_count() == 1u);
  CHECK(tx_equals(0u, "ERR ARM\r\n"));
  return 0;
}

static int test_auto_stop_deadline(void)
{
  sc_tb6612_state_t *tb;

  CHECK(start_bench(0u) == 0);
  inject_text("ARM\r\nMOTOR L 120 100\r\n");
  sc_bt_bench_service(0u);
  CHECK(fake_bench_apply_calls() == 1u);
  CHECK(fake_bench_standby_calls() == 1u);
  fake_bench_clear_tx();

  sc_bt_bench_service(99u);
  CHECK(fake_bench_standby_calls() == 1u);
  sc_bt_bench_service(100u);
  CHECK(fake_bench_standby_calls() == 2u);
  CHECK(fake_bench_last_standby_enable() == 0u);
  CHECK(fake_bench_tx_count() == 1u);
  CHECK(tx_equals(0u, "OK STOP AUTO\r\n"));
  tb = fake_bench_tb_state();
  CHECK(tb->mode == SC_TB6612_MODE_STANDBY);
  CHECK(tb->enabled == 0u);
  return 0;
}

static int test_auto_stop_driver_failure(void)
{
  CHECK(start_bench(0u) == 0);
  inject_text("ARM\r\nMOTOR L 120 100\r\n");
  sc_bt_bench_service(0u);
  CHECK(fake_bench_apply_calls() == 1u);
  fake_bench_clear_tx();
  fake_bench_clear_calls();

  fake_bench_fail_next_standby_disable();
  sc_bt_bench_service(100u);
  CHECK(fake_bench_standby_calls() == 1u);
  CHECK(fake_bench_last_standby_enable() == 0u);
  CHECK(fake_bench_tx_count() == 1u);
  CHECK(tx_equals(0u, "ERR MOTOR\r\n"));
  return 0;
}

static int test_motor_tick_wrap(void)
{
  const uint32_t start = UINT32_MAX - 499u;

  CHECK(start_bench(start) == 0);
  inject_text("ARM\r\nMOTOR L 150 1000\r\n");
  sc_bt_bench_service(start);
  CHECK(fake_bench_apply_calls() == 1u);
  CHECK(fake_bench_last_left() == 150);
  CHECK(fake_bench_standby_calls() == 1u);

  sc_bt_bench_service((uint32_t)(start + 999u));
  CHECK(fake_bench_standby_calls() == 1u);
  sc_bt_bench_service((uint32_t)(start + 1000u));
  CHECK(fake_bench_standby_calls() == 2u);
  CHECK(fake_bench_last_standby_enable() == 0u);
  return 0;
}

static int test_stop_command(void)
{
  sc_tb6612_state_t *tb;

  CHECK(start_bench(0u) == 0);
  inject_text("ARM\r\nMOTOR L 100 1000\r\n");
  sc_bt_bench_service(0u);
  fake_bench_clear_tx();
  fake_bench_clear_calls();
  inject_text("STOP\r\n");
  sc_bt_bench_service(10u);
  CHECK(fake_bench_standby_calls() == 1u);
  CHECK(fake_bench_last_standby_enable() == 0u);
  CHECK(fake_bench_tx_count() == 1u);
  CHECK(tx_equals(0u, "OK STOP\r\n"));
  tb = fake_bench_tb_state();
  CHECK(tb->mode == SC_TB6612_MODE_STANDBY);
  CHECK(tb->left_command == 0);
  CHECK(tb->right_command == 0);
  return 0;
}

static int test_reset_only_stopped(void)
{
  CHECK(start_bench(0u) == 0);
  inject_text("ARM\r\nMOTOR L 100 1000\r\n");
  sc_bt_bench_service(0u);
  fake_bench_clear_tx();
  fake_bench_clear_calls();

  inject_text("RESET\r\n");
  sc_bt_bench_service(10u);
  CHECK(fake_bench_encoder_reset_calls() == 0u);
  CHECK(fake_bench_standby_calls() == 1u);
  CHECK(tx_equals(0u, "ERR ACTIVE\r\n"));

  fake_bench_clear_tx();
  fake_bench_clear_calls();
  inject_text("RESET\r\n");
  sc_bt_bench_service(20u);
  CHECK(fake_bench_encoder_reset_calls() == 1u);
  CHECK(fake_bench_tx_count() == 1u);
  CHECK(tx_equals(0u, "OK RESET\r\n"));

  fake_bench_clear_tx();
  fake_bench_clear_calls();
  fake_bench_fail_next_encoder_reset();
  inject_text("RESET\r\n");
  sc_bt_bench_service(30u);
  CHECK(fake_bench_encoder_reset_calls() == 1u);
  CHECK(fake_bench_standby_calls() == 1u);
  CHECK(fake_bench_last_standby_enable() == 0u);
  CHECK(fake_bench_tx_count() == 1u);
  CHECK(tx_equals(0u, "ERR ENCODER\r\n"));
  return 0;
}

static int test_heartbeat_cadence(void)
{
  CHECK(start_bench(0u) == 0);
  sc_bt_bench_service(999u);
  CHECK(fake_bench_tx_count() == 0u);
  sc_bt_bench_service(1000u);
  CHECK(fake_bench_tx_count() == 1u);
  CHECK(strncmp(fake_bench_tx_at(0u), "HB mode=", 8u) == 0);
  CHECK(strstr(fake_bench_tx_at(0u), "\r\n") != NULL);
  sc_bt_bench_service(1999u);
  CHECK(fake_bench_tx_count() == 1u);
  sc_bt_bench_service(2000u);
  CHECK(fake_bench_tx_count() == 2u);
  CHECK(strncmp(fake_bench_tx_at(1u), "HB mode=", 8u) == 0);
  return 0;
}

static int test_heartbeat_suppressed_during_motion(void)
{
  CHECK(start_bench(0u) == 0);
  inject_text("ARM\r\nMOTOR L 100 1500\r\n");
  sc_bt_bench_service(0u);
  fake_bench_clear_tx();

  sc_bt_bench_service(999u);
  sc_bt_bench_service(1000u);
  sc_bt_bench_service(1001u);
  CHECK(fake_bench_tx_count() == 0u);
  CHECK(fake_bench_tb_state()->mode == SC_TB6612_MODE_ENABLED);
  return 0;
}

static int test_heartbeat_resumes_after_motion_stop(void)
{
  CHECK(start_bench(0u) == 0);
  inject_text("ARM\r\nMOTOR L 100 1000\r\n");
  sc_bt_bench_service(0u);
  fake_bench_clear_tx();

  sc_bt_bench_service(999u);
  sc_bt_bench_service(1000u);
  CHECK(fake_bench_tx_count() == 1u);
  CHECK(tx_equals(0u, "OK STOP AUTO\r\n"));
  CHECK(fake_bench_tb_state()->mode == SC_TB6612_MODE_STANDBY);

  fake_bench_clear_tx();
  sc_bt_bench_service(1010u);
  CHECK(fake_bench_tx_count() == 1u);
  CHECK(strncmp(fake_bench_tx_at(0u), "HB mode=", 8u) == 0);
  return 0;
}

static int test_capture_export_heartbeat_suppressed(void)
{
  uint32_t now_ms;
  uint32_t export_ms = 1001u;
  size_t capture_lines = 0u;
  size_t loops = 0u;
  int ended = 0;
  int saw_begin = 0;
  int saw_heartbeat = 0;

  CHECK(start_bench(0u) == 0);
  inject_text("ARM\r\nMOTOR L 100 100\r\n");
  sc_bt_bench_service(0u);
  for (now_ms = 10u; now_ms <= 100u; now_ms += 10u)
  {
    sc_bt_bench_service(now_ms);
    fake_bench_clear_tx();
  }
  CHECK(fake_bench_tb_state()->mode == SC_TB6612_MODE_STANDBY);
  fake_bench_clear_calls();
  fake_bench_clear_tx();

  inject_text("CAPTURE\r\n");
  sc_bt_bench_service(999u);
  CHECK(fake_bench_tx_count() == 3u);
  CHECK(tx_equals(0u, "OK CAPTURE BEGIN N=11\r\n"));
  saw_begin = 1;
  capture_lines = 2u;
  fake_bench_clear_tx();

  fake_bench_full_next_write();
  sc_bt_bench_service(1000u);
  CHECK(fake_bench_tx_count() == 0u);
  CHECK(fake_bench_standby_calls() == 0u);

  while ((loops < 10u) && (ended == 0))
  {
    size_t index;

    sc_bt_bench_service(export_ms++);
    for (index = 0u; index < fake_bench_tx_count(); index++)
    {
      const char *line = fake_bench_tx_at(index);

      if (strcmp(line, "OK CAPTURE BEGIN N=11\r\n") == 0)
      {
        saw_begin = 1;
      }
      else if (strncmp(line, "CAP,", 4u) == 0)
      {
        capture_lines++;
      }
      else if (strcmp(line, "OK CAPTURE END\r\n") == 0)
      {
        ended = 1;
      }
      else if (strncmp(line, "HB ", 3u) == 0)
      {
        saw_heartbeat = 1;
      }
    }
    fake_bench_clear_tx();
    loops++;
  }

  CHECK(ended == 1);
  CHECK(saw_begin == 1);
  CHECK(capture_lines == 11u);
  CHECK(saw_heartbeat == 0);
  CHECK(fake_bench_standby_calls() == 0u);
  return 0;
}

static int test_uart_error_resets_parser(void)
{
  CHECK(start_bench(0u) == 0);
  inject_text("ARM\r\nPIN");
  sc_bt_bench_service(0u);
  CHECK(fake_bench_standby_calls() == 1u);
  fake_bench_clear_calls();
  fake_bench_clear_tx();

  fake_bench_uart_stats()->rx_overflow_bytes = 1u;
  sc_bt_bench_service(10u);
  CHECK(fake_bench_standby_calls() == 1u);
  CHECK(fake_bench_last_standby_enable() == 0u);
  CHECK(fake_bench_tx_count() == 1u);
  CHECK(tx_equals(0u, "ERR UART\r\n"));

  fake_bench_clear_tx();
  inject_text("G\r\n");
  sc_bt_bench_service(20u);
  CHECK(fake_bench_tx_count() == 0u);
  inject_text("PING\r\n");
  sc_bt_bench_service(30u);
  CHECK(fake_bench_tx_count() == 1u);
  CHECK(tx_equals(0u, "OK PONG\r\n"));

  fake_bench_clear_tx();
  fake_bench_clear_calls();
  fake_bench_uart_stats()->rx_ore_events = 1u;
  sc_bt_bench_service(40u);
  CHECK(fake_bench_standby_calls() == 1u);
  CHECK(tx_equals(0u, "ERR UART\r\n"));

  fake_bench_clear_tx();
  fake_bench_clear_calls();
  fake_bench_uart_stats()->rx_fe_events = 1u;
  sc_bt_bench_service(50u);
  CHECK(fake_bench_standby_calls() == 1u);
  CHECK(tx_equals(0u, "ERR UART\r\n"));

  fake_bench_clear_tx();
  fake_bench_clear_calls();
  fake_bench_uart_stats()->rx_ne_events = 1u;
  sc_bt_bench_service(60u);
  CHECK(fake_bench_standby_calls() == 1u);
  CHECK(tx_equals(0u, "ERR UART\r\n"));

  fake_bench_clear_tx();
  fake_bench_clear_calls();
  fake_bench_uart_stats()->rx_pe_events = 1u;
  sc_bt_bench_service(70u);
  CHECK(fake_bench_standby_calls() == 1u);
  CHECK(tx_equals(0u, "ERR UART\r\n"));
  return 0;
}

static int test_uart_io_failure_safe_stop(void)
{
  CHECK(start_bench(0u) == 0);
  inject_text("ARM\r\nMOTOR L 120 1000\r\n");
  sc_bt_bench_service(0u);
  CHECK(fake_bench_apply_calls() == 1u);
  fake_bench_clear_tx();
  fake_bench_clear_calls();

  fake_bench_fail_next_stats();
  sc_bt_bench_service(10u);
  CHECK(fake_bench_standby_calls() == 1u);
  CHECK(fake_bench_last_standby_enable() == 0u);
  CHECK(fake_bench_tx_count() == 1u);
  CHECK(tx_equals(0u, "ERR UART\r\n"));

  sc_bt_bench_init(100u);
  fake_bench_clear_tx();
  fake_bench_clear_calls();
  fake_bench_fail_next_read();
  sc_bt_bench_service(110u);
  CHECK(fake_bench_standby_calls() == 1u);
  CHECK(fake_bench_last_standby_enable() == 0u);
  CHECK(fake_bench_tx_count() == 1u);
  CHECK(tx_equals(0u, "ERR UART\r\n"));
  return 0;
}

static int test_driver_failure_safe_stop(void)
{
  sc_tb6612_state_t *tb;

  CHECK(start_bench(0u) == 0);
  fake_bench_fail_next_standby_enable();
  inject_text("ARM\r\n");
  sc_bt_bench_service(0u);
  CHECK(fake_bench_standby_calls() == 2u);
  CHECK(fake_bench_last_standby_enable() == 0u);
  CHECK(tx_equals(0u, "ERR MOTOR\r\n"));

  fake_bench_clear_calls();
  fake_bench_clear_tx();
  inject_text("ARM\r\n");
  sc_bt_bench_service(10u);
  CHECK(fake_bench_last_standby_enable() == 1u);
  fake_bench_fail_next_apply();
  fake_bench_clear_calls();
  fake_bench_clear_tx();
  inject_text("MOTOR L 100 100\r\n");
  sc_bt_bench_service(20u);
  CHECK(fake_bench_apply_calls() == 1u);
  CHECK(fake_bench_standby_calls() == 1u);
  CHECK(fake_bench_last_standby_enable() == 0u);
  CHECK(tx_equals(0u, "ERR MOTOR\r\n"));
  tb = fake_bench_tb_state();
  CHECK(tb->mode == SC_TB6612_MODE_STANDBY);
  return 0;
}

static int test_tx_full_stop_still_safe(void)
{
  sc_tb6612_state_t *tb;

  CHECK(start_bench(0u) == 0);
  inject_text("ARM\r\nMOTOR L 100 1000\r\n");
  sc_bt_bench_service(0u);
  fake_bench_clear_calls();
  fake_bench_clear_tx();
  fake_bench_fail_next_write();
  inject_text("STOP\r\n");
  sc_bt_bench_service(10u);
  CHECK(fake_bench_standby_calls() == 2u);
  CHECK(fake_bench_last_standby_enable() == 0u);
  CHECK(fake_bench_tx_count() == 0u);
  tb = fake_bench_tb_state();
  CHECK(tb->mode == SC_TB6612_MODE_STANDBY);

  inject_text("STATUS\r\n");
  sc_bt_bench_service(20u);
  CHECK(fake_bench_tx_count() == 1u);
  CHECK(strstr(fake_bench_tx_at(0u), "mode=STANDBY stby=0 L=0 R=0") != NULL);
  return 0;
}

static int test_raw_requires_fresh_feedback(void)
{
  CHECK(start_bench(0u) == 0);
  inject_text("ARM\r\nRAW B 100 100\r\n");
  sc_bt_bench_service(0u);
  CHECK(fake_bench_apply_calls() == 0u);
  CHECK(tx_equals(0u, "OK ARMED 5000\r\n"));
  CHECK(tx_equals(1u, "ERR FEEDBACK\r\n"));
  CHECK(fake_bench_tb_state()->mode == SC_TB6612_MODE_STANDBY);
  return 0;
}

static int test_raw_requires_arm_and_limits(void)
{
  uint32_t now_ms;

  CHECK(start_bench(0u) == 0);
  CHECK(prime_wheel_feedback() == 0);
  fake_bench_advance_encoder(60u, -20, 20);
  sc_bt_bench_service(60u);
  fake_bench_clear_tx();
  fake_bench_clear_calls();

  inject_text("RAW B 564 100\r\n");
  sc_bt_bench_service(60u);
  CHECK(fake_bench_apply_calls() == 0u);
  CHECK(tx_equals(0u, "ERR ARM\r\n"));

  fake_bench_clear_tx();
  fake_bench_clear_calls();
  inject_text("ARM\r\nRAW B 565 100\r\n");
  sc_bt_bench_service(65u);
  CHECK(fake_bench_apply_calls() == 0u);
  CHECK(tx_equals(0u, "OK ARMED 5000\r\n"));
  CHECK(tx_equals(1u, "ERR ARG\r\n"));

  fake_bench_clear_tx();
  fake_bench_clear_calls();
  inject_text("ARM\r\nRAW L 100 99\r\n");
  sc_bt_bench_service(68u);
  CHECK(fake_bench_apply_calls() == 0u);
  CHECK(tx_equals(1u, "ERR ARG\r\n"));

  fake_bench_clear_tx();
  fake_bench_clear_calls();
  inject_text("ARM\r\nRAW R -564 2001\r\n");
  sc_bt_bench_service(70u);
  CHECK(fake_bench_apply_calls() == 0u);
  CHECK(tx_equals(1u, "ERR ARG\r\n"));

  fake_bench_clear_tx();
  fake_bench_clear_calls();
  inject_text("ARM\r\nRAW B 564 100\r\n");
  sc_bt_bench_service(70u);
  CHECK(fake_bench_apply_calls() == 1u);
  CHECK(fake_bench_last_left() == 564);
  CHECK(fake_bench_last_right() == 564);
  CHECK(tx_equals(1u, "OK RAW PARAM=UNVERIFIED\r\n"));
  for (now_ms = 80u; now_ms <= 170u; now_ms += 10u)
  {
    fake_bench_advance_encoder(now_ms, -20, 20);
    sc_bt_bench_service(now_ms);
  }
  CHECK(fake_bench_last_standby_enable() == 0u);
  CHECK(fake_bench_tb_state()->mode == SC_TB6612_MODE_STANDBY);
  return 0;
}

static int test_raw_stall_zero_progress(void)
{
  uint32_t now_ms;
  uint16_t fault_mask = 0u;

  CHECK(start_bench(0u) == 0);
  CHECK(prime_wheel_feedback() == 0);
  fake_bench_advance_encoder(60u, -20, 20);
  sc_bt_bench_service(60u);
  fake_bench_clear_tx();
  inject_text("ARM\r\nRAW B 300 600\r\n");
  sc_bt_bench_service(60u);
  fake_bench_clear_tx();
  fake_bench_clear_calls();

  for (now_ms = 70u; now_ms <= 370u; now_ms += 10u)
  {
    fake_bench_advance_encoder(now_ms, 0, 0);
    sc_bt_bench_service(now_ms);
    if (now_ms < 370u)
    {
      fake_bench_clear_tx();
    }
  }
  CHECK(fake_bench_tx_count() == 1u);
  CHECK(tx_equals(0u, "ERR STALL\r\n"));
  CHECK(fake_bench_last_standby_enable() == 0u);
  CHECK(fake_bench_tb_state()->mode == SC_TB6612_MODE_STANDBY);
  CHECK(export_capture_fault_mask(380u, &fault_mask) == 1);
  CHECK((fault_mask & ((1u << 4) | (1u << 5))) != 0u);
  return 0;
}

static int test_raw_stall_jitter_keeps_window(void)
{
  uint32_t now_ms;
  uint16_t fault_mask = 0u;

  CHECK(start_bench(0u) == 0);
  CHECK(prime_wheel_feedback() == 0);
  fake_bench_advance_encoder(60u, -20, 20);
  sc_bt_bench_service(60u);
  fake_bench_clear_tx();
  inject_text("ARM\r\nRAW L 300 600\r\n");
  sc_bt_bench_service(60u);
  fake_bench_clear_tx();
  fake_bench_clear_calls();

  for (now_ms = 70u; now_ms <= 370u; now_ms += 10u)
  {
    /* 左编码器符号为 -1，原始 -1 对应物理正向 +1。 */
    fake_bench_advance_encoder(
      now_ms, (now_ms == 80u) ? -1 : 0, 0);
    sc_bt_bench_service(now_ms);
    if (now_ms < 370u)
    {
      fake_bench_clear_tx();
    }
  }
  CHECK(fake_bench_tx_count() == 1u);
  CHECK(tx_equals(0u, "ERR STALL\r\n"));
  CHECK(export_capture_fault_mask(380u, &fault_mask) == 1);
  CHECK((fault_mask & (1u << 4)) != 0u);
  return 0;
}

static int test_raw_stall_five_per_window_fails(void)
{
  uint32_t now_ms;
  int saw_stall = 0;

  CHECK(start_bench(0u) == 0);
  CHECK(prime_wheel_feedback() == 0);
  fake_bench_advance_encoder(60u, -20, 20);
  sc_bt_bench_service(60u);
  fake_bench_clear_tx();
  inject_text("ARM\r\nRAW L 300 1200\r\n");
  sc_bt_bench_service(60u);
  fake_bench_clear_tx();
  fake_bench_clear_calls();

  for (now_ms = 70u; now_ms <= 900u; now_ms += 10u)
  {
    int32_t delta =
      ((now_ms == 100u) || (now_ms == 400u) || (now_ms == 700u)) ?
      -5 : 0;

    fake_bench_advance_encoder(now_ms, delta, 0);
    sc_bt_bench_service(now_ms);
    if ((fake_bench_tx_count() != 0u) &&
        (strstr(fake_bench_tx_at(0u), "ERR STALL") != NULL))
    {
      saw_stall = 1;
      break;
    }
    fake_bench_clear_tx();
  }
  CHECK(saw_stall == 1);
  CHECK(fake_bench_last_standby_enable() == 0u);
  CHECK(fake_bench_tb_state()->mode == SC_TB6612_MODE_STANDBY);
  return 0;
}

static int test_raw_stall_real_progress_passes(void)
{
  uint32_t now_ms;
  uint16_t fault_mask = 0u;
  int saw_stall = 0;

  CHECK(start_bench(0u) == 0);
  CHECK(prime_wheel_feedback() == 0);
  fake_bench_advance_encoder(60u, -20, 20);
  sc_bt_bench_service(60u);
  fake_bench_clear_tx();
  inject_text("ARM\r\nRAW L -300 600\r\n");
  sc_bt_bench_service(60u);
  fake_bench_clear_tx();
  fake_bench_clear_calls();

  for (now_ms = 70u; now_ms <= 660u; now_ms += 10u)
  {
    /* 负命令要求物理计数持续减少；原始计数递增经 -1 符号后为负向。 */
    fake_bench_advance_encoder(now_ms, 10, 0);
    sc_bt_bench_service(now_ms);
    if ((fake_bench_tx_count() != 0u) &&
        (strstr(fake_bench_tx_at(0u), "ERR STALL") != NULL))
    {
      saw_stall = 1;
    }
    fake_bench_clear_tx();
  }
  CHECK(saw_stall == 0);
  CHECK(fake_bench_tb_state()->mode == SC_TB6612_MODE_STANDBY);
  CHECK(export_capture_fault_mask(670u, &fault_mask) == 1);
  CHECK(fault_mask == 0u);
  return 0;
}

static int test_raw_feedback_lost_sync_stops(void)
{
  uint16_t fault_mask = 0u;

  CHECK(start_bench(0u) == 0);
  CHECK(prime_wheel_feedback() == 0);
  fake_bench_advance_encoder(60u, -20, 20);
  sc_bt_bench_service(60u);
  fake_bench_clear_tx();
  inject_text("ARM\r\nRAW L 100 600\r\n");
  sc_bt_bench_service(60u);
  CHECK(tx_equals(1u, "OK RAW PARAM=UNVERIFIED\r\n"));
  fake_bench_clear_tx();
  fake_bench_clear_calls();

  fake_bench_advance_encoder(70u, -20, 0);
  sc_bt_bench_service(70u);
  fake_bench_clear_tx();

  fake_bench_encoder_snapshot()->left.lost_sync = 1u;
  fake_bench_advance_encoder(80u, -20, 0);
  sc_bt_bench_service(80u);
  CHECK(fake_bench_tx_count() == 1u);
  CHECK(tx_equals(0u, "ERR FEEDBACK\r\n"));
  CHECK(fake_bench_last_standby_enable() == 0u);
  CHECK(fake_bench_tb_state()->mode == SC_TB6612_MODE_STANDBY);
  CHECK(export_capture_fault_mask(90u, &fault_mask) == 1);
  CHECK((fault_mask & (1u << 1)) != 0u);
  return 0;
}

static int test_raw_feedback_sequence_freeze_stops(void)
{
  uint16_t fault_mask = 0u;

  CHECK(start_bench(0u) == 0);
  CHECK(prime_wheel_feedback() == 0);
  fake_bench_advance_encoder(60u, -20, 20);
  sc_bt_bench_service(60u);
  fake_bench_clear_tx();
  inject_text("ARM\r\nRAW L 100 600\r\n");
  sc_bt_bench_service(60u);
  CHECK(tx_equals(1u, "OK RAW PARAM=UNVERIFIED\r\n"));
  fake_bench_clear_tx();
  fake_bench_clear_calls();

  sc_bt_bench_service(70u);
  sc_bt_bench_service(90u);
  CHECK(fake_bench_tx_count() == 0u);
  sc_bt_bench_service(100u);
  CHECK(fake_bench_tx_count() == 1u);
  CHECK(tx_equals(0u, "ERR FEEDBACK\r\n"));
  CHECK(fake_bench_last_standby_enable() == 0u);
  CHECK(fake_bench_tb_state()->mode == SC_TB6612_MODE_STANDBY);
  CHECK(export_capture_fault_mask(110u, &fault_mask) == 1);
  CHECK((fault_mask & (1u << 1)) != 0u);
  return 0;
}

static int test_speed_requires_arm_and_feedback(void)
{
  CHECK(start_bench(0u) == 0);
  CHECK(prime_wheel_feedback() == 0);
  fake_bench_advance_encoder(60u, -20, 20);
  sc_bt_bench_service(60u);
  fake_bench_clear_tx();
  fake_bench_clear_calls();

  inject_text("SPEED B 100 100\r\n");
  sc_bt_bench_service(70u);
  CHECK(fake_bench_apply_calls() == 0u);
  CHECK(tx_equals(0u, "ERR ARM\r\n"));

  fake_bench_clear_tx();
  fake_bench_clear_calls();
  inject_text("ARM\r\nSPEED B 501 100\r\n");
  sc_bt_bench_service(80u);
  CHECK(fake_bench_apply_calls() == 0u);
  CHECK(tx_equals(0u, "OK ARMED 5000\r\n"));
  CHECK(tx_equals(1u, "ERR ARG\r\n"));

  fake_bench_clear_tx();
  fake_bench_clear_calls();
  inject_text("ARM\r\nSPEED B 100 99\r\n");
  sc_bt_bench_service(85u);
  CHECK(fake_bench_apply_calls() == 0u);
  CHECK(tx_equals(1u, "ERR ARG\r\n"));

  fake_bench_clear_tx();
  fake_bench_clear_calls();
  inject_text("ARM\r\nSPEED B 100 2001\r\n");
  sc_bt_bench_service(90u);
  CHECK(fake_bench_apply_calls() == 0u);
  CHECK(tx_equals(1u, "ERR ARG\r\n"));

  CHECK(start_bench(0u) == 0);
  inject_text("ARM\r\nSPEED B 100 100\r\n");
  sc_bt_bench_service(0u);
  CHECK(fake_bench_apply_calls() == 0u);
  CHECK(tx_equals(0u, "OK ARMED 5000\r\n"));
  CHECK(tx_equals(1u, "ERR FEEDBACK\r\n"));
  CHECK(fake_bench_tb_state()->mode == SC_TB6612_MODE_STANDBY);
  return 0;
}

static int test_speed_closed_loop_auto_stop(void)
{
  uint32_t now_ms;

  CHECK(start_bench(0u) == 0);
  CHECK(prime_wheel_feedback() == 0);
  fake_bench_advance_encoder(60u, -20, 20);
  sc_bt_bench_service(60u);
  fake_bench_clear_tx();
  fake_bench_clear_calls();
  inject_text("ARM\r\nSPEED B 200 200\r\n");
  sc_bt_bench_service(60u);
  CHECK(tx_equals(0u, "OK ARMED 5000\r\n"));
  CHECK(tx_equals(1u, "OK SPEED PARAM=UNVERIFIED\r\n"));

  for (now_ms = 70u; now_ms <= 260u; now_ms += 10u)
  {
    fake_bench_advance_encoder(now_ms, -20, 20);
    sc_bt_bench_service(now_ms);
  }
  CHECK(fake_bench_last_standby_enable() == 0u);
  CHECK(fake_bench_tb_state()->mode == SC_TB6612_MODE_STANDBY);
  CHECK(strstr(fake_bench_tx_at(fake_bench_tx_count() - 1u),
               "OK STOP AUTO") != NULL);
  return 0;
}

static int test_speed_stall_zero_progress(void)
{
  uint32_t now_ms;
  uint16_t fault_mask = 0u;
  int saw_stall = 0;

  CHECK(start_bench(0u) == 0);
  CHECK(prime_wheel_feedback() == 0);
  fake_bench_advance_encoder(60u, 0, 0);
  sc_bt_bench_service(60u);
  fake_bench_clear_tx();
  inject_text("ARM\r\nSPEED B 200 600\r\n");
  sc_bt_bench_service(60u);
  fake_bench_clear_tx();
  fake_bench_clear_calls();

  for (now_ms = 70u; now_ms <= 550u; now_ms += 10u)
  {
    fake_bench_advance_encoder(now_ms, 0, 0);
    sc_bt_bench_service(now_ms);
    if ((fake_bench_tx_count() != 0u) &&
        (strstr(fake_bench_tx_at(0u), "ERR STALL") != NULL))
    {
      saw_stall = 1;
      break;
    }
    fake_bench_clear_tx();
  }
  CHECK(saw_stall == 1);
  CHECK(fake_bench_last_standby_enable() == 0u);
  CHECK(fake_bench_tb_state()->mode == SC_TB6612_MODE_STANDBY);
  CHECK(export_capture_fault_mask(now_ms + 10u, &fault_mask) == 1);
  CHECK(fault_mask != 0u);
  return 0;
}

static int test_pid_ram_readback_and_state_gate(void)
{
  const char *response;

  CHECK(start_bench(0u) == 0);
  inject_text("PID B 100 20 3\r\n");
  sc_bt_bench_service(0u);
  response = fake_bench_tx_at(0u);
  CHECK(strstr(response, "OK PID B") != NULL);
  CHECK(strstr(response, "Lkp=100") != NULL);
  CHECK(strstr(response, "Lki=20") != NULL);
  CHECK(strstr(response, "Lkd=3") != NULL);
  CHECK(strstr(response, "Lff=803") != NULL);
  CHECK(strstr(response, "Rkp=100") != NULL);
  CHECK(strstr(response, "PARAM=RAM_ONLY") != NULL);

  fake_bench_clear_tx();
  inject_text("PID L 10001 0 0\r\n");
  sc_bt_bench_service(10u);
  CHECK(tx_equals(0u, "ERR ARG\r\n"));

  fake_bench_clear_tx();
  fake_bench_clear_calls();
  inject_text("ARM\r\nMOTOR L 100 500\r\n");
  sc_bt_bench_service(20u);
  fake_bench_clear_tx();
  inject_text("PID B 500 50 5\r\n");
  sc_bt_bench_service(30u);
  CHECK(tx_equals(0u, "ERR ACTIVE\r\n"));
  CHECK(fake_bench_last_standby_enable() == 0u);

  fake_bench_clear_tx();
  inject_text("STATUS\r\n");
  sc_bt_bench_service(40u);
  response = fake_bench_tx_at(0u);
  CHECK(strstr(response, "Lkp=100") != NULL);
  CHECK(strstr(response, "Rkp=100") != NULL);
  CHECK(strstr(response, "PARAM=UNVERIFIED") != NULL);
  return 0;
}

static int test_capture_100hz_two_seconds(void)
{
  uint32_t now_ms;
  uint32_t export_ms = 2020u;
  size_t capture_lines = 0u;
  size_t loops = 0u;
  int ended = 0;
  int saw_begin = 0;
  char last_capture[FAKE_BENCH_TX_LINE_SIZE] = {0};

  CHECK(start_bench(0u) == 0);
  inject_text("ARM\r\nMOTOR L 100 2000\r\n");
  sc_bt_bench_service(0u);
  for (now_ms = 10u; now_ms <= 2000u; now_ms += 10u)
  {
    fake_bench_advance_encoder(now_ms, -20, 20);
    sc_bt_bench_service(now_ms);
    fake_bench_clear_tx();
  }
  CHECK(fake_bench_tb_state()->mode == SC_TB6612_MODE_STANDBY);

  fake_bench_clear_tx();
  inject_text("STATUS\r\n");
  sc_bt_bench_service(2010u);
  CHECK(strstr(fake_bench_tx_at(0u), "CAP=201") != NULL);
  CHECK(strstr(fake_bench_tx_at(0u), "Lspeed=0 ") == NULL);
  CHECK(strstr(fake_bench_tx_at(0u), "Lwheelvalid=1") != NULL);

  fake_bench_clear_tx();
  inject_text("CAPTURE\r\n");
  while ((loops < 100u) && (ended == 0))
  {
    size_t index;

    sc_bt_bench_service(export_ms++);
    for (index = 0u; index < fake_bench_tx_count(); index++)
    {
      const char *line = fake_bench_tx_at(index);

      if (strcmp(line, "OK CAPTURE BEGIN N=201\r\n") == 0)
      {
        saw_begin = 1;
      }
      else if (strncmp(line, "CAP,", 4u) == 0)
      {
        capture_lines++;
        strncpy(last_capture, line, sizeof(last_capture) - 1u);
        last_capture[sizeof(last_capture) - 1u] = '\0';
      }
      else if (strcmp(line, "OK CAPTURE END\r\n") == 0)
      {
        ended = 1;
      }
    }
    fake_bench_clear_tx();
    loops++;
  }
  CHECK(ended == 1);
  CHECK(saw_begin == 1);
  CHECK(capture_lines == 201u);
  CHECK(loops <= 68u);
  CHECK(strstr(last_capture, ",MOTOR,0,0,") != NULL);
  CHECK(strncmp(last_capture, "CAP,2000,", 9u) == 0);
  return 0;
}

static int test_capture_tx_full_retries(void)
{
  uint32_t now_ms;
  size_t capture_lines = 0u;
  int ended = 0;
  int saw_begin = 0;
  size_t loops = 0u;

  CHECK(start_bench(0u) == 0);
  inject_text("ARM\r\nMOTOR L 100 100\r\n");
  sc_bt_bench_service(0u);
  for (now_ms = 10u; now_ms <= 100u; now_ms += 10u)
  {
    sc_bt_bench_service(now_ms);
    fake_bench_clear_tx();
  }
  CHECK(fake_bench_tb_state()->mode == SC_TB6612_MODE_STANDBY);

  inject_text("CAPTURE\r\n");
  fake_bench_full_next_write();
  sc_bt_bench_service(110u);
  CHECK(fake_bench_tx_count() == 0u);

  while ((loops < 10u) && (ended == 0))
  {
    size_t index;

    sc_bt_bench_service(111u + (uint32_t)loops);
    for (index = 0u; index < fake_bench_tx_count(); index++)
    {
      const char *line = fake_bench_tx_at(index);

      if (strcmp(line, "OK CAPTURE BEGIN N=11\r\n") == 0)
      {
        saw_begin = 1;
      }
      else if (strncmp(line, "CAP,", 4u) == 0)
      {
        capture_lines++;
      }
      else if (strcmp(line, "OK CAPTURE END\r\n") == 0)
      {
        ended = 1;
      }
    }
    fake_bench_clear_tx();
    loops++;
  }
  CHECK(ended == 1);
  CHECK(saw_begin == 1);
  CHECK(capture_lines == 11u);
  return 0;
}

static int test_speed_feedback_timeout_stops(void)
{
  CHECK(start_bench(0u) == 0);
  CHECK(prime_wheel_feedback() == 0);
  fake_bench_advance_encoder(60u, -20, 20);
  sc_bt_bench_service(60u);
  fake_bench_clear_tx();
  inject_text("ARM\r\nSPEED B 200 500\r\n");
  sc_bt_bench_service(60u);
  fake_bench_clear_tx();
  fake_bench_advance_encoder(70u, -20, 20);
  sc_bt_bench_service(70u);
  fake_bench_clear_tx();
  sc_bt_bench_service(101u);
  CHECK(tx_equals(0u, "ERR FEEDBACK\r\n"));
  CHECK(fake_bench_last_standby_enable() == 0u);
  CHECK(fake_bench_tb_state()->mode == SC_TB6612_MODE_STANDBY);
  return 0;
}

static int test_capture_requires_stopped_and_uart_error_safe(void)
{
  uint32_t now_ms;

  CHECK(start_bench(0u) == 0);
  inject_text("ARM\r\nMOTOR L 100 100\r\n");
  sc_bt_bench_service(0u);
  fake_bench_clear_tx();
  inject_text("CAPTURE\r\n");
  sc_bt_bench_service(10u);
  CHECK(tx_equals(0u, "ERR ACTIVE\r\n"));
  CHECK(fake_bench_last_standby_enable() == 0u);

  CHECK(start_bench(0u) == 0);
  inject_text("ARM\r\nMOTOR L 100 100\r\n");
  sc_bt_bench_service(0u);
  for (now_ms = 10u; now_ms <= 100u; now_ms += 10u)
  {
    sc_bt_bench_service(now_ms);
  }
  fake_bench_clear_tx();
  fake_bench_fail_next_write();
  inject_text("CAPTURE\r\n");
  sc_bt_bench_service(110u);
  CHECK(tx_equals(0u, "ERR UART\r\n"));
  CHECK(fake_bench_last_standby_enable() == 0u);
  fake_bench_clear_tx();
  sc_bt_bench_service(111u);
  CHECK(fake_bench_tx_count() == 0u);
  return 0;
}

typedef struct {
  const char *name;
  int (*run)(void);
} test_case_t;

static const test_case_t s_tests[] = {
  {"init_safe_standby", test_init_safe_standby},
  {"ping_exact", test_ping_exact},
  {"status_fields", test_status_fields},
  {"line_exact", test_line_exact},
  {"line_alias_fragmented", test_line_alias_fragmented},
  {"line_candidate_states_valid", test_line_candidate_states_valid},
  {"line_does_not_extend_motion", test_line_does_not_extend_motion},
  {"line_invalid_snapshot_safe_stop", test_line_invalid_snapshot_safe_stop},
  {"line_unavailable_and_bad_argument",
   test_line_unavailable_and_bad_argument},
  {"line_help_and_tx_full", test_line_help_and_tx_full},
  {"fragmented_crlf", test_fragmented_crlf},
  {"max_line_exact", test_max_line_exact},
  {"overlong_safe_stop", test_overlong_safe_stop},
  {"overlong_resync", test_overlong_resync},
  {"invalid_byte_safe_stop", test_invalid_byte_safe_stop},
  {"arm_expiry_boundary", test_arm_expiry_boundary},
  {"arm_tick_wrap", test_arm_tick_wrap},
  {"motor_bounds_single_wheel", test_motor_bounds_single_wheel},
  {"motor_bounds_rejected", test_motor_bounds_rejected},
  {"repeated_motion_safe_stop", test_repeated_motion_safe_stop},
  {"unarmed_motion_safe_stop", test_unarmed_motion_safe_stop},
  {"auto_stop_deadline", test_auto_stop_deadline},
  {"auto_stop_driver_failure", test_auto_stop_driver_failure},
  {"motor_tick_wrap", test_motor_tick_wrap},
  {"stop_command", test_stop_command},
  {"reset_only_stopped", test_reset_only_stopped},
  {"heartbeat_cadence", test_heartbeat_cadence},
  {"heartbeat_suppressed_during_motion",
   test_heartbeat_suppressed_during_motion},
  {"heartbeat_resumes_after_motion_stop",
   test_heartbeat_resumes_after_motion_stop},
  {"capture_export_heartbeat_suppressed",
   test_capture_export_heartbeat_suppressed},
  {"uart_error_resets_parser", test_uart_error_resets_parser},
  {"uart_io_failure_safe_stop", test_uart_io_failure_safe_stop},
  {"driver_failure_safe_stop", test_driver_failure_safe_stop},
  {"tx_full_stop_still_safe", test_tx_full_stop_still_safe},
  {"raw_requires_fresh_feedback", test_raw_requires_fresh_feedback},
  {"raw_requires_arm_and_limits", test_raw_requires_arm_and_limits},
  {"raw_stall_zero_progress", test_raw_stall_zero_progress},
  {"raw_stall_jitter_keeps_window", test_raw_stall_jitter_keeps_window},
  {"raw_stall_five_per_window_fails", test_raw_stall_five_per_window_fails},
  {"raw_stall_real_progress_passes", test_raw_stall_real_progress_passes},
  {"raw_feedback_lost_sync_stops", test_raw_feedback_lost_sync_stops},
  {"raw_feedback_sequence_freeze_stops",
   test_raw_feedback_sequence_freeze_stops},
  {"speed_requires_arm_and_feedback",
   test_speed_requires_arm_and_feedback},
  {"speed_closed_loop_auto_stop", test_speed_closed_loop_auto_stop},
  {"speed_stall_zero_progress", test_speed_stall_zero_progress},
  {"pid_ram_readback_and_state_gate",
   test_pid_ram_readback_and_state_gate},
  {"capture_100hz_two_seconds", test_capture_100hz_two_seconds},
  {"capture_tx_full_retries", test_capture_tx_full_retries},
  {"speed_feedback_timeout_stops", test_speed_feedback_timeout_stops},
  {"capture_requires_stopped_and_uart_error_safe",
   test_capture_requires_stopped_and_uart_error_safe}
};

int main(int argc, char **argv)
{
  size_t index;

  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <test-name>\n", argv[0]);
    return 2;
  }
  for (index = 0u; index < (sizeof(s_tests) / sizeof(s_tests[0])); index++)
  {
    if (strcmp(argv[1], s_tests[index].name) == 0)
    {
      return s_tests[index].run();
    }
  }
  fprintf(stderr, "unknown test: %s\n", argv[1]);
  return 2;
}
