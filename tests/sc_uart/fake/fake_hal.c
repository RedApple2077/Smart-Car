#include "main.h"

#include <string.h>

#define FAKE_HISTORY_CAPACITY 8192u

typedef struct {
  uint32_t tx_calls;
  uint32_t rx_calls;
  HAL_StatusTypeDef next_tx_failure;
  HAL_StatusTypeDef next_rx_failure;
  uint8_t history[FAKE_HISTORY_CAPACITY];
  size_t history_len;
} fake_port_state_t;

USART_TypeDef fake_usart1_regs;
USART_TypeDef fake_usart2_regs;
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

static fake_port_state_t s_port1;
static fake_port_state_t s_port2;
static uint32_t s_tick;

static fake_port_state_t *fake_state_for_handle(
  const UART_HandleTypeDef *huart)
{
  if ((huart != NULL) && (huart->Instance == USART1))
  {
    return &s_port1;
  }
  if ((huart != NULL) && (huart->Instance == USART2))
  {
    return &s_port2;
  }
  return NULL;
}

static uint32_t fake_sr_mask_from_error(uint32_t error_code)
{
  uint32_t mask = 0u;

  if ((error_code & HAL_UART_ERROR_PE) != 0u)
  {
    mask |= USART_SR_PE;
  }
  if ((error_code & HAL_UART_ERROR_FE) != 0u)
  {
    mask |= USART_SR_FE;
  }
  if ((error_code & HAL_UART_ERROR_NE) != 0u)
  {
    mask |= USART_SR_NE;
  }
  if ((error_code & HAL_UART_ERROR_ORE) != 0u)
  {
    mask |= USART_SR_ORE;
  }
  return mask;
}

static void fake_configure_handle(UART_HandleTypeDef *huart,
                                  USART_TypeDef *instance, uint32_t baud)
{
  huart->Instance = instance;
  huart->Init.BaudRate = baud;
  huart->Init.WordLength = UART_WORDLENGTH_8B;
  huart->Init.StopBits = UART_STOPBITS_1;
  huart->Init.Parity = UART_PARITY_NONE;
  huart->Init.Mode = UART_MODE_TX_RX;
  huart->Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart->Init.OverSampling = UART_OVERSAMPLING_16;
  huart->gState = HAL_UART_STATE_READY;
  huart->RxState = HAL_UART_STATE_READY;
  huart->ErrorCode = HAL_UART_ERROR_NONE;
}

void fake_hal_reset(void)
{
  memset(&fake_usart1_regs, 0, sizeof(fake_usart1_regs));
  memset(&fake_usart2_regs, 0, sizeof(fake_usart2_regs));
  memset(&huart1, 0, sizeof(huart1));
  memset(&huart2, 0, sizeof(huart2));
  memset(&s_port1, 0, sizeof(s_port1));
  memset(&s_port2, 0, sizeof(s_port2));
  s_tick = 0u;

  fake_configure_handle(&huart1, USART1, 115200u);
  fake_configure_handle(&huart2, USART2, 9600u);
}

uint32_t HAL_GetTick(void)
{
  return s_tick;
}

void fake_set_tick(uint32_t tick)
{
  s_tick = tick;
}

HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *huart,
                                       const uint8_t *pData, uint16_t Size)
{
  fake_port_state_t *state = fake_state_for_handle(huart);

  if (state == NULL)
  {
    return HAL_ERROR;
  }

  state->tx_calls++;
  if (state->next_tx_failure != HAL_OK)
  {
    HAL_StatusTypeDef failure = state->next_tx_failure;
    state->next_tx_failure = HAL_OK;
    return failure;
  }
  if ((pData == NULL) || (Size == 0u))
  {
    return HAL_ERROR;
  }
  if (huart->gState != HAL_UART_STATE_READY)
  {
    return HAL_BUSY;
  }

  huart->pTxBuffPtr = pData;
  huart->TxXferSize = Size;
  huart->TxXferCount = Size;
  huart->ErrorCode = HAL_UART_ERROR_NONE;
  huart->gState = HAL_UART_STATE_BUSY_TX;
  return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *huart,
                                      uint8_t *pData, uint16_t Size)
{
  fake_port_state_t *state = fake_state_for_handle(huart);

  if (state == NULL)
  {
    return HAL_ERROR;
  }

  state->rx_calls++;
  if (state->next_rx_failure != HAL_OK)
  {
    HAL_StatusTypeDef failure = state->next_rx_failure;
    state->next_rx_failure = HAL_OK;
    return failure;
  }
  if ((pData == NULL) || (Size == 0u))
  {
    return HAL_ERROR;
  }
  if (huart->RxState != HAL_UART_STATE_READY)
  {
    return HAL_BUSY;
  }

  huart->pRxBuffPtr = pData;
  huart->RxXferSize = Size;
  huart->RxXferCount = Size;
  huart->ErrorCode = HAL_UART_ERROR_NONE;
  huart->RxState = HAL_UART_STATE_BUSY_RX;
  return HAL_OK;
}

void fake_fail_next_transmit(HAL_StatusTypeDef status)
{
  s_port1.next_tx_failure = status;
}

void fake_fail_next_receive(HAL_StatusTypeDef status)
{
  s_port1.next_rx_failure = status;
}

uint32_t fake_transmit_call_count(const UART_HandleTypeDef *huart)
{
  fake_port_state_t *state = fake_state_for_handle(huart);
  return (state == NULL) ? 0u : state->tx_calls;
}

uint32_t fake_receive_call_count(const UART_HandleTypeDef *huart)
{
  fake_port_state_t *state = fake_state_for_handle(huart);
  return (state == NULL) ? 0u : state->rx_calls;
}

void fake_complete_transmit(UART_HandleTypeDef *huart)
{
  fake_port_state_t *state = fake_state_for_handle(huart);
  size_t available;
  size_t copy_len;

  if ((state == NULL) || (huart->gState != HAL_UART_STATE_BUSY_TX) ||
      (huart->pTxBuffPtr == NULL))
  {
    return;
  }

  available = FAKE_HISTORY_CAPACITY - state->history_len;
  copy_len = huart->TxXferSize;
  if (copy_len > available)
  {
    copy_len = available;
  }
  memcpy(&state->history[state->history_len], huart->pTxBuffPtr, copy_len);
  state->history_len += copy_len;

  huart->TxXferCount = 0u;
  huart->gState = HAL_UART_STATE_READY;
  HAL_UART_TxCpltCallback(huart);
}

void fake_deliver_receive(UART_HandleTypeDef *huart, uint8_t byte)
{
  if ((huart == NULL) || (huart->pRxBuffPtr == NULL) ||
      (huart->RxState != HAL_UART_STATE_BUSY_RX))
  {
    return;
  }

  *huart->pRxBuffPtr = byte;
  huart->RxXferCount = 0u;
  huart->RxState = HAL_UART_STATE_READY;
  huart->ErrorCode = HAL_UART_ERROR_NONE;
  huart->Instance->SR &= ~USART_SR_RXNE;
  HAL_UART_RxCpltCallback(huart);
}

void fake_deliver_receive_error_with_complete(UART_HandleTypeDef *huart,
                                              uint8_t byte,
                                              uint32_t error_code)
{
  if ((huart == NULL) || (huart->pRxBuffPtr == NULL) ||
      (huart->RxState != HAL_UART_STATE_BUSY_RX))
  {
    return;
  }

  *huart->pRxBuffPtr = byte;
  huart->RxXferCount = 0u;
  huart->RxState = HAL_UART_STATE_READY;
  huart->ErrorCode = error_code;
  huart->Instance->SR |= fake_sr_mask_from_error(error_code) | USART_SR_RXNE;

  /* 模拟 HAL 在 RXNE 与错误同时置位时的 RxCplt -> ErrorCallback 顺序。 */
  HAL_UART_RxCpltCallback(huart);
  HAL_UART_ErrorCallback(huart);
}

void fake_deliver_error_without_rxne(UART_HandleTypeDef *huart,
                                     uint32_t error_code)
{
  if ((huart == NULL) || (huart->RxState != HAL_UART_STATE_BUSY_RX))
  {
    return;
  }

  huart->ErrorCode = error_code;
  huart->Instance->SR |= fake_sr_mask_from_error(error_code);
  HAL_UART_ErrorCallback(huart);
}

size_t fake_copy_transmitted(const UART_HandleTypeDef *huart, uint8_t *dst,
                             size_t capacity)
{
  fake_port_state_t *state = fake_state_for_handle(huart);
  size_t start;
  size_t count;

  if ((state == NULL) || ((dst == NULL) && (capacity != 0u)))
  {
    return 0u;
  }

  count = (state->history_len < capacity) ? state->history_len : capacity;
  start = state->history_len - count;
  if ((dst != NULL) && (count != 0u))
  {
    memcpy(dst, &state->history[start], count);
  }
  return count;
}
