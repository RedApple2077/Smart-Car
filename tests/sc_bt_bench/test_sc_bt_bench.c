#include <stdint.h>
#include <stdio.h>
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
  CHECK(strstr(response, "Rvalid=1\r\n") != NULL);
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
    0u, "OK HELP PING STATUS LINE LF ARM MOTOR STOP RESET\r\n"));

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
  CHECK(fake_bench_standby_calls() == 0u);
  sc_bt_bench_service(110u);
  CHECK(fake_bench_standby_calls() == 1u);
  CHECK(fake_bench_last_standby_enable() == 0u);
  CHECK(tx_equals(0u, "OK STOP AUTO\r\n"));
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
  char stream[49u + 12u];
  size_t index;

  CHECK(start_bench(0u) == 0);
  for (index = 0u; index < 49u; index++)
  {
    stream[index] = 'A';
  }
  memcpy(&stream[49], "PING\r\nPING\r\n", 12u);
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
  CHECK(fake_bench_standby_calls() == 1u);
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
  {"uart_error_resets_parser", test_uart_error_resets_parser},
  {"uart_io_failure_safe_stop", test_uart_io_failure_safe_stop},
  {"driver_failure_safe_stop", test_driver_failure_safe_stop},
  {"tx_full_stop_still_safe", test_tx_full_stop_still_safe}
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
