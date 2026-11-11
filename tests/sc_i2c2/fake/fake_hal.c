#include "main.h"

#include <string.h>

#define FAKE_DATA_CAPACITY 64u
#define FAKE_MPU_REGISTER_COUNT 128u

I2C_TypeDef fake_i2c2_regs;
I2C_HandleTypeDef hi2c2;

static bool s_bus_busy;
static bool s_async_active;
static fake_async_kind_t s_async_kind;
static uint16_t s_async_address;
static uint16_t s_async_mem_address;
static uint16_t s_async_size;
static uint8_t *s_async_data;
static uint8_t s_last_async_data[FAKE_DATA_CAPACITY];
static size_t s_last_async_data_len;
static uint8_t s_last_blocking_data[FAKE_DATA_CAPACITY];
static size_t s_last_blocking_data_len;
static uint16_t s_last_blocking_address;
static HAL_StatusTypeDef s_next_blocking_failure;
static HAL_StatusTypeDef s_next_ready_failure;
static HAL_StatusTypeDef s_next_async_failure;
static uint32_t s_hal_call_count;
static uint32_t s_async_start_count;
static uint32_t s_blocking_busy_wait_count;
static uint32_t s_tick;
static uint8_t s_mpu_regs[FAKE_MPU_REGISTER_COUNT];
static bool s_next_mem_read_leaves_busy;

static void fake_record_blocking(uint16_t address,
                                 const uint8_t *data,
                                 size_t len)
{
  size_t copy_len = len;

  s_last_blocking_address = address;
  if (data == NULL)
  {
    s_last_blocking_data_len = 0u;
    return;
  }
  if (copy_len > sizeof(s_last_blocking_data))
  {
    copy_len = sizeof(s_last_blocking_data);
  }
  memcpy(s_last_blocking_data, data, copy_len);
  s_last_blocking_data_len = copy_len;
}

static HAL_StatusTypeDef fake_consume_blocking_failure(void)
{
  HAL_StatusTypeDef status = s_next_blocking_failure;
  s_next_blocking_failure = HAL_OK;
  return status;
}

static void fake_wait_for_blocking_busy(void)
{
  if (s_bus_busy)
  {
    s_bus_busy = false;
    s_blocking_busy_wait_count++;
  }
}

void fake_hal_reset(void)
{
  memset(&hi2c2, 0, sizeof(hi2c2));
  hi2c2.Instance = I2C2;
  hi2c2.Init.ClockSpeed = 100000u;
  hi2c2.Init.DutyCycle = 0u;
  hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c2.State = HAL_I2C_STATE_READY;
  hi2c2.Mode = HAL_I2C_MODE_NONE;
  hi2c2.ErrorCode = HAL_I2C_ERROR_NONE;

  s_bus_busy = false;
  s_async_active = false;
  s_async_kind = FAKE_ASYNC_NONE;
  s_async_address = 0u;
  s_async_mem_address = 0u;
  s_async_size = 0u;
  s_async_data = NULL;
  memset(s_last_async_data, 0, sizeof(s_last_async_data));
  s_last_async_data_len = 0u;
  memset(s_last_blocking_data, 0, sizeof(s_last_blocking_data));
  s_last_blocking_data_len = 0u;
  s_last_blocking_address = 0u;
  s_next_blocking_failure = HAL_OK;
  s_next_ready_failure = HAL_OK;
  s_next_async_failure = HAL_OK;
  s_hal_call_count = 0u;
  s_async_start_count = 0u;
  s_blocking_busy_wait_count = 0u;
  s_tick = 0u;
  s_next_mem_read_leaves_busy = false;
  memset(s_mpu_regs, 0, sizeof(s_mpu_regs));
  s_mpu_regs[0x75u] = 0x68u;
}

void fake_set_tick(uint32_t tick)
{
  s_tick = tick;
}

uint32_t HAL_GetTick(void)
{
  return s_tick;
}

void HAL_Delay(uint32_t delay_ms)
{
  (void)delay_ms;
}

void fake_set_i2c_state(HAL_I2C_StateTypeDef state)
{
  hi2c2.State = state;
}

void fake_set_i2c_bus_busy(bool busy)
{
  s_bus_busy = busy;
}

void fake_set_next_mem_read_leaves_busy(bool leaves_busy)
{
  s_next_mem_read_leaves_busy = leaves_busy;
}

void fake_fail_next_blocking(HAL_StatusTypeDef status)
{
  s_next_blocking_failure = status;
}

void fake_fail_next_ready(HAL_StatusTypeDef status)
{
  s_next_ready_failure = status;
}

void fake_fail_next_async(HAL_StatusTypeDef status)
{
  s_next_async_failure = status;
}

uint32_t fake_i2c_get_flag(const I2C_HandleTypeDef *hi2c, uint32_t flag)
{
  if ((hi2c != &hi2c2) || (flag != I2C_FLAG_BUSY))
  {
    return 0u;
  }
  return s_bus_busy ? 1u : 0u;
}

HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *hi2c,
                                        uint16_t dev_address,
                                        uint32_t trials,
                                        uint32_t timeout)
{
  HAL_StatusTypeDef failure;

  (void)trials;
  (void)timeout;
  s_hal_call_count++;
  if ((hi2c != &hi2c2) ||
      (hi2c->State != HAL_I2C_STATE_READY))
  {
    return HAL_BUSY;
  }
  fake_wait_for_blocking_busy();
  s_last_blocking_address = dev_address;
  failure = s_next_ready_failure;
  s_next_ready_failure = HAL_OK;
  return failure;
}

HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *hi2c,
                                          uint16_t dev_address,
                                          uint8_t *data,
                                          uint16_t size,
                                          uint32_t timeout)
{
  HAL_StatusTypeDef failure;

  (void)timeout;
  s_hal_call_count++;
  if ((hi2c != &hi2c2) || (data == NULL) || (size == 0u) ||
      (hi2c->State != HAL_I2C_STATE_READY))
  {
    return HAL_BUSY;
  }
  fake_wait_for_blocking_busy();

  failure = fake_consume_blocking_failure();
  if (failure != HAL_OK)
  {
    return failure;
  }
  fake_record_blocking(dev_address, data, size);
  return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *hi2c,
                                    uint16_t dev_address,
                                    uint16_t mem_address,
                                    uint16_t mem_address_size,
                                    uint8_t *data,
                                    uint16_t size,
                                    uint32_t timeout)
{
  HAL_StatusTypeDef failure;

  (void)timeout;
  s_hal_call_count++;
  if ((hi2c != &hi2c2) || (data == NULL) || (size == 0u) ||
      (mem_address_size != I2C_MEMADD_SIZE_8BIT) ||
      (hi2c->State != HAL_I2C_STATE_READY))
  {
    return HAL_BUSY;
  }
  fake_wait_for_blocking_busy();

  failure = fake_consume_blocking_failure();
  if (failure != HAL_OK)
  {
    return failure;
  }

  s_last_blocking_address = dev_address;
  s_last_blocking_data_len = 0u;
  if ((dev_address == 0xD0u) &&
      (mem_address < FAKE_MPU_REGISTER_COUNT))
  {
    size_t available = FAKE_MPU_REGISTER_COUNT - mem_address;
    size_t copy_len = (size < available) ? size : available;
    memcpy(&s_mpu_regs[mem_address], data, copy_len);
  }
  return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *hi2c,
                                   uint16_t dev_address,
                                   uint16_t mem_address,
                                   uint16_t mem_address_size,
                                   uint8_t *data,
                                   uint16_t size,
                                   uint32_t timeout)
{
  HAL_StatusTypeDef failure;

  (void)timeout;
  s_hal_call_count++;
  if ((hi2c != &hi2c2) || (data == NULL) || (size == 0u) ||
      (mem_address_size != I2C_MEMADD_SIZE_8BIT) ||
      (hi2c->State != HAL_I2C_STATE_READY))
  {
    return HAL_BUSY;
  }
  fake_wait_for_blocking_busy();

  failure = fake_consume_blocking_failure();
  if (failure != HAL_OK)
  {
    return failure;
  }

  s_last_blocking_address = dev_address;
  s_last_blocking_data_len = 0u;
  if ((dev_address == 0xD0u) &&
      (mem_address < FAKE_MPU_REGISTER_COUNT))
  {
    size_t available = FAKE_MPU_REGISTER_COUNT - mem_address;
    size_t copy_len = (size < available) ? size : available;
    memcpy(data, &s_mpu_regs[mem_address], copy_len);
    if (copy_len != size)
    {
      return HAL_ERROR;
    }
    if (s_next_mem_read_leaves_busy)
    {
      s_next_mem_read_leaves_busy = false;
      s_bus_busy = true;
    }
    return HAL_OK;
  }
  return HAL_ERROR;
}

static HAL_StatusTypeDef fake_start_async(fake_async_kind_t kind,
                                          uint16_t dev_address,
                                          uint16_t mem_address,
                                          uint8_t *data,
                                          uint16_t size)
{
  HAL_StatusTypeDef failure;
  size_t copy_len = size;

  s_hal_call_count++;
  s_async_start_count++;
  s_async_address = dev_address;
  s_async_mem_address = mem_address;
  s_async_size = size;
  s_async_kind = kind;
  s_async_data = data;

  if ((data == NULL) || (size == 0u))
  {
    return HAL_ERROR;
  }
  if (copy_len > sizeof(s_last_async_data))
  {
    copy_len = sizeof(s_last_async_data);
  }
  memcpy(s_last_async_data, data, copy_len);
  s_last_async_data_len = copy_len;

  failure = s_next_async_failure;
  s_next_async_failure = HAL_OK;
  if (failure != HAL_OK)
  {
    return failure;
  }
  if (s_bus_busy || s_async_active ||
      (hi2c2.State != HAL_I2C_STATE_READY))
  {
    return HAL_BUSY;
  }

  s_bus_busy = true;
  s_async_active = true;
  hi2c2.State = (kind == FAKE_ASYNC_TX) ?
                HAL_I2C_STATE_BUSY_TX : HAL_I2C_STATE_BUSY_RX;
  hi2c2.Mode = HAL_I2C_MODE_MASTER;
  hi2c2.ErrorCode = HAL_I2C_ERROR_NONE;
  return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_Master_Transmit_IT(I2C_HandleTypeDef *hi2c,
                                             uint16_t dev_address,
                                             uint8_t *data,
                                             uint16_t size)
{
  if (hi2c != &hi2c2)
  {
    return HAL_ERROR;
  }
  return fake_start_async(FAKE_ASYNC_TX, dev_address, 0u, data, size);
}

HAL_StatusTypeDef HAL_I2C_Mem_Read_IT(I2C_HandleTypeDef *hi2c,
                                      uint16_t dev_address,
                                      uint16_t mem_address,
                                      uint16_t mem_address_size,
                                      uint8_t *data,
                                      uint16_t size)
{
  if ((hi2c != &hi2c2) ||
      (mem_address_size != I2C_MEMADD_SIZE_8BIT))
  {
    return HAL_ERROR;
  }
  return fake_start_async(FAKE_ASYNC_MEM_READ, dev_address,
                          mem_address, data, size);
}

void fake_complete_async_success(void)
{
  fake_async_kind_t kind;

  if (!s_async_active)
  {
    return;
  }

  kind = s_async_kind;
  if ((kind == FAKE_ASYNC_MEM_READ) &&
      (s_async_address == 0xD0u) &&
      (s_async_mem_address < FAKE_MPU_REGISTER_COUNT))
  {
    size_t available = FAKE_MPU_REGISTER_COUNT - s_async_mem_address;
    size_t copy_len = (s_async_size < available) ?
                      s_async_size : available;
    memcpy(s_async_data, &s_mpu_regs[s_async_mem_address], copy_len);
  }

  s_async_active = false;
  s_async_kind = FAKE_ASYNC_NONE;
  s_bus_busy = false;
  hi2c2.State = HAL_I2C_STATE_READY;
  hi2c2.Mode = HAL_I2C_MODE_NONE;
  hi2c2.ErrorCode = HAL_I2C_ERROR_NONE;

  if (kind == FAKE_ASYNC_TX)
  {
    HAL_I2C_MasterTxCpltCallback(&hi2c2);
  }
  else if (kind == FAKE_ASYNC_MEM_READ)
  {
    HAL_I2C_MemRxCpltCallback(&hi2c2);
  }
}

void fake_complete_async_error(uint32_t error_code)
{
  if (!s_async_active)
  {
    return;
  }

  s_async_active = false;
  s_async_kind = FAKE_ASYNC_NONE;
  s_bus_busy = false;
  hi2c2.State = HAL_I2C_STATE_READY;
  hi2c2.Mode = HAL_I2C_MODE_NONE;
  hi2c2.ErrorCode = error_code;
  HAL_I2C_ErrorCallback(&hi2c2);
}

uint32_t fake_hal_call_count(void)
{
  return s_hal_call_count;
}

uint32_t fake_async_start_count(void)
{
  return s_async_start_count;
}

uint32_t fake_blocking_busy_wait_count(void)
{
  return s_blocking_busy_wait_count;
}

uint16_t fake_last_blocking_address(void)
{
  return s_last_blocking_address;
}

uint16_t fake_last_async_address(void)
{
  return s_async_address;
}

uint16_t fake_last_async_mem_address(void)
{
  return s_async_mem_address;
}

uint16_t fake_last_async_size(void)
{
  return s_async_size;
}

fake_async_kind_t fake_last_async_kind(void)
{
  return s_async_kind;
}

size_t fake_copy_last_blocking_data(uint8_t *dst, size_t capacity)
{
  size_t copy_len = s_last_blocking_data_len;

  if ((dst == NULL) && (capacity != 0u))
  {
    return 0u;
  }
  if (copy_len > capacity)
  {
    copy_len = capacity;
  }
  if (copy_len != 0u)
  {
    memcpy(dst, s_last_blocking_data, copy_len);
  }
  return copy_len;
}

size_t fake_copy_last_async_data(uint8_t *dst, size_t capacity)
{
  size_t copy_len = s_last_async_data_len;

  if ((dst == NULL) && (capacity != 0u))
  {
    return 0u;
  }
  if (copy_len > capacity)
  {
    copy_len = capacity;
  }
  if (copy_len != 0u)
  {
    memcpy(dst, s_last_async_data, copy_len);
  }
  return copy_len;
}

void fake_set_mpu_bytes(uint16_t start_register,
                        const uint8_t *data,
                        size_t len)
{
  size_t available;
  size_t copy_len;

  if ((data == NULL) || (start_register >= FAKE_MPU_REGISTER_COUNT))
  {
    return;
  }
  available = FAKE_MPU_REGISTER_COUNT - start_register;
  copy_len = (len < available) ? len : available;
  memcpy(&s_mpu_regs[start_register], data, copy_len);
}

uint8_t fake_get_mpu_register(uint16_t reg)
{
  return (reg < FAKE_MPU_REGISTER_COUNT) ? s_mpu_regs[reg] : 0u;
}
