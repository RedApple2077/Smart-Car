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

static int init_required_ports(void)
{
  CHECK(sc_uart_init(SC_UART_USART1, 115200u) == SC_UART_OK);
  CHECK(sc_uart_init(SC_UART_USART2, 9600u) == SC_UART_OK);
  return 0;
}

static int test_init_and_idempotency(void)
{
  fake_hal_reset();

  CHECK(sc_uart_init(SC_UART_USART1, 115200u) == SC_UART_OK);
  CHECK(fake_receive_call_count(&huart1) == 1u);
  CHECK(sc_uart_init(SC_UART_USART1, 115200u) == SC_UART_OK);
  CHECK(fake_receive_call_count(&huart1) == 1u);

  CHECK(sc_uart_init(SC_UART_USART2, 9600u) == SC_UART_OK);
  CHECK(fake_receive_call_count(&huart2) == 1u);
  CHECK(sc_uart_init(SC_UART_USART2, 9600u) == SC_UART_OK);
  CHECK(fake_receive_call_count(&huart2) == 1u);
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

  CHECK(sc_uart_init((sc_uart_port_t)0, 115200u) ==
        SC_UART_ERR_ARGUMENT);
  CHECK(sc_uart_init((sc_uart_port_t)3, 115200u) ==
        SC_UART_ERR_ARGUMENT);
  CHECK(sc_uart_init(SC_UART_USART1, 0u) == SC_UART_ERR_ARGUMENT);
  CHECK(sc_uart_init(SC_UART_USART1, 9600u) == SC_UART_ERR_CONFIG);

  word_length = huart1.Init.WordLength;
  huart1.Init.WordLength = UART_WORDLENGTH_9B;
  CHECK(sc_uart_init(SC_UART_USART1, 115200u) == SC_UART_ERR_CONFIG);
  huart1.Init.WordLength = word_length;

  stop_bits = huart1.Init.StopBits;
  huart1.Init.StopBits = UART_STOPBITS_2;
  CHECK(sc_uart_init(SC_UART_USART1, 115200u) == SC_UART_ERR_CONFIG);
  huart1.Init.StopBits = stop_bits;

  parity = huart1.Init.Parity;
  huart1.Init.Parity = UART_PARITY_EVEN;
  CHECK(sc_uart_init(SC_UART_USART1, 115200u) == SC_UART_ERR_CONFIG);
  huart1.Init.Parity = parity;

  mode = huart1.Init.Mode;
  huart1.Init.Mode = UART_MODE_RX;
  CHECK(sc_uart_init(SC_UART_USART1, 115200u) == SC_UART_ERR_CONFIG);
  huart1.Init.Mode = mode;

  flow = huart1.Init.HwFlowCtl;
  huart1.Init.HwFlowCtl = 1u;
  CHECK(sc_uart_init(SC_UART_USART1, 115200u) == SC_UART_ERR_CONFIG);
  huart1.Init.HwFlowCtl = flow;

  instance = huart1.Instance;
  huart1.Instance = USART2;
  CHECK(sc_uart_init(SC_UART_USART1, 115200u) == SC_UART_ERR_CONFIG);
  huart1.Instance = instance;

  state = huart1.gState;
  huart1.gState = HAL_UART_STATE_BUSY_TX;
  CHECK(sc_uart_init(SC_UART_USART1, 115200u) == SC_UART_ERR_BUSY);
  huart1.gState = state;

  state = huart1.RxState;
  huart1.RxState = HAL_UART_STATE_ERROR;
  CHECK(sc_uart_init(SC_UART_USART1, 115200u) ==
        SC_UART_ERR_NOT_READY);
  huart1.RxState = state;

  fake_fail_next_receive(HAL_ERROR);
  CHECK(sc_uart_init(SC_UART_USART1, 115200u) == SC_UART_ERR_HAL);
  CHECK(huart1.RxState == HAL_UART_STATE_READY);
  CHECK(sc_uart_init(SC_UART_USART1, 115200u) == SC_UART_OK);

  CHECK(sc_uart_init(SC_UART_USART2, 9600u) == SC_UART_OK);
  CHECK(sc_uart_init(SC_UART_USART2, 115200u) == SC_UART_ERR_CONFIG);
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
  CHECK(sc_uart_init(SC_UART_USART1, 115200u) == SC_UART_OK);

  for (index = 0u; index < sizeof(payload); index++)
  {
    payload[index] = (uint8_t)index;
  }
  CHECK(sc_uart_write_bytes(SC_UART_USART1, payload, sizeof(payload)) ==
        SC_UART_OK);
  CHECK(fake_transmit_call_count(&huart1) == 1u);
  CHECK(huart1.TxXferSize == 512u);

  CHECK(sc_uart_write_bytes(SC_UART_USART1, &extra, 1u) ==
        SC_UART_ERR_BUFFER_FULL);
  CHECK(sc_uart_get_stats(SC_UART_USART1, &stats) == SC_UART_OK);
  CHECK(stats.tx_rejected_writes == 1u);

  fake_complete_transmit(&huart1);
  CHECK(fake_copy_transmitted(&huart1, history, sizeof(history)) ==
        sizeof(payload));
  CHECK(memcmp(history, payload, sizeof(payload)) == 0);

  memset(text, 'x', sizeof(text));
  text[512] = '\0';
  CHECK(sc_uart_write(SC_UART_USART1, text) == SC_UART_OK);
  fake_complete_transmit(&huart1);

  memset(text, 'y', sizeof(text));
  text[513] = '\0';
  CHECK(sc_uart_write(SC_UART_USART1, text) ==
        SC_UART_ERR_BUFFER_FULL);
  CHECK(sc_uart_write_bytes(SC_UART_USART1, NULL, 0u) == SC_UART_OK);
  CHECK(sc_uart_write_bytes(SC_UART_USART1, NULL, 1u) ==
        SC_UART_ERR_ARGUMENT);
  CHECK(sc_uart_read(SC_UART_USART1, NULL, 0u, &received) == SC_UART_OK);
  CHECK(received == 0u);
  CHECK(sc_uart_read(SC_UART_USART1, NULL, 1u, &received) ==
        SC_UART_ERR_ARGUMENT);
  CHECK(sc_uart_read(SC_UART_USART1, &extra, 1u, NULL) ==
        SC_UART_ERR_ARGUMENT);
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
  CHECK(sc_uart_init(SC_UART_USART1, 115200u) == SC_UART_OK);

  for (index = 0u; index < sizeof(first); index++)
  {
    first[index] = (uint8_t)(index + 1u);
  }
  CHECK(sc_uart_write_bytes(SC_UART_USART1, first, sizeof(first)) ==
        SC_UART_OK);
  fake_complete_transmit(&huart1);

  for (index = 0u; index < sizeof(second); index++)
  {
    second[index] = (uint8_t)(0x80u + index);
    expected[index] = second[index];
  }
  CHECK(sc_uart_write_bytes(SC_UART_USART1, second, sizeof(second)) ==
        SC_UART_OK);
  CHECK(huart1.TxXferSize == 112u);
  fake_complete_transmit(&huart1);
  CHECK(huart1.gState == HAL_UART_STATE_BUSY_TX);
  CHECK(huart1.TxXferSize == 188u);
  fake_complete_transmit(&huart1);

  CHECK(fake_copy_transmitted(&huart1, history, sizeof(history)) ==
        sizeof(history));
  CHECK(memcmp(history, expected, sizeof(expected)) == 0);
  CHECK(huart1.gState == HAL_UART_STATE_READY);
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
  CHECK(sc_uart_init(SC_UART_USART1, 115200u) == SC_UART_OK);

  fake_fail_next_transmit(HAL_BUSY);
  CHECK(sc_uart_write_bytes(SC_UART_USART1, rejected, sizeof(rejected)) ==
        SC_UART_ERR_BUSY);
  CHECK(sc_uart_get_stats(SC_UART_USART1, &stats) == SC_UART_OK);
  CHECK(stats.tx_start_failures == 1u);
  CHECK(stats.tx_rejected_writes == 1u);
  CHECK(fake_transmit_call_count(&huart1) == 1u);

  calls_before_service = fake_transmit_call_count(&huart1);
  sc_uart_service();
  CHECK(fake_transmit_call_count(&huart1) == calls_before_service);
  fake_set_tick(1u);
  sc_uart_service();
  CHECK(fake_transmit_call_count(&huart1) == calls_before_service);

  memset(first, 0x31, sizeof(first));
  CHECK(sc_uart_write_bytes(SC_UART_USART1, first, sizeof(first)) ==
        SC_UART_OK);
  fake_complete_transmit(&huart1);

  memset(second, 0x62, sizeof(second));
  CHECK(sc_uart_write_bytes(SC_UART_USART1, second, sizeof(second)) ==
        SC_UART_OK);
  fake_fail_next_transmit(HAL_ERROR);
  calls_before_service = fake_transmit_call_count(&huart1);
  fake_complete_transmit(&huart1);
  CHECK(fake_transmit_call_count(&huart1) == calls_before_service + 1u);
  sc_uart_service();
  CHECK(fake_transmit_call_count(&huart1) == calls_before_service + 1u);

  fake_set_tick(2u);
  sc_uart_service();
  CHECK(fake_transmit_call_count(&huart1) == calls_before_service + 2u);
  CHECK(huart1.TxXferSize == 88u);
  fake_complete_transmit(&huart1);

  CHECK(fake_copy_transmitted(&huart1, history, sizeof(history)) ==
        sizeof(history));
  CHECK(memcmp(history, second, sizeof(second)) == 0);
  CHECK(sc_uart_get_stats(SC_UART_USART1, &stats) == SC_UART_OK);
  CHECK(stats.tx_start_failures == 2u);
  CHECK(stats.tx_rejected_writes == 1u);
  return 0;
}

static int test_rx_overflow(void)
{
  uint8_t expected[128];
  uint8_t actual[128];
  uint8_t follow_up = 0x55u;
  size_t received;
  size_t index;
  sc_uart_stats_t stats;

  fake_hal_reset();
  CHECK(sc_uart_init(SC_UART_USART1, 115200u) == SC_UART_OK);

  for (index = 0u; index < sizeof(expected); index++)
  {
    expected[index] = (uint8_t)(index ^ 0x5Au);
    fake_deliver_receive(&huart1, expected[index]);
  }
  fake_deliver_receive(&huart1, 0xAAu);

  CHECK(sc_uart_get_stats(SC_UART_USART1, &stats) == SC_UART_OK);
  CHECK(stats.rx_overflow_bytes == 1u);
  CHECK(sc_uart_read(SC_UART_USART1, actual, sizeof(actual), &received) ==
        SC_UART_OK);
  CHECK(received == sizeof(expected));
  CHECK(memcmp(actual, expected, sizeof(expected)) == 0);

  fake_deliver_receive(&huart1, follow_up);
  CHECK(sc_uart_read(SC_UART_USART1, actual, sizeof(actual), &received) ==
        SC_UART_OK);
  CHECK(received == 1u);
  CHECK(actual[0] == follow_up);
  return 0;
}

static int test_rx_wrap(void)
{
  uint8_t input[100];
  uint8_t segment[80];
  uint8_t expected[120];
  uint8_t actual[120];
  size_t received;
  size_t index;

  fake_hal_reset();
  CHECK(sc_uart_init(SC_UART_USART1, 115200u) == SC_UART_OK);

  for (index = 0u; index < sizeof(input); index++)
  {
    input[index] = (uint8_t)(index + 0x10u);
    fake_deliver_receive(&huart1, input[index]);
  }
  CHECK(sc_uart_read(SC_UART_USART1, segment, sizeof(segment), &received) ==
        SC_UART_OK);
  CHECK(received == sizeof(segment));
  CHECK(memcmp(segment, input, sizeof(segment)) == 0);

  memcpy(expected, &input[80], 20u);
  for (index = 0u; index < 100u; index++)
  {
    uint8_t byte = (uint8_t)(0x90u + index);
    expected[20u + index] = byte;
    fake_deliver_receive(&huart1, byte);
  }

  CHECK(sc_uart_read(SC_UART_USART1, actual, sizeof(actual), &received) ==
        SC_UART_OK);
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
  CHECK(sc_uart_init(SC_UART_USART1, 115200u) == SC_UART_OK);
  calls = fake_receive_call_count(&huart1);

  fake_deliver_receive_error_with_complete(&huart1, 0xEEu,
                                           HAL_UART_ERROR_ORE);
  CHECK(fake_receive_call_count(&huart1) == calls + 1u);
  CHECK(sc_uart_read(SC_UART_USART1, &byte, 1u, &received) == SC_UART_OK);
  CHECK(received == 0u);
  CHECK(sc_uart_get_stats(SC_UART_USART1, &stats) == SC_UART_OK);
  CHECK(stats.rx_ore_events == 1u);
  CHECK(stats.rx_error_bytes == 1u);

  fake_deliver_receive(&huart1, 0x41u);
  CHECK(sc_uart_read(SC_UART_USART1, &byte, 1u, &received) == SC_UART_OK);
  CHECK((received == 1u) && (byte == 0x41u));

  calls = fake_receive_call_count(&huart1);
  fake_deliver_receive_error_with_complete(
    &huart1, 0xEFu, HAL_UART_ERROR_FE | HAL_UART_ERROR_NE);
  CHECK(fake_receive_call_count(&huart1) == calls + 1u);
  CHECK(sc_uart_read(SC_UART_USART1, &byte, 1u, &received) == SC_UART_OK);
  CHECK(received == 0u);
  CHECK(sc_uart_get_stats(SC_UART_USART1, &stats) == SC_UART_OK);
  CHECK(stats.rx_fe_events == 1u);
  CHECK(stats.rx_ne_events == 1u);
  CHECK(stats.rx_error_bytes == 2u);

  fake_deliver_receive(&huart1, 0x42u);
  CHECK(sc_uart_read(SC_UART_USART1, &byte, 1u, &received) == SC_UART_OK);
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
  CHECK(sc_uart_init(SC_UART_USART1, 115200u) == SC_UART_OK);
  calls = fake_receive_call_count(&huart1);

  fake_deliver_error_without_rxne(&huart1, HAL_UART_ERROR_FE);
  CHECK(fake_receive_call_count(&huart1) == calls);

  fake_deliver_receive(&huart1, 0x42u);
  CHECK(sc_uart_read(SC_UART_USART1, &byte, 1u, &received) == SC_UART_OK);
  CHECK((received == 1u) && (byte == 0x42u));

  fake_fail_next_receive(HAL_BUSY);
  fake_deliver_receive(&huart1, 0x43u);
  CHECK(sc_uart_read(SC_UART_USART1, &byte, 1u, &received) == SC_UART_OK);
  CHECK((received == 1u) && (byte == 0x43u));
  CHECK(sc_uart_get_stats(SC_UART_USART1, &stats) == SC_UART_OK);
  CHECK(stats.rx_rearm_failures == 1u);

  calls = fake_receive_call_count(&huart1);
  sc_uart_service();
  CHECK(fake_receive_call_count(&huart1) == calls);
  fake_set_tick(1u);
  sc_uart_service();
  CHECK(fake_receive_call_count(&huart1) == calls + 1u);

  fake_deliver_receive(&huart1, 0x44u);
  CHECK(sc_uart_read(SC_UART_USART1, &byte, 1u, &received) == SC_UART_OK);
  CHECK((received == 1u) && (byte == 0x44u));
  return 0;
}

static int test_dual_port_isolation(void)
{
  uint8_t byte1 = 0x11u;
  uint8_t byte2 = 0x22u;
  uint8_t byte;
  uint8_t history[1];
  size_t received;
  sc_uart_stats_t stats1;
  sc_uart_stats_t stats2;

  fake_hal_reset();
  CHECK(init_required_ports() == 0);

  fake_deliver_receive(&huart1, byte1);
  fake_deliver_receive(&huart2, byte2);
  CHECK(sc_uart_read(SC_UART_USART1, &byte, 1u, &received) == SC_UART_OK);
  CHECK((received == 1u) && (byte == byte1));
  CHECK(sc_uart_read(SC_UART_USART2, &byte, 1u, &received) == SC_UART_OK);
  CHECK((received == 1u) && (byte == byte2));

  CHECK(sc_uart_write(SC_UART_USART1, "A") == SC_UART_OK);
  CHECK(sc_uart_write(SC_UART_USART2, "B") == SC_UART_OK);
  fake_complete_transmit(&huart1);
  CHECK(fake_copy_transmitted(&huart1, history, sizeof(history)) == 1u);
  CHECK(history[0] == (uint8_t)'A');
  CHECK(huart2.gState == HAL_UART_STATE_BUSY_TX);
  fake_complete_transmit(&huart2);
  CHECK(fake_copy_transmitted(&huart2, history, sizeof(history)) == 1u);
  CHECK(history[0] == (uint8_t)'B');

  fake_deliver_receive_error_with_complete(&huart1, 0xEEu,
                                           HAL_UART_ERROR_FE);
  fake_deliver_receive(&huart2, 0x33u);
  CHECK(sc_uart_read(SC_UART_USART2, &byte, 1u, &received) == SC_UART_OK);
  CHECK((received == 1u) && (byte == 0x33u));
  CHECK(sc_uart_get_stats(SC_UART_USART1, &stats1) == SC_UART_OK);
  CHECK(sc_uart_get_stats(SC_UART_USART2, &stats2) == SC_UART_OK);
  CHECK(stats1.rx_fe_events == 1u);
  CHECK(stats2.rx_fe_events == 0u);
  CHECK(stats2.rx_overflow_bytes == 0u);
  return 0;
}

typedef struct {
  const char *name;
  int (*run)(void);
} test_case_t;

static const test_case_t s_tests[] = {
  {"init_and_idempotency", test_init_and_idempotency},
  {"init_config_errors", test_init_config_errors},
  {"tx_boundary", test_tx_boundary},
  {"tx_wrap", test_tx_wrap},
  {"tx_start_retry", test_tx_start_retry},
  {"rx_overflow", test_rx_overflow},
  {"rx_wrap", test_rx_wrap},
  {"rx_error_ordering", test_rx_error_ordering},
  {"rx_error_recovery", test_rx_error_recovery},
  {"dual_port_isolation", test_dual_port_isolation}
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
