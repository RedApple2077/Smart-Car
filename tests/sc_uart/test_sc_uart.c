#include "main.h"
#include "sc_uart.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression)                                                     \
  do                                                                          \
  {                                                                           \
    if (!(expression))                                                        \
    {                                                                         \
      fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__,     \
              #expression);                                                   \
      return 1;                                                               \
    }                                                                         \
  } while (0)

static int test_init_and_idempotency(void)
{
  fake_hal_reset();

  CHECK(sc_uart_init(9600u, 0u) == SC_UART_OK);
  CHECK(fake_receive_call_count(&huart2) == 0u);
  CHECK(sc_uart_init(9600u, 0u) == SC_UART_OK);
  CHECK(fake_receive_call_count(&huart2) == 0u);
  CHECK(sc_uart_init(9600u, 1u) == SC_UART_ERR_CONFIG);
  return 0;
}

static int test_rx_enabled_init_idempotency(void)
{
  fake_hal_reset();
  CHECK(sc_uart_init(9600u, 1u) == SC_UART_OK);
  CHECK(fake_receive_call_count(&huart2) == 1u);
  CHECK(sc_uart_init(9600u, 1u) == SC_UART_OK);
  CHECK(fake_receive_call_count(&huart2) == 1u);
  CHECK(sc_uart_init(9600u, 0u) == SC_UART_ERR_CONFIG);
  return 0;
}

static int test_init_config_errors(void)
{
  uint32_t word_length;
  uint32_t stop_bits;
  uint32_t parity;
  uint32_t mode;
  uint32_t flow;
  USART_TypeDef *instance;
  HAL_UART_StateTypeDef state;

  fake_hal_reset();

  CHECK(sc_uart_init(0u, 0u) == SC_UART_ERR_ARGUMENT);
  CHECK(sc_uart_init(9600u, 2u) == SC_UART_ERR_ARGUMENT);
  CHECK(sc_uart_init(19200u, 0u) == SC_UART_ERR_CONFIG);

  word_length = huart2.Init.WordLength;
  huart2.Init.WordLength = UART_WORDLENGTH_9B;
  CHECK(sc_uart_init(9600u, 0u) == SC_UART_ERR_CONFIG);
  huart2.Init.WordLength = word_length;

  stop_bits = huart2.Init.StopBits;
  huart2.Init.StopBits = UART_STOPBITS_2;
  CHECK(sc_uart_init(9600u, 0u) == SC_UART_ERR_CONFIG);
  huart2.Init.StopBits = stop_bits;

  parity = huart2.Init.Parity;
  huart2.Init.Parity = UART_PARITY_EVEN;
  CHECK(sc_uart_init(9600u, 0u) == SC_UART_ERR_CONFIG);
  huart2.Init.Parity = parity;

  mode = huart2.Init.Mode;
  huart2.Init.Mode = UART_MODE_RX;
  CHECK(sc_uart_init(9600u, 0u) == SC_UART_ERR_CONFIG);
  huart2.Init.Mode = mode;

  flow = huart2.Init.HwFlowCtl;
  huart2.Init.HwFlowCtl = 1u;
  CHECK(sc_uart_init(9600u, 0u) == SC_UART_ERR_CONFIG);
  huart2.Init.HwFlowCtl = flow;

  instance = huart2.Instance;
  huart2.Instance = NULL;
  CHECK(sc_uart_init(9600u, 0u) == SC_UART_ERR_CONFIG);
  huart2.Instance = instance;

  state = huart2.gState;
  huart2.gState = HAL_UART_STATE_BUSY_TX;
  CHECK(sc_uart_init(9600u, 0u) == SC_UART_ERR_BUSY);
  huart2.gState = state;

  state = huart2.RxState;
  huart2.RxState = HAL_UART_STATE_ERROR;
  CHECK(sc_uart_init(9600u, 1u) == SC_UART_ERR_NOT_READY);
  huart2.RxState = HAL_UART_STATE_READY;

  fake_fail_next_receive(HAL_ERROR);
  CHECK(sc_uart_init(9600u, 1u) == SC_UART_ERR_HAL);
  CHECK(huart2.RxState == HAL_UART_STATE_READY);
  CHECK(sc_uart_init(9600u, 1u) == SC_UART_OK);
  return 0;
}

static int test_rx_disabled_ignores_rx_state(void)
{
  fake_hal_reset();
  huart2.RxState = HAL_UART_STATE_ERROR;
  CHECK(sc_uart_init(9600u, 0u) == SC_UART_OK);
  CHECK(fake_receive_call_count(&huart2) == 0u);
  return 0;
}

static int test_tx_boundary(void)
{
  uint8_t payload[512];
  uint8_t history[512];
  uint8_t extra = 0xA5u;
  char text[514];
  size_t index;
  size_t received;
  sc_uart_stats_t stats;

  fake_hal_reset();
  CHECK(sc_uart_init(9600u, 0u) == SC_UART_OK);

  for (index = 0u; index < sizeof(payload); index++)
  {
    payload[index] = (uint8_t)index;
  }
  CHECK(sc_uart_write_bytes(payload, sizeof(payload)) == SC_UART_OK);
  CHECK(fake_transmit_call_count(&huart2) == 1u);
  CHECK(huart2.TxXferSize == 512u);

  CHECK(sc_uart_write_bytes(&extra, 1u) == SC_UART_ERR_BUFFER_FULL);
  CHECK(sc_uart_get_stats(&stats) == SC_UART_OK);
  CHECK(stats.tx_rejected_writes == 1u);

  fake_complete_transmit(&huart2);
  CHECK(fake_copy_transmitted(&huart2, history, sizeof(history)) ==
        sizeof(payload));
  CHECK(memcmp(history, payload, sizeof(payload)) == 0);

  memset(text, 'x', sizeof(text));
  text[512] = '\0';
  CHECK(sc_uart_write(text) == SC_UART_OK);
  fake_complete_transmit(&huart2);

  memset(text, 'y', sizeof(text));
  text[513] = '\0';
  CHECK(sc_uart_write(text) == SC_UART_ERR_BUFFER_FULL);
  CHECK(sc_uart_write_bytes(NULL, 0u) == SC_UART_OK);
  CHECK(sc_uart_write_bytes(NULL, 1u) == SC_UART_ERR_ARGUMENT);
  CHECK(sc_uart_read(NULL, 0u, &received) == SC_UART_ERR_NOT_READY);
  CHECK(sc_uart_read(NULL, 1u, &received) == SC_UART_ERR_ARGUMENT);
  CHECK(sc_uart_read(&extra, 1u, NULL) == SC_UART_ERR_ARGUMENT);
  return 0;
}

static int test_tx_wrap(void)
{
  uint8_t first[400];
  uint8_t second[300];
  uint8_t expected[300];
  uint8_t history[300];
  size_t index;

  fake_hal_reset();
  CHECK(sc_uart_init(9600u, 0u) == SC_UART_OK);

  for (index = 0u; index < sizeof(first); index++)
  {
    first[index] = (uint8_t)(index + 1u);
  }
  CHECK(sc_uart_write_bytes(first, sizeof(first)) == SC_UART_OK);
  fake_complete_transmit(&huart2);

  for (index = 0u; index < sizeof(second); index++)
  {
    second[index] = (uint8_t)(0x80u + index);
    expected[index] = second[index];
  }
  CHECK(sc_uart_write_bytes(second, sizeof(second)) == SC_UART_OK);
  CHECK(huart2.TxXferSize == 112u);
  fake_complete_transmit(&huart2);
  CHECK(huart2.gState == HAL_UART_STATE_BUSY_TX);
  CHECK(huart2.TxXferSize == 188u);
  fake_complete_transmit(&huart2);

  CHECK(fake_copy_transmitted(&huart2, history, sizeof(history)) ==
        sizeof(history));
  CHECK(memcmp(history, expected, sizeof(expected)) == 0);
  CHECK(huart2.gState == HAL_UART_STATE_READY);
  return 0;
}

static int test_tx_start_retry(void)
{
  uint8_t first[500];
  uint8_t second[100];
  uint8_t history[100];
  uint8_t rejected[4] = {1u, 2u, 3u, 4u};
  uint32_t calls_before_service;
  sc_uart_stats_t stats;

  fake_hal_reset();
  CHECK(sc_uart_init(9600u, 0u) == SC_UART_OK);

  fake_fail_next_transmit(HAL_BUSY);
  CHECK(sc_uart_write_bytes(rejected, sizeof(rejected)) ==
        SC_UART_ERR_BUSY);
  CHECK(sc_uart_get_stats(&stats) == SC_UART_OK);
  CHECK(stats.tx_start_failures == 1u);
  CHECK(stats.tx_rejected_writes == 1u);
  CHECK(fake_transmit_call_count(&huart2) == 1u);

  calls_before_service = fake_transmit_call_count(&huart2);
  sc_uart_service();
  CHECK(fake_transmit_call_count(&huart2) == calls_before_service);
  fake_set_tick(1u);
  sc_uart_service();
  CHECK(fake_transmit_call_count(&huart2) == calls_before_service);

  memset(first, 0x31, sizeof(first));
  CHECK(sc_uart_write_bytes(first, sizeof(first)) == SC_UART_OK);
  fake_complete_transmit(&huart2);

  memset(second, 0x62, sizeof(second));
  CHECK(sc_uart_write_bytes(second, sizeof(second)) == SC_UART_OK);
  fake_fail_next_transmit(HAL_ERROR);
  calls_before_service = fake_transmit_call_count(&huart2);
  fake_complete_transmit(&huart2);
  CHECK(fake_transmit_call_count(&huart2) == calls_before_service + 1u);
  sc_uart_service();
  CHECK(fake_transmit_call_count(&huart2) == calls_before_service + 1u);

  fake_set_tick(2u);
  sc_uart_service();
  CHECK(fake_transmit_call_count(&huart2) == calls_before_service + 2u);
  CHECK(huart2.TxXferSize == 88u);
  fake_complete_transmit(&huart2);

  CHECK(fake_copy_transmitted(&huart2, history, sizeof(history)) ==
        sizeof(history));
  CHECK(memcmp(history, second, sizeof(second)) == 0);
  CHECK(sc_uart_get_stats(&stats) == SC_UART_OK);
  CHECK(stats.tx_start_failures == 2u);
  CHECK(stats.tx_rejected_writes == 1u);
  return 0;
}

static int test_rx_overflow(void)
{
  uint8_t expected[64];
  uint8_t actual[64];
  uint8_t follow_up = 0x55u;
  size_t received;
  size_t index;
  sc_uart_stats_t stats;

  fake_hal_reset();
  CHECK(sc_uart_init(9600u, 1u) == SC_UART_OK);

  for (index = 0u; index < sizeof(expected); index++)
  {
    expected[index] = (uint8_t)(index ^ 0x5Au);
    fake_deliver_receive(&huart2, expected[index]);
  }
  fake_deliver_receive(&huart2, 0xAAu);

  CHECK(sc_uart_get_stats(&stats) == SC_UART_OK);
  CHECK(stats.rx_overflow_bytes == 1u);
  CHECK(sc_uart_read(actual, sizeof(actual), &received) == SC_UART_OK);
  CHECK(received == sizeof(expected));
  CHECK(memcmp(actual, expected, sizeof(expected)) == 0);

  fake_deliver_receive(&huart2, follow_up);
  CHECK(sc_uart_read(actual, sizeof(actual), &received) == SC_UART_OK);
  CHECK(received == 1u);
  CHECK(actual[0] == follow_up);
  return 0;
}

static int test_rx_wrap(void)
{
  uint8_t input[48];
  uint8_t segment[32];
  uint8_t expected[64];
  uint8_t actual[64];
  size_t received;
  size_t index;

  fake_hal_reset();
  CHECK(sc_uart_init(9600u, 1u) == SC_UART_OK);

  for (index = 0u; index < sizeof(input); index++)
  {
    input[index] = (uint8_t)(index + 0x10u);
    fake_deliver_receive(&huart2, input[index]);
  }
  CHECK(sc_uart_read(segment, sizeof(segment), &received) == SC_UART_OK);
  CHECK(received == sizeof(segment));
  CHECK(memcmp(segment, input, sizeof(segment)) == 0);

  memcpy(expected, &input[32], 16u);
  for (index = 0u; index < 48u; index++)
  {
    uint8_t byte = (uint8_t)(0x90u + index);
    expected[16u + index] = byte;
    fake_deliver_receive(&huart2, byte);
  }

  CHECK(sc_uart_read(actual, sizeof(actual), &received) == SC_UART_OK);
  CHECK(received == sizeof(expected));
  CHECK(memcmp(actual, expected, sizeof(expected)) == 0);
  return 0;
}

static int test_rx_error_ordering(void)
{
  uint8_t byte;
  size_t received;
  uint32_t calls;
  sc_uart_stats_t stats;

  fake_hal_reset();
  CHECK(sc_uart_init(9600u, 1u) == SC_UART_OK);
  calls = fake_receive_call_count(&huart2);

  fake_deliver_receive_error_with_complete(&huart2, 0xEEu,
                                           HAL_UART_ERROR_ORE);
  CHECK(fake_receive_call_count(&huart2) == calls + 1u);
  CHECK(sc_uart_read(&byte, 1u, &received) == SC_UART_OK);
  CHECK(received == 0u);
  CHECK(sc_uart_get_stats(&stats) == SC_UART_OK);
  CHECK(stats.rx_ore_events == 1u);
  CHECK(stats.rx_error_bytes == 1u);

  fake_deliver_receive(&huart2, 0x41u);
  CHECK(sc_uart_read(&byte, 1u, &received) == SC_UART_OK);
  CHECK((received == 1u) && (byte == 0x41u));

  calls = fake_receive_call_count(&huart2);
  fake_deliver_receive_error_with_complete(
    &huart2, 0xEFu, HAL_UART_ERROR_FE | HAL_UART_ERROR_NE);
  CHECK(fake_receive_call_count(&huart2) == calls + 1u);
  CHECK(sc_uart_read(&byte, 1u, &received) == SC_UART_OK);
  CHECK(received == 0u);
  CHECK(sc_uart_get_stats(&stats) == SC_UART_OK);
  CHECK(stats.rx_fe_events == 1u);
  CHECK(stats.rx_ne_events == 1u);
  CHECK(stats.rx_error_bytes == 2u);

  fake_deliver_receive(&huart2, 0x42u);
  CHECK(sc_uart_read(&byte, 1u, &received) == SC_UART_OK);
  CHECK((received == 1u) && (byte == 0x42u));
  return 0;
}

static int test_rx_error_recovery(void)
{
  uint8_t byte;
  size_t received;
  uint32_t calls;
  sc_uart_stats_t stats;

  fake_hal_reset();
  CHECK(sc_uart_init(9600u, 1u) == SC_UART_OK);
  calls = fake_receive_call_count(&huart2);

  fake_deliver_error_without_rxne(&huart2, HAL_UART_ERROR_FE);
  CHECK(fake_receive_call_count(&huart2) == calls);

  fake_deliver_receive(&huart2, 0x42u);
  CHECK(sc_uart_read(&byte, 1u, &received) == SC_UART_OK);
  CHECK((received == 1u) && (byte == 0x42u));

  fake_fail_next_receive(HAL_BUSY);
  fake_deliver_receive(&huart2, 0x43u);
  CHECK(sc_uart_read(&byte, 1u, &received) == SC_UART_OK);
  CHECK((received == 1u) && (byte == 0x43u));
  CHECK(sc_uart_get_stats(&stats) == SC_UART_OK);
  CHECK(stats.rx_rearm_failures == 1u);

  calls = fake_receive_call_count(&huart2);
  sc_uart_service();
  CHECK(fake_receive_call_count(&huart2) == calls);
  fake_set_tick(1u);
  sc_uart_service();
  CHECK(fake_receive_call_count(&huart2) == calls + 1u);

  fake_deliver_receive(&huart2, 0x44u);
  CHECK(sc_uart_read(&byte, 1u, &received) == SC_UART_OK);
  CHECK((received == 1u) && (byte == 0x44u));
  return 0;
}

static int test_rx_disabled(void)
{
  uint8_t byte = 0u;
  uint8_t payload[2] = {0x11u, 0x22u};
  uint8_t history[2];
  size_t received;
  uint32_t calls;

  fake_hal_reset();
  CHECK(sc_uart_init(9600u, 0u) == SC_UART_OK);
  CHECK(fake_receive_call_count(&huart2) == 0u);
  CHECK(sc_uart_read(&byte, 1u, &received) == SC_UART_ERR_NOT_READY);

  calls = fake_receive_call_count(&huart2);
  sc_uart_service();
  fake_set_tick(1u);
  sc_uart_service();
  CHECK(fake_receive_call_count(&huart2) == calls);

  CHECK(sc_uart_write_bytes(payload, sizeof(payload)) == SC_UART_OK);
  fake_complete_transmit(&huart2);
  CHECK(fake_copy_transmitted(&huart2, history, sizeof(history)) ==
        sizeof(history));
  CHECK(memcmp(history, payload, sizeof(payload)) == 0);
  return 0;
}

static int test_rx_tx_independent(void)
{
  uint8_t byte;
  uint8_t transmitted;
  size_t received;

  fake_hal_reset();
  CHECK(sc_uart_init(9600u, 1u) == SC_UART_OK);
  fake_deliver_receive(&huart2, 0x33u);
  CHECK(sc_uart_write("B") == SC_UART_OK);
  CHECK(sc_uart_read(&byte, 1u, &received) == SC_UART_OK);
  CHECK((received == 1u) && (byte == 0x33u));
  fake_complete_transmit(&huart2);
  CHECK(fake_copy_transmitted(&huart2, &transmitted, 1u) == 1u);
  CHECK(transmitted == (uint8_t)'B');
  return 0;
}

typedef struct {
  const char *name;
  int (*run)(void);
} test_case_t;

static const test_case_t s_tests[] = {
  {"init_and_idempotency", test_init_and_idempotency},
  {"rx_enabled_init_idempotency", test_rx_enabled_init_idempotency},
  {"init_config_errors", test_init_config_errors},
  {"rx_disabled_ignores_rx_state", test_rx_disabled_ignores_rx_state},
  {"tx_boundary", test_tx_boundary},
  {"tx_wrap", test_tx_wrap},
  {"tx_start_retry", test_tx_start_retry},
  {"rx_overflow", test_rx_overflow},
  {"rx_wrap", test_rx_wrap},
  {"rx_error_ordering", test_rx_error_ordering},
  {"rx_error_recovery", test_rx_error_recovery},
  {"rx_disabled", test_rx_disabled},
  {"rx_tx_independent", test_rx_tx_independent}
};

int main(int argc, char **argv)
{
  size_t index;

  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <test-case>\n", argv[0]);
    return 2;
  }

  for (index = 0u; index < (sizeof(s_tests) / sizeof(s_tests[0])); index++)
  {
    if (strcmp(argv[1], s_tests[index].name) == 0)
    {
      return s_tests[index].run();
    }
  }

  fprintf(stderr, "unknown test case: %s\n", argv[1]);
  return 2;
}
