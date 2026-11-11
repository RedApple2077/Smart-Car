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
  HAL_I2C_STATE_RESET = 0x00u,
  HAL_I2C_STATE_READY = 0x20u,
  HAL_I2C_STATE_BUSY = 0x24u,
  HAL_I2C_STATE_BUSY_TX = 0x21u,
  HAL_I2C_STATE_BUSY_RX = 0x22u,
  HAL_I2C_STATE_TIMEOUT = 0xA0u,
  HAL_I2C_STATE_ERROR = 0xE0u
} HAL_I2C_StateTypeDef;

typedef enum {
  HAL_I2C_MODE_NONE = 0x00u,
  HAL_I2C_MODE_MASTER = 0x10u,
  HAL_I2C_MODE_SLAVE = 0x20u,
  HAL_I2C_MODE_MEM = 0x40u
} HAL_I2C_ModeTypeDef;

typedef struct {
  uint32_t CR1;
} I2C_TypeDef;

typedef struct {
  uint32_t ClockSpeed;
  uint32_t DutyCycle;
  uint32_t OwnAddress1;
  uint32_t AddressingMode;
  uint32_t DualAddressMode;
  uint32_t OwnAddress2;
  uint32_t GeneralCallMode;
  uint32_t NoStretchMode;
} I2C_InitTypeDef;

typedef struct {
  I2C_TypeDef *Instance;
  I2C_InitTypeDef Init;
  volatile HAL_I2C_StateTypeDef PreviousState;
  volatile HAL_I2C_StateTypeDef State;
  volatile HAL_I2C_ModeTypeDef Mode;
  volatile uint32_t ErrorCode;
} I2C_HandleTypeDef;

#define I2C_ADDRESSINGMODE_7BIT       0x00004000u
#define I2C_ADDRESSINGMODE_10BIT      0x00008000u
#define I2C_DUALADDRESS_DISABLE       0x00000000u
#define I2C_MEMADD_SIZE_8BIT          0x00000001u
#define I2C_MEMADD_SIZE_16BIT         0x00000010u
#define I2C_FLAG_BUSY                 0x00100002u
#define HAL_I2C_ERROR_NONE            0x00000000u
#define HAL_I2C_ERROR_BERR            0x00000001u
#define HAL_I2C_ERROR_ARLO            0x00000002u
#define HAL_I2C_ERROR_AF              0x00000004u
#define HAL_I2C_ERROR_TIMEOUT         0x00000020u
#define RESET                         0u

extern I2C_TypeDef fake_i2c2_regs;
#define I2C2 (&fake_i2c2_regs)
extern I2C_HandleTypeDef hi2c2;

static inline uint32_t __get_PRIMASK(void)
{
  return 0u;
}

static inline void __set_PRIMASK(uint32_t primask)
{
  (void)primask;
}

static inline void __disable_irq(void)
{
}

static inline void __enable_irq(void)
{
}

uint32_t fake_i2c_get_flag(const I2C_HandleTypeDef *hi2c, uint32_t flag);
#define __HAL_I2C_GET_FLAG(handle, flag) fake_i2c_get_flag((handle), (flag))

uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t delay_ms);

HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *hi2c,
                                        uint16_t dev_address,
                                        uint32_t trials,
                                        uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *hi2c,
                                          uint16_t dev_address,
                                          uint8_t *data,
                                          uint16_t size,
                                          uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *hi2c,
                                    uint16_t dev_address,
                                    uint16_t mem_address,
                                    uint16_t mem_address_size,
                                    uint8_t *data,
                                    uint16_t size,
                                    uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *hi2c,
                                   uint16_t dev_address,
                                   uint16_t mem_address,
                                   uint16_t mem_address_size,
                                   uint8_t *data,
                                   uint16_t size,
                                   uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Master_Transmit_IT(I2C_HandleTypeDef *hi2c,
                                             uint16_t dev_address,
                                             uint8_t *data,
                                             uint16_t size);
HAL_StatusTypeDef HAL_I2C_Mem_Read_IT(I2C_HandleTypeDef *hi2c,
                                      uint16_t dev_address,
                                      uint16_t mem_address,
                                      uint16_t mem_address_size,
                                      uint8_t *data,
                                      uint16_t size);

void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hi2c);
void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c);
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c);

typedef enum {
  FAKE_ASYNC_NONE = 0,
  FAKE_ASYNC_TX,
  FAKE_ASYNC_MEM_READ
} fake_async_kind_t;

void fake_hal_reset(void);
void fake_set_tick(uint32_t tick);
void fake_set_i2c_state(HAL_I2C_StateTypeDef state);
void fake_set_i2c_bus_busy(bool busy);
void fake_set_next_mem_read_leaves_busy(bool leaves_busy);
void fake_fail_next_blocking(HAL_StatusTypeDef status);
void fake_fail_next_ready(HAL_StatusTypeDef status);
void fake_fail_next_async(HAL_StatusTypeDef status);
uint32_t fake_hal_call_count(void);
uint32_t fake_async_start_count(void);
uint32_t fake_blocking_busy_wait_count(void);
uint16_t fake_last_blocking_address(void);
uint16_t fake_last_async_address(void);
uint16_t fake_last_async_mem_address(void);
uint16_t fake_last_async_size(void);
fake_async_kind_t fake_last_async_kind(void);
size_t fake_copy_last_blocking_data(uint8_t *dst, size_t capacity);
size_t fake_copy_last_async_data(uint8_t *dst, size_t capacity);
void fake_set_mpu_bytes(uint16_t start_register,
                        const uint8_t *data,
                        size_t len);
uint8_t fake_get_mpu_register(uint16_t reg);
void fake_complete_async_success(void);
void fake_complete_async_error(uint32_t error_code);

#endif /* TEST_FAKE_MAIN_H */
