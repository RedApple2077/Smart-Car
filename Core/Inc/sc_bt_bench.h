/**
  ******************************************************************************
  * @file    sc_bt_bench.h
  * @brief   HC-05 串口台架命令层。
  *          仅在 SC_BENCH_BLUETOOTH 固件变体中启用。
  ******************************************************************************
  */

#ifndef __SC_BT_BENCH_H
#define __SC_BT_BENCH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define SC_BT_BENCH_MAX_LINE_BYTES 48u
#define SC_BT_BENCH_ARM_WINDOW_MS  5000u

/**
  * @brief  初始化台架命令层并确保 TB6612 处于待机安全状态。
  *         必须在 sc_uart、sc_tb6612 和 sc_encoder 初始化后调用。
  */
void sc_bt_bench_init(uint32_t now_ms);

/**
  * @brief  主循环服务：处理 USART2 非阻塞输入、超时、心跳和串口错误。
  *         只能从主循环上下文调用。
  */
void sc_bt_bench_service(uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* __SC_BT_BENCH_H */
