/**
  ******************************************************************************
  * @file    sc_i2c2.h
  * @brief   I2C2 共享总线协调层。
  ******************************************************************************
  */

#ifndef __SC_I2C2_H
#define __SC_I2C2_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
  SC_I2C2_OK = 0,
  SC_I2C2_ERR_ARGUMENT,
  SC_I2C2_ERR_CONFIG,
  SC_I2C2_ERR_NOT_READY,
  SC_I2C2_ERR_BUSY,
  SC_I2C2_ERR_TIMEOUT,
  SC_I2C2_ERR_BUS
} sc_i2c2_status_t;

typedef void (*sc_i2c2_complete_cb_t)(sc_i2c2_status_t status,
                                      void *context);

/**
  * @brief  校验 CubeMX 已初始化的 hi2c2，可重复调用。
  */
sc_i2c2_status_t sc_i2c2_init(void);

/**
  * @brief  阻塞探测 7 位设备地址，仅用于启动期初始化。
  */
sc_i2c2_status_t sc_i2c2_is_device_ready(uint16_t address,
                                         uint32_t trials,
                                         uint32_t timeout_ms);

/**
  * @brief  启动期阻塞寄存器写。
  *         短暂的硬件 BUSY 由该 HAL 阻塞 API 在 timeout 内等待清除。
  */
sc_i2c2_status_t sc_i2c2_mem_write_blocking(uint16_t address,
                                            uint16_t mem_address,
                                            uint16_t mem_address_size,
                                            const uint8_t *data,
                                            uint16_t len,
                                            uint32_t timeout_ms);

/**
  * @brief  启动期阻塞寄存器读。
  *         短暂的硬件 BUSY 由该 HAL 阻塞 API 在 timeout 内等待清除。
  */
sc_i2c2_status_t sc_i2c2_mem_read_blocking(uint16_t address,
                                           uint16_t mem_address,
                                           uint16_t mem_address_size,
                                           uint8_t *dst,
                                           uint16_t len,
                                           uint32_t timeout_ms);

/**
  * @brief  启动期阻塞主发送。
  */
sc_i2c2_status_t sc_i2c2_master_transmit_blocking(uint16_t address,
                                                  const uint8_t *data,
                                                  uint16_t len,
                                                  uint32_t timeout_ms);

/**
  * @brief  启动异步主发送；同一时间只允许一笔事务。
  *         调用 HAL_IT 前同时检查 HAL State、协调层在途状态和硬件 BUSY。
  *         成功后回调在 I2C 中断上下文执行，回调内不得启动新事务。
  */
sc_i2c2_status_t sc_i2c2_master_transmit_async(
  uint16_t address,
  uint8_t *data,
  uint16_t len,
  sc_i2c2_complete_cb_t callback,
  void *context);

/**
  * @brief  启动异步寄存器连续读；同一时间只允许一笔事务。
  *         调用 HAL_IT 前同时检查 HAL State、协调层在途状态和硬件 BUSY。
  */
sc_i2c2_status_t sc_i2c2_mem_read_async(
  uint16_t address,
  uint16_t mem_address,
  uint16_t mem_address_size,
  uint8_t *dst,
  uint16_t len,
  sc_i2c2_complete_cb_t callback,
  void *context);

/**
  * @brief  查询协调层是否有在途事务。
  */
uint8_t sc_i2c2_is_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* __SC_I2C2_H */
