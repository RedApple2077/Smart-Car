#include "fake_bench.h"

#include <string.h>

#define FAKE_RX_CAPACITY 256u

static char s_tx[FAKE_BENCH_TX_CAPACITY][FAKE_BENCH_TX_LINE_SIZE];
static size_t s_tx_count;
static uint8_t s_rx[FAKE_RX_CAPACITY];
static size_t s_rx_head;
static size_t s_rx_tail;
static sc_uart_stats_t s_uart_stats;
static sc_tb6612_state_t s_tb_state;
static sc_encoder_snapshot_t s_encoder_snapshot;
static sc_line_state_t s_line_state;
static uint8_t s_fail_read;
static uint8_t s_fail_stats;
static uint8_t s_fail_write;
static uint8_t s_fail_standby_enable;
static uint8_t s_fail_standby_disable;
static uint8_t s_fail_apply;
static uint8_t s_fail_encoder_reset;
static uint8_t s_fail_encoder_snapshot;
static uint8_t s_fail_tb_state;
static uint8_t s_fail_line_snapshot;
static uint32_t s_standby_calls;
static uint8_t s_last_standby_enable;
static uint32_t s_apply_calls;
static int16_t s_last_left;
static int16_t s_last_right;
static uint32_t s_encoder_reset_calls;

void fake_bench_reset(void)
{
  memset(s_tx, 0, sizeof(s_tx));
  s_tx_count = 0u;
  memset(s_rx, 0, sizeof(s_rx));
  s_rx_head = 0u;
  s_rx_tail = 0u;
  memset(&s_uart_stats, 0, sizeof(s_uart_stats));
  memset(&s_tb_state, 0, sizeof(s_tb_state));
  s_tb_state.initialized = 1u;
  s_tb_state.mode = SC_TB6612_MODE_STANDBY;
  memset(&s_encoder_snapshot, 0, sizeof(s_encoder_snapshot));
  s_encoder_snapshot.valid = 1u;
  s_encoder_snapshot.left.valid = 1u;
  s_encoder_snapshot.right.valid = 1u;
  memset(&s_line_state, 0, sizeof(s_line_state));
  s_line_state.sample.raw_levels = 0x09u;
  s_line_state.sample.sample_time_ms = 0u;
  s_line_state.sample.sequence = 1u;
  s_line_state.sample.valid = 1u;
  s_line_state.black_mask = 0x06u;
  s_line_state.semantic_state = SC_LINE_STRAIGHT;
  s_fail_read = 0u;
  s_fail_stats = 0u;
  s_fail_write = 0u;
  s_fail_standby_enable = 0u;
  s_fail_standby_disable = 0u;
  s_fail_apply = 0u;
  s_fail_encoder_reset = 0u;
  s_fail_encoder_snapshot = 0u;
  s_fail_tb_state = 0u;
  s_fail_line_snapshot = 0u;
  s_standby_calls = 0u;
  s_last_standby_enable = 0u;
  s_apply_calls = 0u;
  s_last_left = 0;
  s_last_right = 0;
  s_encoder_reset_calls = 0u;
}

void fake_bench_inject_rx(const char *data, size_t length)
{
  size_t index;

  for (index = 0u; index < length; index++)
  {
    size_t next = (s_rx_head + 1u) % FAKE_RX_CAPACITY;
    if (next == s_rx_tail)
    {
      return;
    }
    s_rx[s_rx_head] = (uint8_t)data[index];
    s_rx_head = next;
  }
}

void fake_bench_clear_tx(void)
{
  memset(s_tx, 0, sizeof(s_tx));
  s_tx_count = 0u;
}

size_t fake_bench_tx_count(void)
{
  return s_tx_count;
}

const char *fake_bench_tx_at(size_t index)
{
  if (index >= s_tx_count)
  {
    return "";
  }
  return s_tx[index];
}

void fake_bench_fail_next_read(void)
{
  s_fail_read = 1u;
}

void fake_bench_fail_next_stats(void)
{
  s_fail_stats = 1u;
}

void fake_bench_fail_next_write(void)
{
  s_fail_write = 1u;
}

void fake_bench_fail_next_standby_enable(void)
{
  s_fail_standby_enable = 1u;
}

void fake_bench_fail_next_standby_disable(void)
{
  s_fail_standby_disable = 1u;
}

void fake_bench_fail_next_apply(void)
{
  s_fail_apply = 1u;
}

void fake_bench_fail_next_encoder_reset(void)
{
  s_fail_encoder_reset = 1u;
}

void fake_bench_fail_encoder_snapshot(void)
{
  s_fail_encoder_snapshot = 1u;
}

void fake_bench_fail_tb_state(void)
{
  s_fail_tb_state = 1u;
}

void fake_bench_fail_line_snapshot(void)
{
  s_fail_line_snapshot = 1u;
}

sc_uart_stats_t *fake_bench_uart_stats(void)
{
  return &s_uart_stats;
}

sc_tb6612_state_t *fake_bench_tb_state(void)
{
  return &s_tb_state;
}

sc_encoder_snapshot_t *fake_bench_encoder_snapshot(void)
{
  return &s_encoder_snapshot;
}

sc_line_state_t *fake_bench_line_state(void)
{
  return &s_line_state;
}

uint32_t fake_bench_standby_calls(void)
{
  return s_standby_calls;
}

uint8_t fake_bench_last_standby_enable(void)
{
  return s_last_standby_enable;
}

uint32_t fake_bench_apply_calls(void)
{
  return s_apply_calls;
}

int16_t fake_bench_last_left(void)
{
  return s_last_left;
}

int16_t fake_bench_last_right(void)
{
  return s_last_right;
}

uint32_t fake_bench_encoder_reset_calls(void)
{
  return s_encoder_reset_calls;
}

void fake_bench_clear_calls(void)
{
  s_standby_calls = 0u;
  s_last_standby_enable = 0u;
  s_apply_calls = 0u;
  s_last_left = 0;
  s_last_right = 0;
  s_encoder_reset_calls = 0u;
}

sc_uart_status_t sc_uart_init(uint32_t baud, uint8_t rx_enable)
{
  (void)baud;
  (void)rx_enable;
  return SC_UART_OK;
}

sc_uart_status_t sc_uart_write(const char *text)
{
  size_t length;

  if (text == NULL)
  {
    return SC_UART_ERR_ARGUMENT;
  }
  if (s_fail_write != 0u)
  {
    s_fail_write = 0u;
    return SC_UART_ERR_HAL;
  }
  length = strlen(text);
  if ((s_tx_count >= FAKE_BENCH_TX_CAPACITY) ||
      (length >= FAKE_BENCH_TX_LINE_SIZE))
  {
    return SC_UART_ERR_BUFFER_FULL;
  }
  memcpy(s_tx[s_tx_count], text, length + 1u);
  s_tx_count++;
  return SC_UART_OK;
}

sc_uart_status_t sc_uart_write_bytes(const uint8_t *data, size_t len)
{
  char text[FAKE_BENCH_TX_LINE_SIZE];

  if ((data == NULL) || (len >= sizeof(text)))
  {
    return SC_UART_ERR_ARGUMENT;
  }
  memcpy(text, data, len);
  text[len] = '\0';
  return sc_uart_write(text);
}

sc_uart_status_t sc_uart_read(uint8_t *dst, size_t capacity,
                              size_t *received)
{
  size_t count = 0u;

  if ((dst == NULL) || (received == NULL) || (s_fail_read != 0u))
  {
    s_fail_read = 0u;
    return SC_UART_ERR_HAL;
  }
  while ((count < capacity) && (s_rx_tail != s_rx_head))
  {
    dst[count++] = s_rx[s_rx_tail];
    s_rx_tail = (s_rx_tail + 1u) % FAKE_RX_CAPACITY;
  }
  *received = count;
  return SC_UART_OK;
}

sc_uart_status_t sc_uart_get_stats(sc_uart_stats_t *stats)
{
  if ((stats == NULL) || (s_fail_stats != 0u))
  {
    s_fail_stats = 0u;
    return SC_UART_ERR_HAL;
  }
  *stats = s_uart_stats;
  return SC_UART_OK;
}

void sc_uart_service(void)
{
}

sc_tb6612_status_t sc_tb6612_init(void)
{
  return SC_TB6612_OK;
}

sc_tb6612_status_t sc_tb6612_set_standby(uint8_t enable)
{
  s_standby_calls++;
  s_last_standby_enable = enable;
  if ((enable == 1u) && (s_fail_standby_enable != 0u))
  {
    s_fail_standby_enable = 0u;
    return SC_TB6612_ERR_HAL;
  }
  if ((enable == 0u) && (s_fail_standby_disable != 0u))
  {
    s_fail_standby_disable = 0u;
    return SC_TB6612_ERR_HAL;
  }
  s_tb_state.standby = (enable != 0u) ? 1u : 0u;
  s_tb_state.enabled = (enable != 0u) ? 1u : 0u;
  s_tb_state.mode = (enable != 0u) ? SC_TB6612_MODE_ENABLED :
    SC_TB6612_MODE_STANDBY;
  if (enable == 0u)
  {
    s_tb_state.left_command = 0;
    s_tb_state.right_command = 0;
  }
  return SC_TB6612_OK;
}

sc_tb6612_status_t sc_tb6612_apply(int16_t left_permille,
                                   int16_t right_permille)
{
  s_apply_calls++;
  s_last_left = left_permille;
  s_last_right = right_permille;
  if (s_fail_apply != 0u)
  {
    s_fail_apply = 0u;
    return SC_TB6612_ERR_HAL;
  }
  s_tb_state.left_command = left_permille;
  s_tb_state.right_command = right_permille;
  s_tb_state.enabled = 1u;
  s_tb_state.standby = 1u;
  s_tb_state.mode = SC_TB6612_MODE_ENABLED;
  return SC_TB6612_OK;
}

sc_tb6612_status_t sc_tb6612_get_state(sc_tb6612_state_t *state)
{
  if ((state == NULL) || (s_fail_tb_state != 0u))
  {
    s_fail_tb_state = 0u;
    return SC_TB6612_ERR_HAL;
  }
  *state = s_tb_state;
  return SC_TB6612_OK;
}

sc_encoder_status_t sc_encoder_init(void)
{
  return SC_ENCODER_OK;
}

sc_encoder_status_t sc_encoder_service(uint32_t now_ms)
{
  (void)now_ms;
  return SC_ENCODER_OK;
}

sc_encoder_status_t sc_encoder_get_latest(sc_encoder_snapshot_t *snapshot)
{
  if ((snapshot == NULL) || (s_fail_encoder_snapshot != 0u))
  {
    s_fail_encoder_snapshot = 0u;
    return SC_ENCODER_ERR_HAL;
  }
  *snapshot = s_encoder_snapshot;
  return SC_ENCODER_OK;
}

sc_encoder_status_t sc_encoder_get_status(sc_encoder_state_t *state)
{
  if (state == NULL)
  {
    return SC_ENCODER_ERR_ARGUMENT;
  }
  return SC_ENCODER_OK;
}

sc_encoder_status_t sc_encoder_reset_counts(uint32_t now_ms)
{
  (void)now_ms;
  s_encoder_reset_calls++;
  if (s_fail_encoder_reset != 0u)
  {
    s_fail_encoder_reset = 0u;
    return SC_ENCODER_ERR_HAL;
  }
  memset(&s_encoder_snapshot, 0, sizeof(s_encoder_snapshot));
  s_encoder_snapshot.valid = 1u;
  s_encoder_snapshot.left.valid = 1u;
  s_encoder_snapshot.right.valid = 1u;
  return SC_ENCODER_OK;
}

sc_line_status_t sc_line_service_get_latest(sc_line_state_t *state)
{
  if ((state == NULL) || (s_fail_line_snapshot != 0u))
  {
    s_fail_line_snapshot = 0u;
    return SC_LINE_ERR_NOT_READY;
  }
  *state = s_line_state;
  return SC_LINE_OK;
}
