/**
  ******************************************************************************
  * @file    sc_telemetry.h
  * @brief   正式固件 HC-05 遥测、OLED 状态页与 LED 指示。
  ******************************************************************************
  */

#ifndef __SC_TELEMETRY_H
#define __SC_TELEMETRY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "sc_app.h"

#define SC_TELEMETRY_UART_PERIOD_MS 200u
#define SC_TELEMETRY_OLED_PERIOD_MS 500u

typedef enum {
  SC_TELEMETRY_OK = 0,
  SC_TELEMETRY_ERR_ARGUMENT,
  SC_TELEMETRY_ERR_UART,
  SC_TELEMETRY_ERR_DISPLAY
} sc_telemetry_status_t;

void sc_telemetry_init(uint32_t now_ms);

/**
  * @brief  推进 LED、OLED 和 HC-05 遥测。
  *         degraded_bits_out 返回本次新增的 TELEMETRY/DISPLAY 降级位。
  */
sc_telemetry_status_t sc_telemetry_service(
  const sc_app_snapshot_t *snapshot, uint32_t now_ms,
  uint32_t *degraded_bits_out);

#ifdef __cplusplus
}
#endif

#endif /* __SC_TELEMETRY_H */
