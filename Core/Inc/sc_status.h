/**
  ******************************************************************************
  * @file    sc_status.h
  * @brief   跨层公共状态码，对应《循迹小车硬件接线与软件API规划》7.2。
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __SC_STATUS_H
#define __SC_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Exported types ------------------------------------------------------------*/
typedef enum {
  SC_OK = 0,
  SC_ERR_ARGUMENT,
  SC_ERR_NOT_READY,
  SC_ERR_TIMEOUT,
  SC_ERR_BUS,
  SC_ERR_INVALID_DATA,
  SC_ERR_CALIBRATION_REQUIRED,
  SC_ERR_HARDWARE,
  SC_ERR_BUFFER_FULL,
  SC_ERR_NOT_SUPPORTED
} sc_status_t;

#ifdef __cplusplus
}
#endif

#endif /* __SC_STATUS_H */
