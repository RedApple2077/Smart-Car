/**
  ******************************************************************************
  * @file    sc_uart.h
  * @brief   HC-05/USART2 非阻塞收发驱动。
  *          PA2/PA3, 9600 8N1。
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __SC_UART_H
#define __SC_UART_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stddef.h>
#include <stdint.h>

/* Exported types ------------------------------------------------------------*/
typedef enum {
  SC_UART_OK = 0,
  SC_UART_ERR_ARGUMENT,
  SC_UART_ERR_CONFIG,
  SC_UART_ERR_NOT_READY,
  SC_UART_ERR_BUSY,
  SC_UART_ERR_BUFFER_FULL,
  SC_UART_ERR_HAL
} sc_uart_status_t;

typedef struct {
  uint32_t rx_overflow_bytes;
  uint32_t rx_ore_events;
  uint32_t rx_fe_events;
  uint32_t rx_ne_events;
  uint32_t rx_pe_events;
  uint32_t rx_error_bytes;
  uint32_t rx_rearm_failures;
  uint32_t tx_start_failures;
  uint32_t tx_rejected_writes;
  uint32_t last_hal_error;
} sc_uart_stats_t;

/* Exported functions prototypes ---------------------------------------------*/
/**
  * @brief  校验并启用 CubeMX 已初始化的 HC-05 UART。
  *         只校验 Instance、波特率、8N1、TX/RX 和无流控配置，不重复调用
  *         HAL_UART_Init()。rx_enable=0 时不挂接接收中断；rx_enable=1 时
  *         启用单字节中断接收。正式固件传 0，HC-05 Bench 固件传 1。
  */
sc_uart_status_t sc_uart_init(uint32_t baud, uint8_t rx_enable);

/**
  * @brief  将零结尾字符串整次写入 TX 队列；不发送 '\0'、不添加换行。
  *         成功仅表示全部字节已入队，不表示远端已收到。
  */
sc_uart_status_t sc_uart_write(const char *text);

/**
  * @brief  将指定字节整次写入 TX 队列；空间不足时整次拒绝。
  *         成功仅表示全部字节已入队，不表示远端已收到。
  */
sc_uart_status_t sc_uart_write_bytes(const uint8_t *data, size_t len);

/**
  * @brief  非阻塞读取当前 RX 队列，返回不超过 capacity 的已有字节。
  *         仅在初始化时 rx_enable=1 的端口上可用。
  */
sc_uart_status_t sc_uart_read(uint8_t *dst, size_t capacity,
                              size_t *received);

/**
  * @brief  获取 HC-05 UART 的统计快照。
  */
sc_uart_status_t sc_uart_get_stats(sc_uart_stats_t *stats);

/**
  * @brief  主循环服务函数，限频重试 RX 重挂和待发送队列。
  *         公共读写及 service 须仅在主循环上下文调用；HAL UART 完成和
  *         错误回调由 sc_uart.c 内部实现与处理。
  */
void sc_uart_service(void);

#ifdef __cplusplus
}
#endif

#endif /* __SC_UART_H */
