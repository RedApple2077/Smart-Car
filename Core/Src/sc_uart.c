/**
  ******************************************************************************
  * @file    sc_uart.c
  * @brief   USART1/USART2 非阻塞环形缓冲驱动。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "sc_uart.h"
#include "main.h"

#include <stdbool.h>

/* Private defines -----------------------------------------------------------*/
#define SC_UART_USART1_RX_CAPACITY  128u
#define SC_UART_USART1_TX_CAPACITY  512u
#define SC_UART_USART2_RX_CAPACITY  64u
#define SC_UART_USART2_TX_CAPACITY  512u

#define SC_UART_RETRY_INTERVAL_MS   1u

/* Private types -------------------------------------------------------------*/
typedef struct {
  uint8_t *data;
  size_t capacity;
  volatile uint32_t head;
  volatile uint32_t tail;
} sc_uart_ring_t;

typedef struct {
  UART_HandleTypeDef *hal;
  sc_uart_ring_t rx;
  sc_uart_ring_t tx;
  uint8_t rx_byte;
  volatile bool initialized;
  volatile bool rx_armed;
  volatile bool tx_active;
  volatile uint16_t tx_inflight;
  uint32_t baud;
  uint32_t next_rx_retry_tick;
  uint32_t next_tx_retry_tick;
  sc_uart_stats_t stats;
} sc_uart_context_t;

/* External variables --------------------------------------------------------*/
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;

/* Private variables ---------------------------------------------------------*/
static uint8_t s_usart1_rx_storage[SC_UART_USART1_RX_CAPACITY];
static uint8_t s_usart1_tx_storage[SC_UART_USART1_TX_CAPACITY];
static uint8_t s_usart2_rx_storage[SC_UART_USART2_RX_CAPACITY];
static uint8_t s_usart2_tx_storage[SC_UART_USART2_TX_CAPACITY];

static sc_uart_context_t s_contexts[3] = {
  [SC_UART_USART1] = {
    .hal = &huart1,
    .rx = {s_usart1_rx_storage, SC_UART_USART1_RX_CAPACITY, 0u, 0u},
    .tx = {s_usart1_tx_storage, SC_UART_USART1_TX_CAPACITY, 0u, 0u}
  },
  [SC_UART_USART2] = {
    .hal = &huart2,
    .rx = {s_usart2_rx_storage, SC_UART_USART2_RX_CAPACITY, 0u, 0u},
    .tx = {s_usart2_tx_storage, SC_UART_USART2_TX_CAPACITY, 0u, 0u}
  }
};

/* Private functions ---------------------------------------------------------*/
static uint32_t sc_uart_enter_critical(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

static void sc_uart_exit_critical(uint32_t primask)
{
  if (primask == 0u)
  {
    __enable_irq();
  }
  else
  {
    __disable_irq();
  }
}

static sc_uart_context_t *sc_uart_context_for_port(sc_uart_port_t port)
{
  if ((port != SC_UART_USART1) && (port != SC_UART_USART2))
  {
    return NULL;
  }

  return &s_contexts[(uint32_t)port];
}

static sc_uart_context_t *sc_uart_context_for_handle(
  const UART_HandleTypeDef *huart)
{
  if (huart == NULL)
  {
    return NULL;
  }

  if (huart->Instance == USART1)
  {
    return &s_contexts[SC_UART_USART1];
  }
  if (huart->Instance == USART2)
  {
    return &s_contexts[SC_UART_USART2];
  }

  return NULL;
}

static size_t sc_uart_ring_used(const sc_uart_ring_t *ring)
{
  uint32_t head = ring->head;
  uint32_t used = head - ring->tail;

  if (used > ring->capacity)
  {
    used = (uint32_t)ring->capacity;
  }
  return (size_t)used;
}

static size_t sc_uart_ring_free(const sc_uart_ring_t *ring)
{
  return ring->capacity - sc_uart_ring_used(ring);
}

static void sc_uart_ring_push(sc_uart_ring_t *ring, uint8_t byte,
                              sc_uart_stats_t *stats)
{
  if (sc_uart_ring_free(ring) == 0u)
  {
    stats->rx_overflow_bytes++;
    return;
  }

  ring->data[ring->head & (ring->capacity - 1u)] = byte;
  ring->head++;
}

static void sc_uart_ring_copy_in(sc_uart_ring_t *ring, uint32_t head,
                                 const uint8_t *data, size_t len)
{
  size_t offset;

  for (offset = 0u; offset < len; offset++)
  {
    ring->data[(head + (uint32_t)offset) & (ring->capacity - 1u)] = data[offset];
  }
}

static size_t sc_uart_ring_pop(sc_uart_ring_t *ring, uint8_t *dst,
                               size_t capacity)
{
  uint32_t head;
  uint32_t tail;
  uint32_t primask;
  size_t count;
  size_t offset;

  primask = sc_uart_enter_critical();
  head = ring->head;
  tail = ring->tail;
  sc_uart_exit_critical(primask);

  count = (size_t)(head - tail);
  if (count > ring->capacity)
  {
    count = ring->capacity;
  }
  if (count > capacity)
  {
    count = capacity;
  }

  for (offset = 0u; offset < count; offset++)
  {
    dst[offset] = ring->data[(tail + (uint32_t)offset) &
                             (ring->capacity - 1u)];
  }

  primask = sc_uart_enter_critical();
  ring->tail = tail + (uint32_t)count;
  sc_uart_exit_critical(primask);
  return count;
}

static sc_uart_status_t sc_uart_status_from_hal(HAL_StatusTypeDef status)
{
  if (status == HAL_BUSY)
  {
    return SC_UART_ERR_BUSY;
  }
  if (status != HAL_OK)
  {
    return SC_UART_ERR_HAL;
  }
  return SC_UART_OK;
}

static sc_uart_status_t sc_uart_arm_rx(sc_uart_context_t *ctx)
{
  HAL_StatusTypeDef hal_status;
  uint32_t primask;
  uint32_t retry_tick;

  primask = sc_uart_enter_critical();
  if (ctx->rx_armed)
  {
    sc_uart_exit_critical(primask);
    return SC_UART_OK;
  }
  ctx->rx_armed = true;
  sc_uart_exit_critical(primask);

  hal_status = HAL_UART_Receive_IT(ctx->hal, &ctx->rx_byte, 1u);
  if (hal_status == HAL_OK)
  {
    return SC_UART_OK;
  }

  retry_tick = HAL_GetTick() + SC_UART_RETRY_INTERVAL_MS;
  primask = sc_uart_enter_critical();
  ctx->rx_armed = false;
  ctx->stats.rx_rearm_failures++;
  ctx->stats.last_hal_error = (uint32_t)hal_status;
  ctx->next_rx_retry_tick = retry_tick;
  sc_uart_exit_critical(primask);
  return sc_uart_status_from_hal(hal_status);
}

static sc_uart_status_t sc_uart_start_tx(sc_uart_context_t *ctx)
{
  const uint8_t *segment;
  HAL_StatusTypeDef hal_status;
  uint32_t head;
  uint32_t tail;
  uint32_t primask;
  uint32_t retry_tick;
  size_t used;
  size_t contiguous;
  size_t chunk;

  primask = sc_uart_enter_critical();
  if (ctx->tx_active)
  {
    sc_uart_exit_critical(primask);
    return SC_UART_OK;
  }

  head = ctx->tx.head;
  tail = ctx->tx.tail;
  used = (size_t)(head - tail);
  if ((used == 0u) || (used > ctx->tx.capacity))
  {
    sc_uart_exit_critical(primask);
    return SC_UART_OK;
  }

  contiguous = ctx->tx.capacity -
               (size_t)(tail & (ctx->tx.capacity - 1u));
  chunk = (used < contiguous) ? used : contiguous;
  segment = &ctx->tx.data[tail & (ctx->tx.capacity - 1u)];
  ctx->tx_active = true;
  ctx->tx_inflight = (uint16_t)chunk;
  sc_uart_exit_critical(primask);

  hal_status = HAL_UART_Transmit_IT(ctx->hal, segment, (uint16_t)chunk);
  if (hal_status == HAL_OK)
  {
    return SC_UART_OK;
  }

  retry_tick = HAL_GetTick() + SC_UART_RETRY_INTERVAL_MS;
  primask = sc_uart_enter_critical();
  ctx->tx_active = false;
  ctx->tx_inflight = 0u;
  ctx->stats.tx_start_failures++;
  ctx->stats.last_hal_error = (uint32_t)hal_status;
  ctx->next_tx_retry_tick = retry_tick;
  sc_uart_exit_critical(primask);
  return sc_uart_status_from_hal(hal_status);
}

static void sc_uart_clear_rx_error_flags(UART_HandleTypeDef *huart)
{
  uint32_t status = huart->Instance->SR;

  /* STM32F1 的错误标志由 SR 后接 DR 的读序列清除。仅当错误仍存在时
     读 DR，避免 HAL 已清错后吞掉刚到达的有效字节。 */
  if ((status & (USART_SR_PE | USART_SR_FE | USART_SR_NE | USART_SR_ORE)) != 0u)
  {
    (void)huart->Instance->DR;
  }
}

static bool sc_uart_config_matches(const UART_HandleTypeDef *huart,
                                   const USART_TypeDef *instance,
                                   uint32_t baud)
{
  return (huart->Instance == instance) &&
         (huart->Init.BaudRate == baud) &&
         (huart->Init.WordLength == UART_WORDLENGTH_8B) &&
         (huart->Init.StopBits == UART_STOPBITS_1) &&
         (huart->Init.Parity == UART_PARITY_NONE) &&
         (huart->Init.Mode == UART_MODE_TX_RX) &&
         (huart->Init.HwFlowCtl == UART_HWCONTROL_NONE);
}

/* Exported functions --------------------------------------------------------*/
sc_uart_status_t sc_uart_init(sc_uart_port_t port, uint32_t baud)
{
  sc_uart_context_t *ctx = sc_uart_context_for_port(port);
  const USART_TypeDef *instance;
  uint32_t primask;

  if ((ctx == NULL) || (baud == 0u))
  {
    return SC_UART_ERR_ARGUMENT;
  }

  instance = (port == SC_UART_USART1) ? USART1 : USART2;
  if (!sc_uart_config_matches(ctx->hal, instance, baud))
  {
    return SC_UART_ERR_CONFIG;
  }

  if (ctx->initialized)
  {
    return (ctx->baud == baud) ? SC_UART_OK : SC_UART_ERR_CONFIG;
  }

  if (ctx->hal->gState != HAL_UART_STATE_READY)
  {
    return ((ctx->hal->gState == HAL_UART_STATE_BUSY_TX) ||
            (ctx->hal->gState == HAL_UART_STATE_BUSY_TX_RX))
             ? SC_UART_ERR_BUSY
             : SC_UART_ERR_NOT_READY;
  }
  if (ctx->hal->RxState != HAL_UART_STATE_READY)
  {
    return ((ctx->hal->RxState == HAL_UART_STATE_BUSY_RX) ||
            (ctx->hal->RxState == HAL_UART_STATE_BUSY_TX_RX))
             ? SC_UART_ERR_BUSY
             : SC_UART_ERR_NOT_READY;
  }

  ctx->hal->ErrorCode = HAL_UART_ERROR_NONE;
  sc_uart_clear_rx_error_flags(ctx->hal);

  primask = sc_uart_enter_critical();
  ctx->rx.head = 0u;
  ctx->rx.tail = 0u;
  ctx->tx.head = 0u;
  ctx->tx.tail = 0u;
  ctx->stats = (sc_uart_stats_t){0};
  ctx->rx_armed = false;
  ctx->tx_active = false;
  ctx->tx_inflight = 0u;
  ctx->next_rx_retry_tick = 0u;
  ctx->next_tx_retry_tick = 0u;
  ctx->baud = baud;
  ctx->initialized = true;
  sc_uart_exit_critical(primask);

  {
    sc_uart_status_t status = sc_uart_arm_rx(ctx);
    if (status != SC_UART_OK)
    {
      ctx->initialized = false;
      return status;
    }
  }

  return SC_UART_OK;
}

sc_uart_status_t sc_uart_write(sc_uart_port_t port, const char *text)
{
  sc_uart_context_t *ctx = sc_uart_context_for_port(port);
  size_t len;
  uint32_t primask;

  if ((ctx == NULL) || (text == NULL))
  {
    return SC_UART_ERR_ARGUMENT;
  }
  if (!ctx->initialized)
  {
    return SC_UART_ERR_NOT_READY;
  }

  for (len = 0u; len <= ctx->tx.capacity; len++)
  {
    if (text[len] == '\0')
    {
      break;
    }
  }

  if (len > ctx->tx.capacity)
  {
    primask = sc_uart_enter_critical();
    ctx->stats.tx_rejected_writes++;
    sc_uart_exit_critical(primask);
    return SC_UART_ERR_BUFFER_FULL;
  }

  return sc_uart_write_bytes(port, (const uint8_t *)text, len);
}

sc_uart_status_t sc_uart_write_bytes(sc_uart_port_t port,
                                     const uint8_t *data, size_t len)
{
  sc_uart_context_t *ctx = sc_uart_context_for_port(port);
  sc_uart_status_t status;
  uint32_t old_head;
  uint32_t primask;

  if ((ctx == NULL) || ((data == NULL) && (len != 0u)))
  {
    return SC_UART_ERR_ARGUMENT;
  }
  if (!ctx->initialized)
  {
    return SC_UART_ERR_NOT_READY;
  }
  if (len == 0u)
  {
    return SC_UART_OK;
  }

  primask = sc_uart_enter_critical();
  old_head = ctx->tx.head;
  if (sc_uart_ring_free(&ctx->tx) < len)
  {
    ctx->stats.tx_rejected_writes++;
    sc_uart_exit_critical(primask);
    return SC_UART_ERR_BUFFER_FULL;
  }
  sc_uart_exit_critical(primask);

  sc_uart_ring_copy_in(&ctx->tx, old_head, data, len);

  primask = sc_uart_enter_critical();
  ctx->tx.head = old_head + (uint32_t)len;
  sc_uart_exit_critical(primask);

  status = sc_uart_start_tx(ctx);
  if (status != SC_UART_OK)
  {
    primask = sc_uart_enter_critical();
    ctx->tx.head = old_head;
    ctx->stats.tx_rejected_writes++;
    sc_uart_exit_critical(primask);
  }
  return status;
}

sc_uart_status_t sc_uart_read(sc_uart_port_t port, uint8_t *dst,
                              size_t capacity, size_t *received)
{
  sc_uart_context_t *ctx = sc_uart_context_for_port(port);

  if ((ctx == NULL) || (received == NULL) ||
      ((dst == NULL) && (capacity != 0u)))
  {
    return SC_UART_ERR_ARGUMENT;
  }
  if (!ctx->initialized)
  {
    return SC_UART_ERR_NOT_READY;
  }

  *received = sc_uart_ring_pop(&ctx->rx, dst, capacity);
  return SC_UART_OK;
}

sc_uart_status_t sc_uart_get_stats(sc_uart_port_t port,
                                   sc_uart_stats_t *stats)
{
  sc_uart_context_t *ctx = sc_uart_context_for_port(port);
  uint32_t primask;

  if ((ctx == NULL) || (stats == NULL))
  {
    return SC_UART_ERR_ARGUMENT;
  }
  if (!ctx->initialized)
  {
    return SC_UART_ERR_NOT_READY;
  }

  primask = sc_uart_enter_critical();
  *stats = ctx->stats;
  sc_uart_exit_critical(primask);
  return SC_UART_OK;
}

void sc_uart_service(void)
{
  sc_uart_context_t *ctx;
  uint32_t now = HAL_GetTick();
  uint32_t index;

  for (index = SC_UART_USART1; index <= SC_UART_USART2; index++)
  {
    ctx = &s_contexts[index];
    if (!ctx->initialized)
    {
      continue;
    }

    if (!ctx->rx_armed &&
        (ctx->hal->RxState == HAL_UART_STATE_READY) &&
        ((int32_t)(now - ctx->next_rx_retry_tick) >= 0))
    {
      (void)sc_uart_arm_rx(ctx);
    }

    if (!ctx->tx_active && (sc_uart_ring_used(&ctx->tx) != 0u) &&
        ((int32_t)(now - ctx->next_tx_retry_tick) >= 0))
    {
      (void)sc_uart_start_tx(ctx);
    }
  }
}

/* HAL callbacks -------------------------------------------------------------*/
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  sc_uart_context_t *ctx = sc_uart_context_for_handle(huart);
  uint32_t primask;
  uint32_t used;
  uint32_t advanced;

  if ((ctx == NULL) || !ctx->initialized)
  {
    return;
  }

  primask = sc_uart_enter_critical();
  used = ctx->tx.head - ctx->tx.tail;
  advanced = ctx->tx_inflight;
  if ((used > ctx->tx.capacity) || (advanced > used))
  {
    advanced = (used > ctx->tx.capacity) ? 0u : used;
  }
  ctx->tx.tail += advanced;
  ctx->tx_active = false;
  ctx->tx_inflight = 0u;
  sc_uart_exit_critical(primask);

  if (sc_uart_ring_used(&ctx->tx) != 0u)
  {
    (void)sc_uart_start_tx(ctx);
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  sc_uart_context_t *ctx = sc_uart_context_for_handle(huart);
  uint32_t primask;

  if ((ctx == NULL) || !ctx->initialized)
  {
    return;
  }

  primask = sc_uart_enter_critical();
  ctx->rx_armed = false;
  sc_uart_exit_critical(primask);

  if (huart->ErrorCode != HAL_UART_ERROR_NONE)
  {
    ctx->stats.rx_error_bytes++;
    ctx->stats.last_hal_error = huart->ErrorCode;
    return;
  }

  sc_uart_ring_push(&ctx->rx, ctx->rx_byte, &ctx->stats);
  (void)sc_uart_arm_rx(ctx);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  sc_uart_context_t *ctx;
  uint32_t error;
  uint32_t primask;

  if (huart == NULL)
  {
    return;
  }

  ctx = sc_uart_context_for_handle(huart);
  error = huart->ErrorCode;
  if ((ctx == NULL) || !ctx->initialized)
  {
    return;
  }

  if ((error & HAL_UART_ERROR_ORE) != 0u)
  {
    ctx->stats.rx_ore_events++;
  }
  if ((error & HAL_UART_ERROR_FE) != 0u)
  {
    ctx->stats.rx_fe_events++;
  }
  if ((error & HAL_UART_ERROR_NE) != 0u)
  {
    ctx->stats.rx_ne_events++;
  }
  if ((error & HAL_UART_ERROR_PE) != 0u)
  {
    ctx->stats.rx_pe_events++;
  }
  ctx->stats.last_hal_error = error;

  sc_uart_clear_rx_error_flags(huart);

  if (huart->RxState == HAL_UART_STATE_READY)
  {
    primask = sc_uart_enter_critical();
    ctx->rx_armed = false;
    sc_uart_exit_critical(primask);
    (void)sc_uart_arm_rx(ctx);
  }
}
