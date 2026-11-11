#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fake_support.h"
#include "main.h"
#include "sc_bt_bench.h"
#include "sc_lf04.h"
#include "sc_line_service.h"

#define CHECK(condition) \
  do { \
    if (!(condition)) { \
      fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
              __FILE__, __LINE__, #condition); \
      return 1; \
    } \
  } while (0)

static int test_lf_init_config(void)
{
  fake_line_reset();
  fake_line_set_raw(0x0Au);
  CHECK(sc_lf04_init() == SC_LF04_OK);
  CHECK(sc_lf04_init() == SC_LF04_OK);
  CHECK(sc_lf04_get_latest(NULL) == SC_LF04_ERR_ARGUMENT);
  CHECK(sc_lf04_get_status(NULL) == SC_LF04_ERR_ARGUMENT);
  return 0;
}

static int test_lf_config_rejections(void)
{
  fake_line_reset();
  fake_line_set_clock(0u);
  CHECK(sc_lf04_init() == SC_LF04_ERR_CONFIG);

  fake_line_reset();
  fake_line_set_pin_config(GPIO_PIN_12, 0x0u);
  CHECK(sc_lf04_init() == SC_LF04_ERR_CONFIG);

  fake_line_reset();
  fake_line_set_pin_config(GPIO_PIN_13, 0x0u);
  CHECK(sc_lf04_init() == SC_LF04_ERR_CONFIG);

  fake_line_reset();
  fake_line_set_pin_config(GPIO_PIN_14, 0x0u);
  CHECK(sc_lf04_init() == SC_LF04_ERR_CONFIG);

  fake_line_reset();
  fake_line_set_pin_config(GPIO_PIN_15, 0x0u);
  CHECK(sc_lf04_init() == SC_LF04_ERR_CONFIG);

  fake_line_reset();
  fake_line_set_pull(GPIO_PIN_12, 0u);
  CHECK(sc_lf04_init() == SC_LF04_ERR_CONFIG);

  fake_line_reset();
  CHECK(sc_lf04_init() == SC_LF04_OK);
  return 0;
}

static int test_lf_init_idempotent(void)
{
  sc_line_sample_t sample;
  sc_lf04_state_t state;

  fake_line_reset();
  fake_line_set_raw(0x0Fu);
  fake_line_set_tick(1u);
  CHECK(sc_lf04_init() == SC_LF04_OK);
  CHECK(sc_lf04_get_latest(&sample) == SC_LF04_OK);
  CHECK(sample.raw_levels == 0x0Fu);
  CHECK(sample.sequence == 1u);
  fake_line_set_raw(0x08u);
  CHECK(sc_lf04_init() == SC_LF04_OK);
  CHECK(sc_lf04_get_latest(&sample) == SC_LF04_OK);
  CHECK(sample.raw_levels == 0x0Fu);
  CHECK(sample.sequence == 1u);
  CHECK(sc_lf04_get_status(&state) == SC_LF04_OK);
  CHECK(state.samples == 1u);
  return 0;
}

static int test_lf_sample_snapshot_sequence(void)
{
  sc_line_sample_t sample;
  sc_lf04_state_t state;

  fake_line_reset();
  fake_line_set_raw(0x0Fu);
  CHECK(sc_lf04_init() == SC_LF04_OK);
  fake_line_set_idr(0x000080F0u); /* raw=0001, unrelated low bits set */
  CHECK(sc_lf04_sample(40u, &sample) == SC_LF04_OK);
  CHECK(sample.raw_levels == 0x08u);
  CHECK(sample.sample_time_ms == 40u);
  CHECK(sample.sequence == 2u);
  CHECK(sample.valid == 1u);
  CHECK(sc_lf04_get_latest(&sample) == SC_LF04_OK);
  CHECK(sample.raw_levels == 0x08u);
  CHECK(sample.sequence == 2u);
  CHECK(sc_lf04_get_status(&state) == SC_LF04_OK);
  CHECK(state.samples == 2u);
  return 0;
}

static int test_lf_sample_all_pins(void)
{
  sc_line_sample_t sample;
  uint8_t raw;

  fake_line_reset();
  CHECK(sc_lf04_init() == SC_LF04_OK);
  for (raw = 0u; raw < 16u; raw++)
  {
    fake_line_set_raw(raw);
    CHECK(sc_lf04_sample((uint32_t)raw, &sample) == SC_LF04_OK);
    CHECK(sample.raw_levels == raw);
  }
  return 0;
}

static int test_lf_sample_not_ready(void)
{
  sc_line_sample_t sample = {0};

  fake_line_reset();
  CHECK(sc_lf04_sample(10u, &sample) == SC_LF04_ERR_NOT_READY);
  CHECK(sample.valid == 0u);
  CHECK(sample.sample_time_ms == 10u);
  CHECK(sc_lf04_get_latest(&sample) == SC_LF04_ERR_NOT_READY);
  CHECK(sc_lf04_sample(11u, NULL) == SC_LF04_ERR_ARGUMENT);
  return 0;
}

static int test_lf_config_failure_recovery(void)
{
  sc_line_sample_t sample;
  sc_lf04_state_t state;

  fake_line_reset();
  fake_line_set_tick(5u);
  CHECK(sc_lf04_init() == SC_LF04_OK);
  fake_line_set_pin_config(GPIO_PIN_14, 0x0u);
  CHECK(sc_lf04_sample(20u, &sample) == SC_LF04_ERR_CONFIG);
  CHECK(sample.valid == 0u);
  CHECK(sample.sequence == 1u);
  CHECK(sc_lf04_get_latest(&sample) == SC_LF04_OK);
  CHECK(sample.valid == 0u);
  CHECK(sc_lf04_get_status(&state) == SC_LF04_OK);
  CHECK(state.config_failures == 1u);
  CHECK(state.sample_failures == 1u);

  fake_line_set_pin_config(GPIO_PIN_14, 0x8u);
  fake_line_set_raw(0x07u);
  CHECK(sc_lf04_sample(30u, &sample) == SC_LF04_OK);
  CHECK(sample.valid == 1u);
  CHECK(sample.raw_levels == 0x07u);
  CHECK(sample.sequence == 2u);
  return 0;
}

static int test_line_first_get_not_ready(void)
{
  sc_line_state_t state;
  sc_line_sample_t sample = {0};

  CHECK(sc_line_service_get_latest(&state) == SC_LINE_ERR_NOT_READY);
  CHECK(sc_line_service_update(NULL) == SC_LINE_ERR_ARGUMENT);
  CHECK(sc_line_service_update(&sample) == SC_LINE_ERR_INVALID_DATA);
  CHECK(sc_line_service_get_latest(&state) == SC_LINE_OK);
  CHECK(state.sample.valid == 0u);
  CHECK(state.semantic_state == SC_LINE_INVALID);
  CHECK(state.black_mask == 0u);
  return 0;
}

static int test_line_all_sixteen_patterns(void)
{
  static const struct {
    uint8_t raw;
    uint8_t mask;
    sc_line_class_t state;
  } cases[] = {
    {0x00u, 0x0Fu, SC_LINE_JUNCTION_CANDIDATE},
    {0x01u, 0x0Eu, SC_LINE_RIGHT_RIGHT_ANGLE_CANDIDATE},
    {0x02u, 0x0Du, SC_LINE_UNKNOWN_PATTERN},
    {0x03u, 0x0Cu, SC_LINE_UNKNOWN_PATTERN},
    {0x04u, 0x0Bu, SC_LINE_UNKNOWN_PATTERN},
    {0x05u, 0x0Au, SC_LINE_UNKNOWN_PATTERN},
    {0x06u, 0x09u, SC_LINE_UNKNOWN_PATTERN},
    {0x07u, 0x08u, SC_LINE_RIGHT_LARGE_BEND_CANDIDATE},
    {0x08u, 0x07u, SC_LINE_LEFT_RIGHT_ANGLE_CANDIDATE},
    {0x09u, 0x06u, SC_LINE_STRAIGHT},
    {0x0Au, 0x05u, SC_LINE_UNKNOWN_PATTERN},
    {0x0Bu, 0x04u, SC_LINE_RIGHT_FINE_ADJUST_CANDIDATE},
    {0x0Cu, 0x03u, SC_LINE_UNKNOWN_PATTERN},
    {0x0Du, 0x02u, SC_LINE_LEFT_FINE_ADJUST_CANDIDATE},
    {0x0Eu, 0x01u, SC_LINE_LEFT_LARGE_BEND_CANDIDATE},
    {0x0Fu, 0x00u, SC_LINE_LOST_CANDIDATE}
  };
  sc_line_state_t state;
  sc_line_sample_t sample = {0};
  size_t index;

  for (index = 0u; index < (sizeof(cases) / sizeof(cases[0])); index++)
  {
    sample.raw_levels = cases[index].raw;
    sample.sample_time_ms = (uint32_t)index;
    sample.sequence = (uint32_t)index + 1u;
    sample.valid = 1u;
    CHECK(sc_line_service_update(&sample) == SC_LINE_OK);
    CHECK(sc_line_service_get_latest(&state) == SC_LINE_OK);
    CHECK(state.black_mask == cases[index].mask);
    CHECK(state.semantic_state == cases[index].state);
    CHECK(state.sample.valid == 1u);
    CHECK(state.sample.raw_levels == cases[index].raw);
  }
  return 0;
}

static int test_line_invalid_and_argument(void)
{
  sc_line_state_t state;
  sc_line_sample_t sample = {0};
  sc_line_sample_t previous;

  sample.raw_levels = 0x09u;
  sample.sample_time_ms = 1u;
  sample.sequence = 7u;
  sample.valid = 1u;
  CHECK(sc_line_service_update(&sample) == SC_LINE_OK);
  CHECK(sc_line_service_get_latest(&state) == SC_LINE_OK);
  previous = state.sample;
  CHECK(sc_line_service_update(NULL) == SC_LINE_ERR_ARGUMENT);
  CHECK(sc_line_service_get_latest(&state) == SC_LINE_OK);
  CHECK(state.sample.raw_levels == previous.raw_levels);
  CHECK(state.sample.sequence == previous.sequence);

  sample.valid = 0u;
  sample.raw_levels = 0x08u;
  sample.sample_time_ms = 2u;
  sample.sequence = 8u;
  CHECK(sc_line_service_update(&sample) == SC_LINE_ERR_INVALID_DATA);
  CHECK(sc_line_service_get_latest(&state) == SC_LINE_OK);
  CHECK(state.sample.valid == 0u);
  CHECK(state.sample.raw_levels == 0x08u);
  CHECK(state.black_mask == 0u);
  CHECK(state.semantic_state == SC_LINE_INVALID);

  sample.valid = 1u;
  sample.raw_levels = 0x18u;
  sample.sample_time_ms = 3u;
  CHECK(sc_line_service_update(&sample) == SC_LINE_ERR_INVALID_DATA);
  CHECK(sc_line_service_get_latest(&state) == SC_LINE_OK);
  CHECK(state.sample.valid == 0u);
  CHECK(state.sample.raw_levels == 0x18u);
  CHECK(state.black_mask == 0u);
  CHECK(state.semantic_state == SC_LINE_INVALID);
  return 0;
}

static int test_line_unknown_valid(void)
{
  sc_line_state_t state;
  sc_line_sample_t sample = {
    .raw_levels = 0x06u,
    .sample_time_ms = 12u,
    .sequence = 4u,
    .valid = 1u
  };

  CHECK(sc_line_service_update(&sample) == SC_LINE_OK);
  CHECK(sc_line_service_get_latest(&state) == SC_LINE_OK);
  CHECK(state.sample.valid == 1u);
  CHECK(state.black_mask == 0x09u);
  CHECK(state.semantic_state == SC_LINE_UNKNOWN_PATTERN);
  return 0;
}

static int test_integration_driver_service_bench(void)
{
  sc_line_sample_t sample;
  sc_line_state_t line;
  const char *response;

  fake_line_reset();
  fake_line_set_tick(100u);
  fake_line_set_raw(0x08u);
  CHECK(sc_lf04_init() == SC_LF04_OK);
  CHECK(sc_lf04_sample(120u, &sample) == SC_LF04_OK);
  CHECK(sc_line_service_update(&sample) == SC_LINE_OK);
  CHECK(sc_line_service_get_latest(&line) == SC_LINE_OK);
  CHECK(line.black_mask == 0x07u);
  sc_bt_bench_init(120u);
  fake_bench_clear_tx();
  fake_bench_inject_rx("LINE\r\n", 6u);
  sc_bt_bench_service(123u);
  CHECK(fake_bench_tx_count() == 1u);
  response = fake_bench_tx_at(0u);
  CHECK(strcmp(
    response,
    "LINE raw=0001 raw_hex=0x08 black_mask=0x07 "
    "state=SC_LINE_LEFT_RIGHT_ANGLE_CANDIDATE valid=1 "
    "t=120 seq=2 age=3\r\n") == 0);
  return 0;
}

static int test_bench_no_line_snapshot_safe_stop(void)
{
  fake_line_reset();
  fake_line_set_raw(0x09u);
  sc_bt_bench_init(0u);
  fake_bench_clear_tx();
  fake_bench_inject_rx("LF\r\n", 4u);
  sc_bt_bench_service(10u);
  CHECK(fake_bench_tx_count() == 1u);
  CHECK(strcmp(fake_bench_tx_at(0u), "ERR IO\r\n") == 0);
  return 0;
}

typedef struct {
  const char *name;
  int (*run)(void);
} test_case_t;

static const test_case_t s_tests[] = {
  {"lf_init_config", test_lf_init_config},
  {"lf_config_rejections", test_lf_config_rejections},
  {"lf_init_idempotent", test_lf_init_idempotent},
  {"lf_sample_snapshot_sequence", test_lf_sample_snapshot_sequence},
  {"lf_sample_all_pins", test_lf_sample_all_pins},
  {"lf_sample_not_ready", test_lf_sample_not_ready},
  {"lf_config_failure_recovery", test_lf_config_failure_recovery},
  {"line_first_get_not_ready", test_line_first_get_not_ready},
  {"line_all_sixteen_patterns", test_line_all_sixteen_patterns},
  {"line_invalid_and_argument", test_line_invalid_and_argument},
  {"line_unknown_valid", test_line_unknown_valid},
  {"integration_driver_service_bench", test_integration_driver_service_bench},
  {"bench_no_line_snapshot_safe_stop", test_bench_no_line_snapshot_safe_stop}
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
