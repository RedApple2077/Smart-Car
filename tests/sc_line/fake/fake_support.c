#include "fake_support.h"

#include <string.h>

#include "main.h"
#include "sc_encoder.h"
#include "sc_line_service.h"
#include "sc_tb6612.h"
#include "sc_uart.h"

#define FAKE_BENCH_RX_CAPACITY 256u
#define FAKE_BENCH_TX_CAPACITY 8u
#define FAKE_BENCH_TX_SIZE 256u

GPIO_TypeDef fake_gpio_b;
RCC_TypeDef fake_rcc;

static uint32_t s_tick;
static uint32_t s_rx_head;
static uint32_t s_rx_tail;
static uint8_t s_rx[FAKE_BENCH_RX_CAPACITY];
static char s_tx[FAKE_BENCH_TX_CAPACITY][FAKE_BENCH_TX_SIZE];
static size_t s_tx_count;
static sc_tb6612_state_t s_tb_state;
static sc_encoder_snapshot_t s_encoder_snapshot;

static uint32_t fake_pin_shift(uint16_t pin)
{
  switch (pin)
  {
    case GPIO_PIN_12:
      return 16u;
    case GPIO_PIN_13:
      return 20u;
    case GPIO_PIN_14:
      return 24u;
    case GPIO_PIN_15:
      return 28u;
    default:
      return 32u;
  }
}

void fake_line_reset(void)
{
  s_tick = 0u;
  s_rx_head = 0u;
  s_rx_tail = 0u;
  memset(s_rx, 0, sizeof(s_rx));
  memset(s_tx, 0, sizeof(s_tx));
  s_tx_count = 0u;
  fake_rcc.APB2ENR = RCC_APB2ENR_IOPBEN;
  fake_gpio_b.CRH = 0x88880000u;
  fake_gpio_b.ODR = 0x0000F000u;
  fake_gpio_b.IDR = 0x0000F000u;
  memset(&s_tb_state, 0, sizeof(s_tb_state));
  s_tb_state.initialized = 1u;
  s_tb_state.mode = SC_TB6612_MODE_STANDBY;
  memset(&s_encoder_snapshot, 0, sizeof(s_encoder_snapshot));
  s_encoder_snapshot.valid = 1u;
  s_encoder_snapshot.left.valid = 1u;
  s_encoder_snapshot.right.valid = 1u;
}

void fake_line_set_tick(uint32_t tick)
{
  s_tick = tick;
}

void fake_line_set_raw(uint8_t raw_levels)
{
  fake_line_set_idr((((uint32_t)raw_levels & 0x0Fu) << 12u) | 0x0FFFu);
}

void fake_line_set_idr(uint32_t idr)
{
  fake_gpio_b.IDR = idr;
}

void fake_line_set_clock(uint8_t enabled)
{
  if (enabled != 0u)
  {
    fake_rcc.APB2ENR |= RCC_APB2ENR_IOPBEN;
  }
  else
  {
    fake_rcc.APB2ENR &= ~RCC_APB2ENR_IOPBEN;
  }
}

void fake_line_set_pin_config(uint16_t pin, uint8_t nibble)
{
  uint32_t shift = fake_pin_shift(pin);
  uint32_t mask;

  if (shift >= 32u)
  {
    return;
  }
  mask = 0x0Fu << shift;
  fake_gpio_b.CRH = (fake_gpio_b.CRH & ~mask) |
                    (((uint32_t)nibble & 0x0Fu) << shift);
}

void fake_line_set_pull(uint16_t pin, uint8_t enabled)
{
  if (enabled != 0u)
  {
    fake_gpio_b.ODR |= pin;
  }
  else
  {
    fake_gpio_b.ODR &= (uint32_t)~pin;
  }
}

uint32_t HAL_GetTick(void)
{
  return s_tick;
}

void fake_bench_clear_tx(void)
{
  memset(s_tx, 0, sizeof(s_tx));
  s_tx_count = 0u;
}

void fake_bench_inject_rx(const char *data, size_t length)
{
  size_t index;

  for (index = 0u; index < length; index++)
  {
    uint32_t next = (s_rx_head + 1u) % FAKE_BENCH_RX_CAPACITY;
    if (next == s_rx_tail)
    {
      return;
    }
    s_rx[s_rx_head] = (uint8_t)data[index];
    s_rx_head = next;
  }
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

sc_uart_status_t sc_uart_init(sc_uart_port_t port, uint32_t baud)
{
  (void)port;
  (void)baud;
  return SC_UART_OK;
}

sc_uart_status_t sc_uart_write(sc_uart_port_t port, const char *text)
{
  size_t length;

  if ((port != SC_UART_USART2) || (text == NULL))
  {
    return SC_UART_ERR_ARGUMENT;
  }
  length = strlen(text);
  if ((s_tx_count >= FAKE_BENCH_TX_CAPACITY) ||
      (length >= FAKE_BENCH_TX_SIZE))
  {
    return SC_UART_ERR_BUFFER_FULL;
  }
  memcpy(s_tx[s_tx_count], text, length + 1u);
  s_tx_count++;
  return SC_UART_OK;
}

sc_uart_status_t sc_uart_read(sc_uart_port_t port, uint8_t *dst,
                              size_t capacity, size_t *received)
{
  size_t count = 0u;

  if ((port != SC_UART_USART2) || (dst == NULL) ||
      (received == NULL))
  {
    return SC_UART_ERR_ARGUMENT;
  }
  while ((count < capacity) && (s_rx_tail != s_rx_head))
  {
    dst[count++] = s_rx[s_rx_tail];
    s_rx_tail = (s_rx_tail + 1u) % FAKE_BENCH_RX_CAPACITY;
  }
  *received = count;
  return SC_UART_OK;
}

sc_uart_status_t sc_uart_get_stats(sc_uart_port_t port,
                                   sc_uart_stats_t *stats)
{
  if ((port != SC_UART_USART2) || (stats == NULL))
  {
    return SC_UART_ERR_ARGUMENT;
  }
  memset(stats, 0, sizeof(*stats));
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
  s_tb_state.left_command = left_permille;
  s_tb_state.right_command = right_permille;
  s_tb_state.enabled = 1u;
  s_tb_state.standby = 1u;
  s_tb_state.mode = SC_TB6612_MODE_ENABLED;
  return SC_TB6612_OK;
}

sc_tb6612_status_t sc_tb6612_get_state(sc_tb6612_state_t *state)
{
  if (state == NULL)
  {
    return SC_TB6612_ERR_ARGUMENT;
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
  if (snapshot == NULL)
  {
    return SC_ENCODER_ERR_ARGUMENT;
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
  memset(state, 0, sizeof(*state));
  return SC_ENCODER_OK;
}

sc_encoder_status_t sc_encoder_reset_counts(uint32_t now_ms)
{
  (void)now_ms;
  memset(&s_encoder_snapshot, 0, sizeof(s_encoder_snapshot));
  s_encoder_snapshot.valid = 1u;
  s_encoder_snapshot.left.valid = 1u;
  s_encoder_snapshot.right.valid = 1u;
  return SC_ENCODER_OK;
}
