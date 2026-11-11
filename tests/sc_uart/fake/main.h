#ifndef TEST_FAKE_MAIN_H
#define TEST_FAKE_MAIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  HAL_OK = 0,
  HAL_ERROR = 1,
  HAL_BUSY = 2,
  HAL_TIMEOUT = 3
} HAL_StatusTypeDef;

typedef enum {
  HAL_UART_STATE_RESET = 0x00u,
  HAL_UART_STATE_READY = 0x20u,
  HAL_UART_STATE_BUSY_TX = 0x21u,
  HAL_UART_STATE_BUSY_RX = 0x22u,
  HAL_UART_STATE_BUSY_TX_RX = 0x23u,
  HAL_UART_STATE_TIMEOUT = 0xA0u,
  HAL_UART_STATE_ERROR = 0xE0u
} HAL_UART_StateTypeDef;

typedef struct {
  volatile uint32_t SR;
  volatile uint32_t DR;
  volatile uint32_t CR1;
  volatile uint32_t CR3;
} USART_TypeDef;

typedef struct {
  uint32_t BaudRate;
  uint32_t WordLength;
  uint32_t StopBits;
  uint32_t Parity;
  uint32_t Mode;
  uint32_t HwFlowCtl;
  uint32_t OverSampling;
} UART_InitTypeDef;

typedef struct {
  USART_TypeDef *Instance;
  UART_InitTypeDef Init;
  volatile HAL_UART_StateTypeDef gState;
  volatile HAL_UART_StateTypeDef RxState;
  volatile uint32_t ErrorCode;
  const uint8_t *pTxBuffPtr;
  uint16_t TxXferSize;
  volatile uint16_t TxXferCount;
  uint8_t *pRxBuffPtr;
  uint16_t RxXferSize;
  volatile uint16_t RxXferCount;
} UART_HandleTypeDef;

#define UART_WORDLENGTH_8B 0x0000u
#define UART_WORDLENGTH_9B 0x1000u
#define UART_STOPBITS_1 0x0000u
#define UART_STOPBITS_2 0x2000u
#define UART_PARITY_NONE 0x0000u
#define UART_PARITY_EVEN 0x0400u
#define UART_PARITY_ODD 0x0600u
#define UART_MODE_RX 0x0004u
#define UART_MODE_TX 0x0008u
#define UART_MODE_TX_RX (UART_MODE_RX | UART_MODE_TX)
#define UART_HWCONTROL_NONE 0x0000u
#define UART_OVERSAMPLING_16 0x0000u

#define USART_SR_PE 0x0001u
#define USART_SR_FE 0x0002u
#define USART_SR_NE 0x0004u
#define USART_SR_ORE 0x0008u
#define USART_SR_RXNE 0x0020u
#define USART_SR_TC 0x0040u
#define USART_SR_TXE 0x0080u

#define HAL_UART_ERROR_NONE 0x00000000u
#define HAL_UART_ERROR_PE 0x00000001u
#define HAL_UART_ERROR_NE 0x00000002u
#define HAL_UART_ERROR_FE 0x00000004u
#define HAL_UART_ERROR_ORE 0x00000008u

extern USART_TypeDef fake_usart1_regs;
extern USART_TypeDef fake_usart2_regs;

#define USART1 (&fake_usart1_regs)
#define USART2 (&fake_usart2_regs)

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;

static inline uint32_t __get_PRIMASK(void)
{
  return 0u;
}

static inline void __disable_irq(void)
{
}

static inline void __enable_irq(void)
{
}

uint32_t HAL_GetTick(void);
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *huart,
                                       const uint8_t *pData, uint16_t Size);
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *huart,
                                      uint8_t *pData, uint16_t Size);
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart);
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart);

void fake_hal_reset(void);
void fake_set_tick(uint32_t tick);
void fake_fail_next_transmit(HAL_StatusTypeDef status);
void fake_fail_next_receive(HAL_StatusTypeDef status);
uint32_t fake_transmit_call_count(const UART_HandleTypeDef *huart);
uint32_t fake_receive_call_count(const UART_HandleTypeDef *huart);
void fake_complete_transmit(UART_HandleTypeDef *huart);
void fake_deliver_receive(UART_HandleTypeDef *huart, uint8_t byte);
void fake_deliver_receive_error_with_complete(UART_HandleTypeDef *huart,
                                              uint8_t byte,
                                              uint32_t error_code);
void fake_deliver_error_without_rxne(UART_HandleTypeDef *huart,
                                     uint32_t error_code);
size_t fake_copy_transmitted(const UART_HandleTypeDef *huart, uint8_t *dst,
                             size_t capacity);

#endif /* TEST_FAKE_MAIN_H */
