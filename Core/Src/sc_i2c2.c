/**
  ******************************************************************************
  * @file    sc_i2c2.c
  * @brief   I2C2 共享总线协调层。
  ******************************************************************************
  */

#include "sc_i2c2.h"

#include <stddef.h>

#include "main.h"

extern I2C_HandleTypeDef hi2c2;

typedef enum {
  SC_I2C2_OP_NONE = 0,
  SC_I2C2_OP_MASTER_TX,
  SC_I2C2_OP_MEM_READ
} sc_i2c2_operation_t;

static uint8_t s_initialized;
static volatile uint8_t s_inflight;
static volatile sc_i2c2_operation_t s_operation;
static sc_i2c2_complete_cb_t s_callback;
static void *s_callback_context;

static uint32_t sc_i2c2_enter_critical(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

static void sc_i2c2_exit_critical(uint32_t primask)
{
  if (primask == 0u)
  {
    __enable_irq();
  }
}

static sc_i2c2_status_t sc_i2c2_validate_config(void)
{
  if (hi2c2.Instance != I2C2)
  {
    return SC_I2C2_ERR_CONFIG;
  }
  if ((hi2c2.Init.ClockSpeed != 100000u) ||
      (hi2c2.Init.AddressingMode != I2C_ADDRESSINGMODE_7BIT))
  {
    return SC_I2C2_ERR_CONFIG;
  }
  return SC_I2C2_OK;
}

static sc_i2c2_status_t sc_i2c2_map_hal(HAL_StatusTypeDef status)
{
  switch (status)
  {
    case HAL_OK:
      return SC_I2C2_OK;
    case HAL_BUSY:
      return SC_I2C2_ERR_BUSY;
    case HAL_TIMEOUT:
      return SC_I2C2_ERR_TIMEOUT;
    case HAL_ERROR:
    default:
      return SC_I2C2_ERR_BUS;
  }
}

static sc_i2c2_status_t sc_i2c2_check_common_ready(void)
{
  sc_i2c2_status_t status;

  if (s_initialized == 0u)
  {
    return SC_I2C2_ERR_NOT_READY;
  }

  status = sc_i2c2_validate_config();
  if (status != SC_I2C2_OK)
  {
    return status;
  }
  if (hi2c2.State != HAL_I2C_STATE_READY)
  {
    if (((uint32_t)hi2c2.State & 0x0Fu) != 0u)
    {
      return SC_I2C2_ERR_BUSY;
    }
    return SC_I2C2_ERR_NOT_READY;
  }
  if (s_inflight != 0u)
  {
    return SC_I2C2_ERR_BUSY;
  }
  return SC_I2C2_OK;
}

static void sc_i2c2_clear_transaction(void)
{
  s_callback = NULL;
  s_callback_context = NULL;
  s_operation = SC_I2C2_OP_NONE;
  s_inflight = 0u;
}

static void sc_i2c2_complete(sc_i2c2_status_t status,
                             sc_i2c2_operation_t expected_operation)
{
  sc_i2c2_complete_cb_t callback = NULL;
  void *context = NULL;

  if ((s_inflight == 0u) || (s_operation != expected_operation))
  {
    return;
  }

  callback = s_callback;
  context = s_callback_context;
  sc_i2c2_clear_transaction();

  if (callback != NULL)
  {
    callback(status, context);
  }
}

sc_i2c2_status_t sc_i2c2_init(void)
{
  sc_i2c2_status_t status = sc_i2c2_validate_config();

  if (status != SC_I2C2_OK)
  {
    return status;
  }
  if (hi2c2.State != HAL_I2C_STATE_READY)
  {
    return SC_I2C2_ERR_NOT_READY;
  }

  s_initialized = 1u;
  return SC_I2C2_OK;
}

sc_i2c2_status_t sc_i2c2_is_device_ready(uint16_t address,
                                         uint32_t trials,
                                         uint32_t timeout_ms)
{
  sc_i2c2_status_t status;
  HAL_StatusTypeDef hal_status;

  if ((address > 0x7Fu) || (trials == 0u) || (timeout_ms == 0u))
  {
    return SC_I2C2_ERR_ARGUMENT;
  }

  status = sc_i2c2_check_common_ready();
  if (status != SC_I2C2_OK)
  {
    return status;
  }

  hal_status = HAL_I2C_IsDeviceReady(&hi2c2, (uint16_t)(address << 1u),
                                      trials, timeout_ms);
  return sc_i2c2_map_hal(hal_status);
}

sc_i2c2_status_t sc_i2c2_mem_write_blocking(uint16_t address,
                                            uint16_t mem_address,
                                            uint16_t mem_address_size,
                                            const uint8_t *data,
                                            uint16_t len,
                                            uint32_t timeout_ms)
{
  sc_i2c2_status_t status;
  HAL_StatusTypeDef hal_status;

  if ((address > 0x7Fu) || (data == NULL) || (len == 0u) ||
      (timeout_ms == 0u) ||
      ((mem_address_size != I2C_MEMADD_SIZE_8BIT) &&
       (mem_address_size != I2C_MEMADD_SIZE_16BIT)))
  {
    return SC_I2C2_ERR_ARGUMENT;
  }

  status = sc_i2c2_check_common_ready();
  if (status != SC_I2C2_OK)
  {
    return status;
  }

  hal_status = HAL_I2C_Mem_Write(&hi2c2, (uint16_t)(address << 1u),
                                 mem_address, mem_address_size,
                                 (uint8_t *)data, len, timeout_ms);
  return sc_i2c2_map_hal(hal_status);
}

sc_i2c2_status_t sc_i2c2_mem_read_blocking(uint16_t address,
                                           uint16_t mem_address,
                                           uint16_t mem_address_size,
                                           uint8_t *dst,
                                           uint16_t len,
                                           uint32_t timeout_ms)
{
  sc_i2c2_status_t status;
  HAL_StatusTypeDef hal_status;

  if ((address > 0x7Fu) || (dst == NULL) || (len == 0u) ||
      (timeout_ms == 0u) ||
      ((mem_address_size != I2C_MEMADD_SIZE_8BIT) &&
       (mem_address_size != I2C_MEMADD_SIZE_16BIT)))
  {
    return SC_I2C2_ERR_ARGUMENT;
  }

  status = sc_i2c2_check_common_ready();
  if (status != SC_I2C2_OK)
  {
    return status;
  }

  hal_status = HAL_I2C_Mem_Read(&hi2c2, (uint16_t)(address << 1u),
                                mem_address, mem_address_size,
                                dst, len, timeout_ms);
  return sc_i2c2_map_hal(hal_status);
}

sc_i2c2_status_t sc_i2c2_master_transmit_blocking(uint16_t address,
                                                  const uint8_t *data,
                                                  uint16_t len,
                                                  uint32_t timeout_ms)
{
  sc_i2c2_status_t status;
  HAL_StatusTypeDef hal_status;

  if ((address > 0x7Fu) || (data == NULL) || (len == 0u) ||
      (timeout_ms == 0u))
  {
    return SC_I2C2_ERR_ARGUMENT;
  }

  status = sc_i2c2_check_common_ready();
  if (status != SC_I2C2_OK)
  {
    return status;
  }

  hal_status = HAL_I2C_Master_Transmit(&hi2c2, (uint16_t)(address << 1u),
                                       (uint8_t *)data, len, timeout_ms);
  return sc_i2c2_map_hal(hal_status);
}

static sc_i2c2_status_t sc_i2c2_begin_async(
  sc_i2c2_operation_t operation,
  sc_i2c2_complete_cb_t callback,
  void *context)
{
  uint32_t primask;
  sc_i2c2_status_t status;

  if (callback == NULL)
  {
    return SC_I2C2_ERR_ARGUMENT;
  }

  status = sc_i2c2_check_common_ready();
  if (status != SC_I2C2_OK)
  {
    return status;
  }

  primask = sc_i2c2_enter_critical();
  if ((s_inflight != 0u) ||
      (__HAL_I2C_GET_FLAG(&hi2c2, I2C_FLAG_BUSY) != RESET))
  {
    sc_i2c2_exit_critical(primask);
    return SC_I2C2_ERR_BUSY;
  }
  s_callback = callback;
  s_callback_context = context;
  s_operation = operation;
  s_inflight = 1u;
  sc_i2c2_exit_critical(primask);
  return SC_I2C2_OK;
}

sc_i2c2_status_t sc_i2c2_master_transmit_async(
  uint16_t address,
  uint8_t *data,
  uint16_t len,
  sc_i2c2_complete_cb_t callback,
  void *context)
{
  sc_i2c2_status_t status;
  HAL_StatusTypeDef hal_status;

  if ((address > 0x7Fu) || (data == NULL) || (len == 0u) ||
      (callback == NULL))
  {
    return SC_I2C2_ERR_ARGUMENT;
  }
  if (s_initialized == 0u)
  {
    return SC_I2C2_ERR_NOT_READY;
  }

  status = sc_i2c2_begin_async(SC_I2C2_OP_MASTER_TX, callback, context);
  if (status != SC_I2C2_OK)
  {
    return status;
  }

  hal_status = HAL_I2C_Master_Transmit_IT(&hi2c2,
                                          (uint16_t)(address << 1u),
                                          data, len);
  if (hal_status != HAL_OK)
  {
    uint32_t primask = sc_i2c2_enter_critical();
    if ((s_inflight != 0u) && (s_operation == SC_I2C2_OP_MASTER_TX))
    {
      sc_i2c2_clear_transaction();
    }
    sc_i2c2_exit_critical(primask);
    return sc_i2c2_map_hal(hal_status);
  }
  return SC_I2C2_OK;
}

sc_i2c2_status_t sc_i2c2_mem_read_async(
  uint16_t address,
  uint16_t mem_address,
  uint16_t mem_address_size,
  uint8_t *dst,
  uint16_t len,
  sc_i2c2_complete_cb_t callback,
  void *context)
{
  sc_i2c2_status_t status;
  HAL_StatusTypeDef hal_status;

  if ((address > 0x7Fu) || (dst == NULL) || (len == 0u) ||
      (callback == NULL) ||
      ((mem_address_size != I2C_MEMADD_SIZE_8BIT) &&
       (mem_address_size != I2C_MEMADD_SIZE_16BIT)))
  {
    return SC_I2C2_ERR_ARGUMENT;
  }
  if (s_initialized == 0u)
  {
    return SC_I2C2_ERR_NOT_READY;
  }

  status = sc_i2c2_begin_async(SC_I2C2_OP_MEM_READ, callback, context);
  if (status != SC_I2C2_OK)
  {
    return status;
  }

  hal_status = HAL_I2C_Mem_Read_IT(&hi2c2,
                                   (uint16_t)(address << 1u),
                                   mem_address,
                                   mem_address_size,
                                   dst, len);
  if (hal_status != HAL_OK)
  {
    uint32_t primask = sc_i2c2_enter_critical();
    if ((s_inflight != 0u) && (s_operation == SC_I2C2_OP_MEM_READ))
    {
      sc_i2c2_clear_transaction();
    }
    sc_i2c2_exit_critical(primask);
    return sc_i2c2_map_hal(hal_status);
  }
  return SC_I2C2_OK;
}

uint8_t sc_i2c2_is_busy(void)
{
  return s_inflight;
}

void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
  if (hi2c == &hi2c2)
  {
    sc_i2c2_complete(SC_I2C2_OK, SC_I2C2_OP_MASTER_TX);
  }
}

void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
  if (hi2c == &hi2c2)
  {
    sc_i2c2_complete(SC_I2C2_OK, SC_I2C2_OP_MEM_READ);
  }
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
  sc_i2c2_status_t status = SC_I2C2_ERR_BUS;

  if (hi2c != &hi2c2)
  {
    return;
  }

  if ((hi2c->ErrorCode & HAL_I2C_ERROR_TIMEOUT) != 0u)
  {
    status = SC_I2C2_ERR_TIMEOUT;
  }
  sc_i2c2_complete(status, s_operation);
}
