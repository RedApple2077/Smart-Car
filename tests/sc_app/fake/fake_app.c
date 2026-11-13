#include "fake_app.h"

#include <string.h>

#include "sc_led.h"
#include "sc_lf04.h"
#include "sc_safety.h"

static sc_line_sample_t s_line_sample;
static sc_line_state_t s_line_state;
static uint8_t s_line_has_snapshot;
static uint8_t s_line_raw = 0x0Fu;
static uint8_t s_line_fail;

static sc_encoder_snapshot_t s_encoder;
static uint8_t s_encoder_initialized;
static uint8_t s_encoder_frozen;
static uint8_t s_encoder_fail;
static uint32_t s_encoder_sequence;
static uint32_t s_encoder_last_time;
static int64_t s_encoder_left_total;
static int64_t s_encoder_right_total;
static int32_t s_encoder_left_delta = -1;
static int32_t s_encoder_right_delta = 1;
static uint8_t s_encoder_left_valid = 1u;
static uint8_t s_encoder_right_valid = 1u;
static uint8_t s_encoder_left_lost;
static uint8_t s_encoder_right_lost;

static uint8_t s_button_events[FAKE_APP_EVENT_CAPACITY];
static size_t s_button_head;
static size_t s_button_tail;

static sc_tb6612_state_t s_tb;
static uint8_t s_tb_fail_init;
static uint8_t s_tb_fail_apply;
static uint8_t s_tb_fail_standby;
static uint8_t s_tb_fail_get;
static fake_tb_event_t s_tb_events[FAKE_APP_EVENT_CAPACITY];
static size_t s_tb_event_count;

static uint32_t s_emergency_count;
static uint32_t s_led_set_count;
static uint8_t s_led_last_value;

static void fake_tb_record(fake_tb_event_kind_t kind, int16_t left,
                           int16_t right, uint8_t value)
{
  if (s_tb_event_count < FAKE_APP_EVENT_CAPACITY)
  {
    s_tb_events[s_tb_event_count].kind = kind;
    s_tb_events[s_tb_event_count].left = left;
    s_tb_events[s_tb_event_count].right = right;
    s_tb_events[s_tb_event_count].value = value;
    s_tb_event_count++;
  }
}

void fake_app_reset(void)
{
  memset(&s_line_sample, 0, sizeof(s_line_sample));
  memset(&s_line_state, 0, sizeof(s_line_state));
  s_line_has_snapshot = 0u;
  s_line_raw = 0x0Fu;
  s_line_fail = 0u;

  memset(&s_encoder, 0, sizeof(s_encoder));
  s_encoder_initialized = 0u;
  s_encoder_frozen = 0u;
  s_encoder_fail = 0u;
  s_encoder_sequence = 0u;
  s_encoder_last_time = 0u;
  s_encoder_left_total = 0;
  s_encoder_right_total = 0;
  s_encoder_left_delta = -1;
  s_encoder_right_delta = 1;
  s_encoder_left_valid = 1u;
  s_encoder_right_valid = 1u;
  s_encoder_left_lost = 0u;
  s_encoder_right_lost = 0u;

  memset(s_button_events, 0, sizeof(s_button_events));
  s_button_head = 0u;
  s_button_tail = 0u;

  memset(&s_tb, 0, sizeof(s_tb));
  s_tb.mode = SC_TB6612_MODE_UNINITIALIZED;
  s_tb_fail_init = 0u;
  s_tb_fail_apply = 0u;
  s_tb_fail_standby = 0u;
  s_tb_fail_get = 0u;
  memset(s_tb_events, 0, sizeof(s_tb_events));
  s_tb_event_count = 0u;
  s_emergency_count = 0u;
  s_led_set_count = 0u;
  s_led_last_value = 0u;
}

void fake_line_set_mask(uint8_t black_mask)
{
  fake_line_set_raw((uint8_t)((~black_mask) & 0x0Fu));
}

void fake_line_set_raw(uint8_t raw_levels)
{
  s_line_raw = raw_levels & 0x0Fu;
}

void fake_line_fail_next(void)
{
  s_line_fail = 1u;
}

sc_lf04_status_t sc_lf04_init(void)
{
  return SC_LF04_OK;
}

sc_lf04_status_t sc_lf04_sample(uint32_t now_ms, sc_line_sample_t *sample)
{
  if (sample == NULL)
  {
    return SC_LF04_ERR_ARGUMENT;
  }
  s_line_sample.sample_time_ms = now_ms;
  s_line_sample.raw_levels = s_line_raw;
  s_line_sample.valid = 1u;
  if (s_line_fail != 0u)
  {
    s_line_fail = 0u;
    s_line_sample.valid = 0u;
    *sample = s_line_sample;
    return SC_LF04_ERR_NOT_READY;
  }
  s_line_sample.sequence++;
  *sample = s_line_sample;
  return SC_LF04_OK;
}

sc_line_status_t sc_line_service_update(const sc_line_sample_t *sample)
{
  if (sample == NULL)
  {
    return SC_LINE_ERR_ARGUMENT;
  }
  s_line_state.sample = *sample;
  s_line_has_snapshot = 1u;
  if (sample->valid == 0u)
  {
    s_line_state.black_mask = 0u;
    s_line_state.semantic_state = SC_LINE_INVALID;
    return SC_LINE_ERR_INVALID_DATA;
  }
  s_line_state.black_mask = (uint8_t)(~sample->raw_levels) & 0x0Fu;
  s_line_state.semantic_state = SC_LINE_UNKNOWN_PATTERN;
  return SC_LINE_OK;
}

sc_line_status_t sc_line_service_get_latest(sc_line_state_t *state)
{
  if (state == NULL)
  {
    return SC_LINE_ERR_ARGUMENT;
  }
  if (s_line_has_snapshot == 0u)
  {
    return SC_LINE_ERR_NOT_READY;
  }
  *state = s_line_state;
  return SC_LINE_OK;
}

void fake_encoder_set_deltas(int32_t left_delta, int32_t right_delta)
{
  s_encoder_left_delta = left_delta;
  s_encoder_right_delta = right_delta;
}

void fake_encoder_set_valid(uint8_t left_valid, uint8_t right_valid)
{
  s_encoder_left_valid = left_valid;
  s_encoder_right_valid = right_valid;
}

void fake_encoder_set_lost_sync(uint8_t left_lost, uint8_t right_lost)
{
  s_encoder_left_lost = left_lost;
  s_encoder_right_lost = right_lost;
}

void fake_encoder_freeze(uint8_t frozen)
{
  s_encoder_frozen = frozen;
}

void fake_encoder_fail_next(void)
{
  s_encoder_fail = 1u;
}

sc_encoder_status_t sc_encoder_init(void)
{
  s_encoder_initialized = 1u;
  return SC_ENCODER_OK;
}

sc_encoder_status_t sc_encoder_service(uint32_t now_ms)
{
  uint32_t interval_ms = 0u;

  if (s_encoder_initialized == 0u)
  {
    return SC_ENCODER_ERR_NOT_READY;
  }
  if (s_encoder_fail != 0u)
  {
    s_encoder_fail = 0u;
    return SC_ENCODER_ERR_HAL;
  }
  if (s_encoder_frozen != 0u)
  {
    return (s_encoder_left_lost != 0u) || (s_encoder_right_lost != 0u) ?
      SC_ENCODER_ERR_LOST_SYNC : SC_ENCODER_OK;
  }
  if (s_encoder_sequence != 0u)
  {
    interval_ms = (uint32_t)(now_ms - s_encoder_last_time);
    if (interval_ms < 1u)
    {
      return SC_ENCODER_OK;
    }
  }

  s_encoder_left_total += s_encoder_left_delta;
  s_encoder_right_total += s_encoder_right_delta;
  s_encoder_sequence++;
  s_encoder_last_time = now_ms;
  s_encoder.left.raw_count = (uint16_t)s_encoder_left_total;
  s_encoder.right.raw_count = (uint16_t)s_encoder_right_total;
  s_encoder.left.total_count = s_encoder_left_total;
  s_encoder.right.total_count = s_encoder_right_total;
  s_encoder.left.delta_count = s_encoder_left_delta;
  s_encoder.right.delta_count = s_encoder_right_delta;
  s_encoder.left.valid = s_encoder_left_valid;
  s_encoder.right.valid = s_encoder_right_valid;
  s_encoder.left.lost_sync = s_encoder_left_lost;
  s_encoder.right.lost_sync = s_encoder_right_lost;
  s_encoder.sample_time_ms = now_ms;
  s_encoder.interval_ms = interval_ms;
  s_encoder.sequence = s_encoder_sequence;
  s_encoder.valid =
    (uint8_t)((s_encoder_left_valid != 0u) &&
              (s_encoder_right_valid != 0u));
  return ((s_encoder_left_lost != 0u) || (s_encoder_right_lost != 0u)) ?
    SC_ENCODER_ERR_LOST_SYNC : SC_ENCODER_OK;
}

sc_encoder_status_t sc_encoder_get_latest(sc_encoder_snapshot_t *snapshot)
{
  if (snapshot == NULL)
  {
    return SC_ENCODER_ERR_ARGUMENT;
  }
  *snapshot = s_encoder;
  return s_encoder_initialized ? SC_ENCODER_OK :
    SC_ENCODER_ERR_NOT_READY;
}

sc_encoder_status_t sc_encoder_reset_counts(uint32_t now_ms)
{
  (void)now_ms;
  return SC_ENCODER_OK;
}

sc_button_status_t sc_button_init(void)
{
  return SC_BUTTON_OK;
}

void fake_button_queue(uint8_t events)
{
  size_t next = (s_button_head + 1u) % FAKE_APP_EVENT_CAPACITY;

  if (next != s_button_tail)
  {
    s_button_events[s_button_head] = events;
    s_button_head = next;
  }
}

uint8_t sc_button_poll(uint32_t now_ms)
{
  (void)now_ms;
  if (s_button_tail == s_button_head)
  {
    return SC_BUTTON_EVENT_NONE;
  }
  {
    uint8_t events = s_button_events[s_button_tail];
    s_button_tail = (s_button_tail + 1u) % FAKE_APP_EVENT_CAPACITY;
    return events;
  }
}

sc_led_status_t sc_led_init(void)
{
  return SC_LED_OK;
}

sc_led_status_t sc_led_set(uint8_t on)
{
  s_led_set_count++;
  s_led_last_value = on;
  return SC_LED_OK;
}

sc_tb6612_status_t sc_tb6612_init(void)
{
  if (s_tb_fail_init != 0u)
  {
    s_tb.mode = SC_TB6612_MODE_FAULT;
    return SC_TB6612_ERR_HAL;
  }
  s_tb.mode = SC_TB6612_MODE_STANDBY;
  s_tb.initialized = 1u;
  s_tb.enabled = 0u;
  s_tb.standby = 0u;
  return SC_TB6612_OK;
}

sc_tb6612_status_t sc_tb6612_set_standby(uint8_t enable)
{
  fake_tb_record(FAKE_TB_EVENT_SET_STANDBY, 0, 0, enable);
  if (s_tb_fail_standby != 0u)
  {
    s_tb_fail_standby = 0u;
    return SC_TB6612_ERR_HAL;
  }
  s_tb.enabled = (enable != 0u) ? 1u : 0u;
  s_tb.standby = (enable != 0u) ? 1u : 0u;
  s_tb.mode = (enable != 0u) ? SC_TB6612_MODE_ENABLED :
                               SC_TB6612_MODE_STANDBY;
  if (enable == 0u)
  {
    s_tb.left_command = 0;
    s_tb.right_command = 0;
  }
  return SC_TB6612_OK;
}

sc_tb6612_status_t sc_tb6612_apply(int16_t left, int16_t right)
{
  fake_tb_record(FAKE_TB_EVENT_APPLY, left, right, 0u);
  if (s_tb_fail_apply != 0u)
  {
    s_tb_fail_apply = 0u;
    return SC_TB6612_ERR_HAL;
  }
  if (s_tb.mode != SC_TB6612_MODE_ENABLED)
  {
    return SC_TB6612_ERR_NOT_READY;
  }
  s_tb.left_command = left;
  s_tb.right_command = right;
  return SC_TB6612_OK;
}

sc_tb6612_status_t sc_tb6612_get_state(sc_tb6612_state_t *state)
{
  if (state == NULL)
  {
    return SC_TB6612_ERR_ARGUMENT;
  }
  if (s_tb_fail_get != 0u)
  {
    s_tb_fail_get = 0u;
    return SC_TB6612_ERR_HAL;
  }
  *state = s_tb;
  return SC_TB6612_OK;
}

void fake_tb_fail_init(void)
{
  s_tb_fail_init = 1u;
}

void fake_tb_fail_apply(void)
{
  s_tb_fail_apply = 1u;
}

void fake_tb_fail_standby(void)
{
  s_tb_fail_standby = 1u;
}

void fake_tb_fail_get(void)
{
  s_tb_fail_get = 1u;
}

size_t fake_tb_event_count(void)
{
  return s_tb_event_count;
}

const fake_tb_event_t *fake_tb_event_at(size_t index)
{
  return (index < s_tb_event_count) ? &s_tb_events[index] : NULL;
}

void fake_tb_clear_events(void)
{
  s_tb_event_count = 0u;
}

const sc_tb6612_state_t *fake_tb_state(void)
{
  return &s_tb;
}

void sc_safety_emergency_stop(void)
{
  s_emergency_count++;
  fake_tb_record(FAKE_TB_EVENT_EMERGENCY, 0, 0, 0u);
  s_tb.enabled = 0u;
  s_tb.standby = 0u;
  s_tb.mode = SC_TB6612_MODE_STANDBY;
  s_tb.left_command = 0;
  s_tb.right_command = 0;
}

uint32_t fake_emergency_count(void)
{
  return s_emergency_count;
}

uint32_t fake_led_set_count(void)
{
  return s_led_set_count;
}

uint8_t fake_led_last_value(void)
{
  return s_led_last_value;
}
