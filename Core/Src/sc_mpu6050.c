/**
  ******************************************************************************
  * @file    sc_mpu6050.c
  * @brief   MPU6050 六轴传感器非阻塞采样驱动。
  ******************************************************************************
  */

#include "sc_mpu6050.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "main.h"
#include "sc_i2c2.h"

#define SC_MPU6050_REG_CONFIG       0x1Au
#define SC_MPU6050_REG_GYRO_CONFIG  0x1Bu
#define SC_MPU6050_REG_ACCEL_CONFIG 0x1Cu
#define SC_MPU6050_REG_SAMPLE_RATE  0x19u
#define SC_MPU6050_REG_DATA_START   0x3Bu
#define SC_MPU6050_REG_PWR_MGMT_1   0x6Bu
#define SC_MPU6050_REG_PWR_MGMT_2   0x6Cu
#define SC_MPU6050_REG_WHO_AM_I     0x75u

#define SC_MPU6050_SAMPLE_PERIOD_MS 10u
#define SC_MPU6050_RETRY_PERIOD_MS  100u
#define SC_MPU6050_I2C_TIMEOUT_MS   20u
#define SC_MPU6050_DATA_LEN         14u
#define SC_MPU6050_MAX_CALIBRATION  1024u

#define SC_MPU6050_ACCEL_SCALE_DEN  16384
#define SC_MPU6050_GYRO_SCALE_DEN   131
#define SC_MPU6050_TEMP_SCALE_DEN   340
#define SC_MPU6050_TEMP_OFFSET_MDEG 36530

typedef struct {
  uint8_t active;
  uint16_t completed;
  uint16_t target;
  int64_t gyro_z_sum_mdps;
} sc_mpu6050_calibration_t;

static uint8_t s_initialized;
static uint8_t s_sampling;
static volatile uint8_t s_read_done;
static volatile uint8_t s_read_pending;
static uint32_t s_sample_start_ms;
static uint32_t s_next_sample_ms;
static uint32_t s_sequence;
static sc_mpu6050_status_t s_last_status;
static volatile sc_mpu6050_status_t s_read_result;
static uint8_t s_read_buffer[SC_MPU6050_DATA_LEN];
static sc_mpu6050_sample_t s_latest;
static sc_mpu6050_bias_t s_bias;
static sc_mpu6050_calibration_t s_calibration;

static uint32_t sc_mpu6050_enter_critical(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

static void sc_mpu6050_exit_critical(uint32_t primask)
{
  if (primask == 0u)
  {
    __enable_irq();
  }
}

static bool sc_mpu6050_time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
  return (int32_t)(now_ms - deadline_ms) >= 0;
}

static sc_mpu6050_status_t sc_mpu6050_map_i2c2(
  sc_i2c2_status_t status)
{
  switch (status)
  {
    case SC_I2C2_OK:
      return SC_MPU6050_OK;
    case SC_I2C2_ERR_ARGUMENT:
      return SC_MPU6050_ERR_ARGUMENT;
    case SC_I2C2_ERR_CONFIG:
      return SC_MPU6050_ERR_CONFIG;
    case SC_I2C2_ERR_NOT_READY:
      return SC_MPU6050_ERR_NOT_READY;
    case SC_I2C2_ERR_BUSY:
      return SC_MPU6050_ERR_BUSY;
    case SC_I2C2_ERR_TIMEOUT:
      return SC_MPU6050_ERR_TIMEOUT;
    case SC_I2C2_ERR_BUS:
    default:
      return SC_MPU6050_ERR_BUS;
  }
}

static sc_mpu6050_status_t sc_mpu6050_record_failure(
  sc_mpu6050_status_t status)
{
  s_last_status = status;
  s_sampling = 0u;
  s_latest.valid = 0u;
  return status;
}

static sc_mpu6050_status_t sc_mpu6050_record_status(
  sc_mpu6050_status_t status)
{
  s_last_status = status;
  return status;
}

static int16_t sc_mpu6050_read_be16(const uint8_t *data)
{
  return (int16_t)(((uint16_t)data[0] << 8u) | (uint16_t)data[1]);
}

static int32_t sc_mpu6050_accel_to_mg(int16_t raw)
{
  return ((int32_t)raw * 1000) / SC_MPU6050_ACCEL_SCALE_DEN;
}

static int32_t sc_mpu6050_gyro_to_mdps(int16_t raw)
{
  return ((int32_t)raw * 2000) / SC_MPU6050_GYRO_SCALE_DEN;
}

static int32_t sc_mpu6050_temp_to_mdeg_c(int16_t raw)
{
  return (((int32_t)raw * 1000) / SC_MPU6050_TEMP_SCALE_DEN) +
         SC_MPU6050_TEMP_OFFSET_MDEG;
}

static sc_mpu6050_status_t sc_mpu6050_write_verify(uint8_t reg,
                                                   uint8_t value)
{
  uint8_t readback = 0u;
  sc_i2c2_status_t status;

  status = sc_i2c2_mem_write_blocking(
    SC_MPU6050_I2C_ADDRESS, reg, I2C_MEMADD_SIZE_8BIT,
    &value, 1u, SC_MPU6050_I2C_TIMEOUT_MS);
  if (status != SC_I2C2_OK)
  {
    return sc_mpu6050_map_i2c2(status);
  }

  status = sc_i2c2_mem_read_blocking(
    SC_MPU6050_I2C_ADDRESS, reg, I2C_MEMADD_SIZE_8BIT,
    &readback, 1u, SC_MPU6050_I2C_TIMEOUT_MS);
  if (status != SC_I2C2_OK)
  {
    return sc_mpu6050_map_i2c2(status);
  }

  return (readback == value) ? SC_MPU6050_OK : SC_MPU6050_ERR_CONFIG;
}

static void sc_mpu6050_read_complete(sc_i2c2_status_t status, void *context)
{
  (void)context;
  s_read_result = sc_mpu6050_map_i2c2(status);
  s_read_pending = 0u;
  s_read_done = 1u;
}

static void sc_mpu6050_decode_sample(uint32_t sample_time_ms)
{
  uint8_t axis;

  for (axis = 0u; axis < 3u; axis++)
  {
    s_latest.accel_raw[axis] =
      sc_mpu6050_read_be16(&s_read_buffer[axis * 2u]);
    s_latest.gyro_raw[axis] =
      sc_mpu6050_read_be16(&s_read_buffer[8u + axis * 2u]);
    s_latest.accel_mg[axis] =
      sc_mpu6050_accel_to_mg(s_latest.accel_raw[axis]);
    s_latest.gyro_mdps[axis] =
      sc_mpu6050_gyro_to_mdps(s_latest.gyro_raw[axis]);
  }

  s_latest.temp_raw = sc_mpu6050_read_be16(&s_read_buffer[6]);
  s_latest.temp_mdeg_c =
    sc_mpu6050_temp_to_mdeg_c(s_latest.temp_raw);
  s_latest.sample_time_ms = sample_time_ms;
  s_sequence++;
  s_latest.sequence = s_sequence;
  s_latest.valid = 1u;
  s_latest.calibrated = s_bias.valid;
}

static void sc_mpu6050_publish_calibration(void)
{
  if ((s_calibration.active == 0u) ||
      (s_calibration.completed < s_calibration.target))
  {
    return;
  }

  s_bias.gyro_z_mdps =
    (int32_t)(s_calibration.gyro_z_sum_mdps /
              (int64_t)s_calibration.completed);
  s_bias.valid = 1u;
  s_calibration.active = 0u;
  s_latest.calibrated = 1u;
}

static void sc_mpu6050_handle_success(void)
{
  sc_mpu6050_decode_sample(s_sample_start_ms);
  s_last_status = SC_MPU6050_OK;

  if (s_calibration.active != 0u)
  {
    s_calibration.gyro_z_sum_mdps += s_latest.gyro_mdps[2];
    s_calibration.completed++;
    sc_mpu6050_publish_calibration();
  }

}

static void sc_mpu6050_handle_failure(uint32_t now_ms,
                                      sc_mpu6050_status_t status)
{
  s_latest.valid = 0u;
  s_last_status = status;

  if (s_calibration.active != 0u)
  {
    s_calibration.active = 0u;
    s_bias.valid = 0u;
  }

  s_next_sample_ms = now_ms + SC_MPU6050_RETRY_PERIOD_MS;
}

static void sc_mpu6050_service_completion(uint32_t now_ms)
{
  uint32_t primask;
  uint8_t done = 0u;
  sc_mpu6050_status_t result;

  primask = sc_mpu6050_enter_critical();
  if (s_read_done != 0u)
  {
    done = 1u;
    s_read_done = 0u;
    result = s_read_result;
  }
  sc_mpu6050_exit_critical(primask);

  if (done == 0u)
  {
    return;
  }

  if (result == SC_MPU6050_OK)
  {
    sc_mpu6050_handle_success();
  }
  else
  {
    sc_mpu6050_handle_failure(now_ms, result);
  }
}

static void sc_mpu6050_start_read(uint32_t now_ms)
{
  sc_mpu6050_status_t status;

  if (s_read_pending != 0u)
  {
    return;
  }

  s_sample_start_ms = now_ms;
  s_read_pending = 1u;
  status = sc_mpu6050_map_i2c2(sc_i2c2_mem_read_async(
    SC_MPU6050_I2C_ADDRESS,
    SC_MPU6050_REG_DATA_START,
    I2C_MEMADD_SIZE_8BIT,
    s_read_buffer,
    SC_MPU6050_DATA_LEN,
    sc_mpu6050_read_complete,
    NULL));

  if (status == SC_MPU6050_ERR_BUSY)
  {
    s_read_pending = 0u;
    return;
  }
  if (status != SC_MPU6050_OK)
  {
    s_read_pending = 0u;
    sc_mpu6050_handle_failure(now_ms, status);
    return;
  }

  s_next_sample_ms = now_ms + SC_MPU6050_SAMPLE_PERIOD_MS;
  s_sampling = 1u;
}

sc_mpu6050_status_t sc_mpu6050_init(void)
{
  uint8_t who_am_i = 0u;
  sc_i2c2_status_t i2c_status;
  sc_mpu6050_status_t status;

  if (s_initialized != 0u)
  {
    return SC_MPU6050_OK;
  }

  i2c_status = sc_i2c2_init();
  if (i2c_status != SC_I2C2_OK)
  {
    return sc_mpu6050_record_failure(
      sc_mpu6050_map_i2c2(i2c_status));
  }

  i2c_status = sc_i2c2_is_device_ready(
    SC_MPU6050_I2C_ADDRESS, 1u, SC_MPU6050_I2C_TIMEOUT_MS);
  if (i2c_status != SC_I2C2_OK)
  {
    return sc_mpu6050_record_failure(
      sc_mpu6050_map_i2c2(i2c_status));
  }

  i2c_status = sc_i2c2_mem_read_blocking(
    SC_MPU6050_I2C_ADDRESS,
    SC_MPU6050_REG_WHO_AM_I,
    I2C_MEMADD_SIZE_8BIT,
    &who_am_i, 1u, SC_MPU6050_I2C_TIMEOUT_MS);
  if (i2c_status != SC_I2C2_OK)
  {
    return sc_mpu6050_record_failure(
      sc_mpu6050_map_i2c2(i2c_status));
  }
  if (who_am_i != SC_MPU6050_EXPECTED_WHO_AM_I)
  {
    return sc_mpu6050_record_failure(SC_MPU6050_ERR_CONFIG);
  }

  status = sc_mpu6050_write_verify(SC_MPU6050_REG_PWR_MGMT_1, 0x01u);
  if (status != SC_MPU6050_OK)
  {
    return sc_mpu6050_record_failure(status);
  }
  HAL_Delay(30u);

  status = sc_mpu6050_write_verify(SC_MPU6050_REG_PWR_MGMT_2, 0x00u);
  if (status != SC_MPU6050_OK)
  {
    return sc_mpu6050_record_failure(status);
  }
  status = sc_mpu6050_write_verify(SC_MPU6050_REG_CONFIG, 0x03u);
  if (status != SC_MPU6050_OK)
  {
    return sc_mpu6050_record_failure(status);
  }
  status = sc_mpu6050_write_verify(SC_MPU6050_REG_SAMPLE_RATE, 0x09u);
  if (status != SC_MPU6050_OK)
  {
    return sc_mpu6050_record_failure(status);
  }
  status = sc_mpu6050_write_verify(SC_MPU6050_REG_GYRO_CONFIG, 0x08u);
  if (status != SC_MPU6050_OK)
  {
    return sc_mpu6050_record_failure(status);
  }
  status = sc_mpu6050_write_verify(SC_MPU6050_REG_ACCEL_CONFIG, 0x00u);
  if (status != SC_MPU6050_OK)
  {
    return sc_mpu6050_record_failure(status);
  }

  memset(&s_latest, 0, sizeof(s_latest));
  memset(&s_bias, 0, sizeof(s_bias));
  memset(&s_calibration, 0, sizeof(s_calibration));
  s_initialized = 1u;
  s_sampling = 0u;
  s_read_done = 0u;
  s_read_pending = 0u;
  s_sequence = 0u;
  s_next_sample_ms = HAL_GetTick();
  s_last_status = SC_MPU6050_OK;
  return SC_MPU6050_OK;
}

sc_mpu6050_status_t sc_mpu6050_start_calibration(uint16_t sample_count)
{
  uint32_t primask;
  uint8_t pending;
  uint8_t completed;
  uint8_t bus_busy;

  if (s_initialized == 0u)
  {
    return sc_mpu6050_record_status(SC_MPU6050_ERR_NOT_READY);
  }
  if ((sample_count == 0u) ||
      (sample_count > SC_MPU6050_MAX_CALIBRATION))
  {
    return sc_mpu6050_record_status(SC_MPU6050_ERR_ARGUMENT);
  }

  primask = sc_mpu6050_enter_critical();
  pending = s_read_pending;
  completed = s_read_done;
  bus_busy = sc_i2c2_is_busy();
  sc_mpu6050_exit_critical(primask);

  if ((pending != 0u) || (completed != 0u) || (bus_busy != 0u))
  {
    return sc_mpu6050_record_status(SC_MPU6050_ERR_BUSY);
  }

  memset(&s_calibration, 0, sizeof(s_calibration));
  memset(&s_bias, 0, sizeof(s_bias));
  s_calibration.active = 1u;
  s_calibration.target = sample_count;
  s_latest.calibrated = 0u;
  s_next_sample_ms = HAL_GetTick();
  s_last_status = SC_MPU6050_OK;
  return SC_MPU6050_OK;
}

void sc_mpu6050_service(uint32_t now_ms)
{
  if (s_initialized == 0u)
  {
    return;
  }

  sc_mpu6050_service_completion(now_ms);

  if ((s_read_pending == 0u) &&
      sc_mpu6050_time_reached(now_ms, s_next_sample_ms))
  {
    sc_mpu6050_start_read(now_ms);
  }
}

sc_mpu6050_status_t sc_mpu6050_get_latest(
  sc_mpu6050_sample_t *sample,
  uint32_t *sequence)
{
  if (sample == NULL)
  {
    return SC_MPU6050_ERR_ARGUMENT;
  }
  if (s_sequence == 0u)
  {
    return SC_MPU6050_ERR_NOT_READY;
  }

  *sample = s_latest;
  if (sequence != NULL)
  {
    *sequence = s_sequence;
  }
  return SC_MPU6050_OK;
}

sc_mpu6050_status_t sc_mpu6050_get_bias_mdps(int32_t *bias_mdps)
{
  if (bias_mdps == NULL)
  {
    return SC_MPU6050_ERR_ARGUMENT;
  }
  if (s_bias.valid == 0u)
  {
    return SC_MPU6050_ERR_NOT_READY;
  }

  *bias_mdps = s_bias.gyro_z_mdps;
  return SC_MPU6050_OK;
}

sc_mpu6050_status_t sc_mpu6050_get_bias(sc_mpu6050_bias_t *bias)
{
  sc_mpu6050_status_t status;
  int32_t bias_mdps;

  if (bias == NULL)
  {
    return SC_MPU6050_ERR_ARGUMENT;
  }

  status = sc_mpu6050_get_bias_mdps(&bias_mdps);
  if (status != SC_MPU6050_OK)
  {
    return status;
  }

  bias->gyro_z_mdps = bias_mdps;
  bias->valid = 1u;
  return SC_MPU6050_OK;
}

sc_mpu6050_status_t sc_mpu6050_get_status(sc_mpu6050_state_t *state)
{
  if (state == NULL)
  {
    return SC_MPU6050_ERR_ARGUMENT;
  }

  state->initialized = s_initialized;
  state->sampling = s_sampling;
  state->calibrating = s_calibration.active;
  state->calibrated = s_bias.valid;
  state->latest_valid = s_latest.valid;
  state->calibration_samples = s_calibration.completed;
  state->calibration_target = s_calibration.target;
  state->last_status = s_last_status;
  return SC_MPU6050_OK;
}
