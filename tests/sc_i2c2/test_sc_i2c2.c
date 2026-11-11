#include "main.h"

#include <stdio.h>
#include <string.h>

#include "sc_font5x7.h"
#include "sc_i2c2.h"
#include "sc_mpu6050.h"
#include "sc_ssd1306.h"

#define TEST_GYRO_Z_UNIT_RAW 131

#define CHECK(condition)                                                     \
  do                                                                         \
  {                                                                          \
    if (!(condition))                                                        \
    {                                                                        \
      fprintf(stderr, "CHECK failed at %s:%d: %s\n",                        \
              __FILE__, __LINE__, #condition);                               \
      return false;                                                          \
    }                                                                        \
  } while (0)

static const uint8_t s_sample_14[14] = {
  0x40u, 0x00u,
  0xC0u, 0x00u,
  0x00u, 0x00u,
  0x00u, 0x00u,
  0x02u, 0x8Fu,
  0xFDu, 0x71u,
  0x02u, 0x8Fu
};

static void test_i2c_callback(sc_i2c2_status_t status, void *context)
{
  (void)status;
  (void)context;
}

static void set_gyro_z_raw(uint8_t *sample, int16_t raw)
{
  uint16_t encoded = (uint16_t)raw;

  memset(sample, 0, 14u);
  sample[12] = (uint8_t)(encoded >> 8u);
  sample[13] = (uint8_t)encoded;
}

static void build_expected_line(uint8_t *page, const char *text)
{
  uint16_t col = 0u;
  size_t i;
  uint8_t k;

  memset(page, 0, SC_SSD1306_WIDTH);
  for (i = 0u;
       (text[i] != '\0') && (i < SC_SSD1306_LINE_CHARS);
       i++)
  {
    const uint8_t *glyph =
      sc_font5x7[(uint8_t)text[i] - 0x20u];

    for (k = 0u; k < 5u; k++)
    {
      page[col + k] = glyph[k];
    }
    col = (uint16_t)(col + 6u);
  }
}

static bool reset_and_init_bus(void)
{
  fake_hal_reset();
  CHECK(sc_i2c2_init() == SC_I2C2_OK);
  CHECK(sc_i2c2_init() == SC_I2C2_OK);
  return true;
}

static bool init_mpu(void)
{
  CHECK(sc_mpu6050_init() == SC_MPU6050_OK);
  return true;
}

static bool fetch_one_sample(uint32_t now_ms,
                             sc_mpu6050_sample_t *sample)
{
  uint32_t sequence = 0u;

  sc_mpu6050_service(now_ms);
  CHECK(fake_last_async_kind() == FAKE_ASYNC_MEM_READ);
  CHECK(fake_last_async_address() == 0xD0u);
  CHECK(fake_last_async_mem_address() == 0x3Bu);
  CHECK(fake_last_async_size() == 14u);
  fake_complete_async_success();
  sc_mpu6050_service(now_ms);
  CHECK(sc_mpu6050_get_latest(sample, &sequence) == SC_MPU6050_OK);
  CHECK(sequence == sample->sequence);
  return true;
}

static bool drive_oled_transactions(uint32_t now_ms, uint32_t count)
{
  uint32_t i;

  for (i = 0u; i < count; i++)
  {
    sc_ssd1306_service(now_ms);
    CHECK(fake_last_async_kind() == FAKE_ASYNC_TX);
    fake_complete_async_success();
  }
  sc_ssd1306_service(now_ms);
  return true;
}

static bool test_i2c_config_and_idempotency(void)
{
  uint8_t byte = 0xA5u;

  fake_hal_reset();
  CHECK(sc_i2c2_master_transmit_blocking(0x3Cu, &byte, 1u, 10u) ==
        SC_I2C2_ERR_NOT_READY);
  CHECK(sc_i2c2_init() == SC_I2C2_OK);
  CHECK(sc_i2c2_init() == SC_I2C2_OK);

  hi2c2.Init.ClockSpeed = 400000u;
  CHECK(sc_i2c2_init() == SC_I2C2_ERR_CONFIG);
  hi2c2.Init.ClockSpeed = 100000u;
  hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_10BIT;
  CHECK(sc_i2c2_init() == SC_I2C2_ERR_CONFIG);
  hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;

  fake_set_i2c_state(HAL_I2C_STATE_BUSY_TX);
  CHECK(sc_i2c2_init() == SC_I2C2_ERR_NOT_READY);
  fake_set_i2c_state(HAL_I2C_STATE_READY);

  fake_set_i2c_state(HAL_I2C_STATE_BUSY_TX);
  CHECK(sc_i2c2_master_transmit_blocking(0x3Cu, &byte, 1u, 10u) ==
        SC_I2C2_ERR_BUSY);
  CHECK(fake_hal_call_count() == 0u);
  return true;
}

static bool test_blocking_busy_settle(void)
{
  uint8_t who_am_i = 0u;
  uint8_t value = 0x01u;
  uint8_t readback = 0u;
  uint32_t calls;
  uint32_t async_calls;

  CHECK(reset_and_init_bus());
  CHECK(sc_i2c2_is_device_ready(0x68u, 1u, 20u) == SC_I2C2_OK);
  fake_set_next_mem_read_leaves_busy(true);
  CHECK(sc_i2c2_mem_read_blocking(
          0x68u, 0x75u, I2C_MEMADD_SIZE_8BIT,
          &who_am_i, 1u, 20u) == SC_I2C2_OK);
  CHECK(who_am_i == 0x68u);
  CHECK(hi2c2.State == HAL_I2C_STATE_READY);
  CHECK(hi2c2.ErrorCode == HAL_I2C_ERROR_NONE);
  CHECK(fake_i2c_get_flag(&hi2c2, I2C_FLAG_BUSY) != RESET);

  calls = fake_hal_call_count();
  CHECK(sc_i2c2_mem_write_blocking(
          0x68u, 0x6Bu, I2C_MEMADD_SIZE_8BIT,
          &value, 1u, 20u) == SC_I2C2_OK);
  CHECK(fake_hal_call_count() == calls + 1u);
  CHECK(fake_blocking_busy_wait_count() == 1u);
  CHECK(fake_i2c_get_flag(&hi2c2, I2C_FLAG_BUSY) == RESET);

  CHECK(sc_i2c2_mem_read_blocking(
          0x68u, 0x6Bu, I2C_MEMADD_SIZE_8BIT,
          &readback, 1u, 20u) == SC_I2C2_OK);
  CHECK(readback == value);

  fake_set_i2c_bus_busy(true);
  async_calls = fake_async_start_count();
  CHECK(sc_i2c2_master_transmit_async(
          0x3Cu, &value, 1u,
          test_i2c_callback, NULL) == SC_I2C2_ERR_BUSY);
  CHECK(fake_async_start_count() == async_calls);
  return true;
}

static bool test_i2c_single_transaction(void)
{
  uint8_t first = 0x11u;
  uint8_t second = 0x22u;

  CHECK(reset_and_init_bus());
  CHECK(sc_i2c2_master_transmit_async(
          0x3Cu, &first, 1u, NULL, NULL) == SC_I2C2_ERR_ARGUMENT);
  CHECK(sc_i2c2_master_transmit_async(
          0x3Cu, &first, 1u, test_i2c_callback, NULL) == SC_I2C2_OK);
  CHECK(sc_i2c2_is_busy() != 0u);
  CHECK(sc_i2c2_master_transmit_async(
          0x3Cu, &second, 1u,
          test_i2c_callback, NULL) == SC_I2C2_ERR_BUSY);
  CHECK(fake_async_start_count() == 1u);
  fake_complete_async_success();
  CHECK(sc_i2c2_is_busy() == 0u);
  CHECK(sc_i2c2_master_transmit_async(
          0x3Cu, &second, 1u,
          test_i2c_callback, NULL) == SC_I2C2_OK);
  CHECK(fake_async_start_count() == 2u);
  CHECK(sc_i2c2_is_busy() != 0u);
  HAL_I2C_MemRxCpltCallback(&hi2c2);
  CHECK(sc_i2c2_is_busy() != 0u);
  fake_complete_async_success();
  CHECK(sc_i2c2_is_busy() == 0u);
  return true;
}

static bool test_mpu_init_who_and_config(void)
{
  uint32_t calls;

  fake_hal_reset();
  fake_set_mpu_bytes(0x75u, (const uint8_t[]){0x00u}, 1u);
  CHECK(sc_mpu6050_init() == SC_MPU6050_ERR_CONFIG);

  fake_hal_reset();
  CHECK(init_mpu());
  CHECK(fake_get_mpu_register(0x6Bu) == 0x01u);
  CHECK(fake_get_mpu_register(0x6Cu) == 0x00u);
  CHECK(fake_get_mpu_register(0x1Au) == 0x03u);
  CHECK(fake_get_mpu_register(0x19u) == 0x09u);
  CHECK(fake_get_mpu_register(0x1Bu) == 0x08u);
  CHECK(fake_get_mpu_register(0x1Cu) == 0x00u);
  CHECK(fake_last_blocking_address() == 0xD0u);

  calls = fake_hal_call_count();
  CHECK(sc_mpu6050_init() == SC_MPU6050_OK);
  CHECK(fake_hal_call_count() == calls);
  return true;
}

static bool test_mpu_decode_and_failure(void)
{
  sc_mpu6050_sample_t sample;
  sc_mpu6050_state_t state;
  uint32_t sequence = 0u;

  fake_hal_reset();
  CHECK(init_mpu());
  fake_set_mpu_bytes(0x3Bu, s_sample_14, sizeof(s_sample_14));
  CHECK(fetch_one_sample(0u, &sample));

  CHECK(sample.accel_raw[0] == 16384);
  CHECK(sample.accel_raw[1] == -16384);
  CHECK(sample.accel_mg[0] == 1000);
  CHECK(sample.accel_mg[1] == -1000);
  CHECK(sample.gyro_raw[0] == 655);
  CHECK(sample.gyro_raw[1] == -655);
  CHECK(sample.gyro_mdps[0] == 10000);
  CHECK(sample.gyro_mdps[1] == -10000);
  CHECK(sample.gyro_mdps[2] == 10000);
  CHECK(sample.temp_raw == 0);
  CHECK(sample.temp_mdeg_c == 36530);
  CHECK(sample.sequence == 1u);
  CHECK(sample.valid == 1u);
  CHECK(sample.calibrated == 0u);

  sc_mpu6050_service(10u);
  CHECK(fake_last_async_kind() == FAKE_ASYNC_MEM_READ);
  fake_complete_async_error(HAL_I2C_ERROR_AF);
  sc_mpu6050_service(10u);

  CHECK(sc_mpu6050_get_latest(&sample, &sequence) == SC_MPU6050_OK);
  CHECK(sample.valid == 0u);
  CHECK(sample.sequence == 1u);
  CHECK(sequence == 1u);
  CHECK(sc_mpu6050_get_status(&state) == SC_MPU6050_OK);
  CHECK(state.last_status == SC_MPU6050_ERR_BUS);
  CHECK(state.latest_valid == 0u);

  sc_mpu6050_service(110u);
  CHECK(fake_last_async_kind() == FAKE_ASYNC_MEM_READ);
  fake_complete_async_error(HAL_I2C_ERROR_TIMEOUT);
  sc_mpu6050_service(110u);
  CHECK(sc_mpu6050_get_status(&state) == SC_MPU6050_OK);
  CHECK(state.last_status == SC_MPU6050_ERR_TIMEOUT);
  CHECK(sc_mpu6050_get_latest(&sample, &sequence) == SC_MPU6050_OK);
  CHECK(sample.valid == 0u);
  CHECK(sequence == 1u);
  return true;
}

static bool test_mpu_bias_api(void)
{
  sc_mpu6050_bias_t bias;
  uint8_t sample[14];
  int32_t bias_mdps = 1234;

  fake_hal_reset();
  CHECK(init_mpu());
  CHECK(sc_mpu6050_get_bias_mdps(NULL) == SC_MPU6050_ERR_ARGUMENT);
  CHECK(sc_mpu6050_get_bias(NULL) == SC_MPU6050_ERR_ARGUMENT);
  CHECK(sc_mpu6050_get_bias_mdps(&bias_mdps) ==
        SC_MPU6050_ERR_NOT_READY);
  CHECK(bias_mdps == 1234);
  CHECK(sc_mpu6050_get_bias(&bias) == SC_MPU6050_ERR_NOT_READY);
  CHECK(sc_mpu6050_start_calibration(2u) == SC_MPU6050_OK);

  set_gyro_z_raw(sample, 131);
  fake_set_mpu_bytes(0x3Bu, sample, sizeof(sample));
  sc_mpu6050_service(0u);
  CHECK(fake_last_async_kind() == FAKE_ASYNC_MEM_READ);
  fake_complete_async_success();

  set_gyro_z_raw(sample, 262);
  fake_set_mpu_bytes(0x3Bu, sample, sizeof(sample));
  sc_mpu6050_service(10u);
  CHECK(fake_last_async_kind() == FAKE_ASYNC_MEM_READ);
  fake_complete_async_success();
  sc_mpu6050_service(20u);

  CHECK(sc_mpu6050_get_bias_mdps(&bias_mdps) == SC_MPU6050_OK);
  CHECK(bias_mdps == 3000);
  CHECK(sc_mpu6050_get_bias(&bias) == SC_MPU6050_OK);
  CHECK(bias.valid == 1u);
  CHECK(bias.gyro_z_mdps == 3000);
  return true;
}

static bool test_mpu_calibration(void)
{
  sc_mpu6050_bias_t bias;
  sc_mpu6050_state_t state;
  uint8_t sample[14];
  int32_t bias_mdps = 0;
  uint32_t i;

  fake_hal_reset();
  CHECK(init_mpu());
  CHECK(sc_mpu6050_start_calibration(128u) == SC_MPU6050_OK);

  for (i = 0u; i < 127u; i++)
  {
    uint32_t now_ms = i * 10u;

    set_gyro_z_raw(
      sample, (int16_t)(((int32_t)i - 64) * TEST_GYRO_Z_UNIT_RAW));
    fake_set_mpu_bytes(0x3Bu, sample, sizeof(sample));
    sc_mpu6050_service(now_ms);
    CHECK(fake_last_async_kind() == FAKE_ASYNC_MEM_READ);
    fake_complete_async_success();
  }

  set_gyro_z_raw(
    sample, (int16_t)(63 * TEST_GYRO_Z_UNIT_RAW));
  fake_set_mpu_bytes(0x3Bu, sample, sizeof(sample));
  sc_mpu6050_service(1270u);
  CHECK(sc_mpu6050_get_status(&state) == SC_MPU6050_OK);
  CHECK(state.calibrating == 1u);
  CHECK(state.calibration_samples == 127u);
  CHECK(state.calibration_target == 128u);
  CHECK(state.calibrated == 0u);
  CHECK(sc_mpu6050_get_bias(&bias) == SC_MPU6050_ERR_NOT_READY);
  CHECK(sc_mpu6050_get_bias_mdps(&bias_mdps) ==
        SC_MPU6050_ERR_NOT_READY);

  fake_complete_async_success();
  sc_mpu6050_service(1280u);
  CHECK(sc_mpu6050_get_status(&state) == SC_MPU6050_OK);
  CHECK(state.calibrating == 0u);
  CHECK(state.calibration_samples == 128u);
  CHECK(state.calibrated == 1u);
  CHECK(sc_mpu6050_get_bias(&bias) == SC_MPU6050_OK);
  CHECK(bias.valid == 1u);
  CHECK(bias.gyro_z_mdps == -1000);
  CHECK(sc_mpu6050_get_bias_mdps(&bias_mdps) == SC_MPU6050_OK);
  CHECK(bias_mdps == -1000);
  return true;
}

static bool test_mpu_calibration_completion_race(void)
{
  sc_mpu6050_state_t state;
  uint8_t sample[14];
  int32_t bias_mdps = 0;

  fake_hal_reset();
  CHECK(init_mpu());

  set_gyro_z_raw(sample, 655);
  fake_set_mpu_bytes(0x3Bu, sample, sizeof(sample));
  sc_mpu6050_service(0u);
  CHECK(fake_last_async_kind() == FAKE_ASYNC_MEM_READ);
  fake_complete_async_success();

  CHECK(sc_mpu6050_start_calibration(2u) == SC_MPU6050_ERR_BUSY);
  CHECK(sc_mpu6050_get_status(&state) == SC_MPU6050_OK);
  CHECK(state.calibrating == 0u);
  CHECK(state.calibration_samples == 0u);

  sc_mpu6050_service(0u);
  CHECK(sc_mpu6050_start_calibration(2u) == SC_MPU6050_OK);

  set_gyro_z_raw(sample, 131);
  fake_set_mpu_bytes(0x3Bu, sample, sizeof(sample));
  sc_mpu6050_service(0u);
  CHECK(fake_last_async_kind() == FAKE_ASYNC_MEM_READ);
  fake_complete_async_success();

  set_gyro_z_raw(sample, 262);
  fake_set_mpu_bytes(0x3Bu, sample, sizeof(sample));
  sc_mpu6050_service(10u);
  CHECK(fake_last_async_kind() == FAKE_ASYNC_MEM_READ);
  fake_complete_async_success();
  sc_mpu6050_service(20u);

  CHECK(sc_mpu6050_get_bias_mdps(&bias_mdps) == SC_MPU6050_OK);
  CHECK(bias_mdps == 3000);
  return true;
}

static bool test_mpu_calibration_failure_isolation(void)
{
  sc_mpu6050_sample_t sample;
  sc_mpu6050_state_t state;
  uint32_t sequence = 0u;

  fake_hal_reset();
  CHECK(init_mpu());
  fake_set_mpu_bytes(0x3Bu, s_sample_14, sizeof(s_sample_14));
  CHECK(fetch_one_sample(0u, &sample));
  CHECK(sample.sequence == 1u);

  CHECK(sc_mpu6050_start_calibration(2u) == SC_MPU6050_OK);
  sc_mpu6050_service(10u);
  fake_complete_async_error(HAL_I2C_ERROR_BERR);
  sc_mpu6050_service(10u);
  CHECK(sc_mpu6050_get_latest(&sample, &sequence) == SC_MPU6050_OK);
  CHECK(sample.valid == 0u);
  CHECK(sample.sequence == 1u);
  CHECK(sc_mpu6050_get_status(&state) == SC_MPU6050_OK);
  CHECK(state.calibrating == 0u);
  CHECK(state.calibrated == 0u);

  sc_mpu6050_service(110u);
  CHECK(fake_last_async_kind() == FAKE_ASYNC_MEM_READ);
  fake_complete_async_success();
  sc_mpu6050_service(110u);
  CHECK(sc_mpu6050_get_latest(&sample, &sequence) == SC_MPU6050_OK);
  CHECK(sample.valid == 1u);
  CHECK(sample.sequence == 2u);
  CHECK(sequence == 2u);
  CHECK(sample.calibrated == 0u);
  return true;
}

static bool test_oled_init_chunks(void)
{
  static const char text[] = "0123456789ABCDEFGHIJ";
  uint8_t expected[SC_SSD1306_WIDTH];
  uint8_t data[64];
  uint8_t chunk;
  size_t len;

  fake_hal_reset();
  CHECK(sc_ssd1306_init() == SC_SSD1306_OK);
  CHECK(sc_ssd1306_write_line(0u, text) == SC_SSD1306_OK);
  build_expected_line(expected, text);
  CHECK(fake_last_blocking_address() == 0x78u);
  len = fake_copy_last_blocking_data(data, sizeof(data));
  CHECK(len > 1u);
  CHECK(data[0] == 0x00u);

  sc_ssd1306_service(0u);
  CHECK(fake_last_async_kind() == FAKE_ASYNC_TX);
  CHECK(fake_last_async_address() == 0x78u);
  CHECK(fake_last_async_size() == 4u);
  len = fake_copy_last_async_data(data, sizeof(data));
  CHECK(len == 4u);
  CHECK(data[0] == 0x00u);
  CHECK(data[1] == 0xB0u);
  CHECK(data[2] == 0x00u);
  CHECK(data[3] == 0x10u);

  fake_complete_async_success();

  for (chunk = 0u; chunk < 4u; chunk++)
  {
    sc_ssd1306_service(0u);
    CHECK(fake_last_async_kind() == FAKE_ASYNC_TX);
    CHECK(fake_last_async_address() == 0x78u);
    CHECK(fake_last_async_size() == 33u);
    len = fake_copy_last_async_data(data, sizeof(data));
    CHECK(len == 33u);
    CHECK(data[0] == 0x40u);
    CHECK(memcmp(&data[1],
                 &expected[(uint16_t)chunk * 32u],
                 32u) == 0);
    fake_complete_async_success();
  }
  return true;
}

static bool test_oled_generation_protection(void)
{
  sc_ssd1306_status_info_t status;
  uint8_t data[64];

  fake_hal_reset();
  CHECK(sc_ssd1306_init() == SC_SSD1306_OK);
  CHECK(drive_oled_transactions(0u, 40u));
  CHECK(sc_ssd1306_get_status(&status) == SC_SSD1306_OK);
  CHECK(status.dirty_pages == 0u);

  CHECK(sc_ssd1306_write_line(0u, "A") == SC_SSD1306_OK);
  CHECK(sc_ssd1306_refresh() == SC_SSD1306_OK);
  sc_ssd1306_service(0u);
  CHECK(fake_last_async_kind() == FAKE_ASYNC_TX);
  fake_complete_async_success();
  sc_ssd1306_service(0u);
  CHECK(fake_last_async_kind() == FAKE_ASYNC_TX);
  CHECK(fake_last_async_size() == 33u);

  CHECK(sc_ssd1306_write_line(0u, "B") == SC_SSD1306_OK);
  fake_complete_async_success();

  sc_ssd1306_service(0u);
  fake_complete_async_success();
  sc_ssd1306_service(0u);
  fake_complete_async_success();
  sc_ssd1306_service(0u);
  fake_complete_async_success();
  sc_ssd1306_service(0u);

  CHECK(sc_ssd1306_get_status(&status) == SC_SSD1306_OK);
  CHECK((status.dirty_pages & 0x01u) != 0u);
  CHECK(status.refresh_pending == 1u);
  CHECK(fake_last_async_kind() == FAKE_ASYNC_TX);
  CHECK(fake_copy_last_async_data(data, sizeof(data)) == 4u);
  CHECK(data[0] == 0x00u);
  CHECK(data[1] == 0xB0u);
  return true;
}

static bool test_oled_failure_backoff(void)
{
  sc_ssd1306_status_info_t status;
  uint32_t calls;

  fake_hal_reset();
  CHECK(sc_ssd1306_init() == SC_SSD1306_OK);
  CHECK(drive_oled_transactions(0u, 40u));
  CHECK(sc_ssd1306_write_line(1u, "RETRY") == SC_SSD1306_OK);
  CHECK(sc_ssd1306_refresh() == SC_SSD1306_OK);

  calls = fake_async_start_count();
  fake_fail_next_async(HAL_ERROR);
  sc_ssd1306_service(0u);
  CHECK(fake_async_start_count() == calls + 1u);
  CHECK(sc_ssd1306_get_status(&status) == SC_SSD1306_OK);
  CHECK(status.state == SC_SSD1306_STATE_RETRY_WAIT);
  CHECK(status.last_status == SC_SSD1306_ERR_BUS);
  CHECK((status.dirty_pages & 0x02u) != 0u);

  sc_ssd1306_service(99u);
  CHECK(fake_async_start_count() == calls + 1u);
  sc_ssd1306_service(100u);
  CHECK(fake_async_start_count() == calls + 2u);
  CHECK(fake_last_async_kind() == FAKE_ASYNC_TX);
  CHECK(fake_last_async_size() == 4u);
  return true;
}

static bool test_shared_bus_priority(void)
{
  uint32_t calls;

  fake_hal_reset();
  CHECK(init_mpu());
  CHECK(sc_ssd1306_init() == SC_SSD1306_OK);
  fake_set_mpu_bytes(0x3Bu, s_sample_14, sizeof(s_sample_14));

  sc_mpu6050_service(0u);
  CHECK(fake_last_async_kind() == FAKE_ASYNC_MEM_READ);
  calls = fake_async_start_count();
  sc_ssd1306_service(0u);
  CHECK(fake_async_start_count() == calls);

  fake_complete_async_success();
  sc_mpu6050_service(0u);
  sc_ssd1306_service(0u);
  CHECK(fake_last_async_kind() == FAKE_ASYNC_TX);
  fake_complete_async_success();

  calls = fake_async_start_count();
  sc_mpu6050_service(10u);
  CHECK(fake_async_start_count() == calls + 1u);
  CHECK(fake_last_async_kind() == FAKE_ASYNC_MEM_READ);
  CHECK(fake_last_async_address() == 0xD0u);
  sc_ssd1306_service(10u);
  CHECK(fake_async_start_count() == calls + 1u);
  return true;
}

static bool test_mpu_failure_oled_isolation(void)
{
  uint32_t calls;

  fake_hal_reset();
  fake_set_mpu_bytes(0x75u, (const uint8_t[]){0x00u}, 1u);
  CHECK(sc_mpu6050_init() == SC_MPU6050_ERR_CONFIG);
  CHECK(sc_ssd1306_init() == SC_SSD1306_OK);

  calls = fake_async_start_count();
  sc_ssd1306_service(0u);
  CHECK(fake_async_start_count() == calls + 1u);
  CHECK(fake_last_async_kind() == FAKE_ASYNC_TX);
  return true;
}

static bool test_device_failure_isolation(void)
{
  uint32_t calls;

  fake_hal_reset();
  CHECK(init_mpu());
  fake_fail_next_ready(HAL_ERROR);
  CHECK(sc_ssd1306_init() == SC_SSD1306_ERR_BUS);

  calls = fake_async_start_count();
  sc_ssd1306_service(0u);
  CHECK(fake_async_start_count() == calls);
  CHECK(sc_ssd1306_refresh() == SC_SSD1306_ERR_NOT_READY);

  fake_set_mpu_bytes(0x3Bu, s_sample_14, sizeof(s_sample_14));
  sc_mpu6050_service(0u);
  CHECK(fake_async_start_count() == calls + 1u);
  CHECK(fake_last_async_kind() == FAKE_ASYNC_MEM_READ);
  return true;
}

typedef struct {
  const char *name;
  bool (*run)(void);
} test_case_t;

static const test_case_t s_tests[] = {
  {"i2c_config_and_idempotency", test_i2c_config_and_idempotency},
  {"blocking_busy_settle", test_blocking_busy_settle},
  {"i2c_single_transaction", test_i2c_single_transaction},
  {"mpu_init_who_and_config", test_mpu_init_who_and_config},
  {"mpu_decode_and_failure", test_mpu_decode_and_failure},
  {"mpu_bias_api", test_mpu_bias_api},
  {"mpu_calibration", test_mpu_calibration},
  {"mpu_calibration_completion_race",
   test_mpu_calibration_completion_race},
  {"mpu_calibration_failure_isolation",
   test_mpu_calibration_failure_isolation},
  {"oled_init_chunks", test_oled_init_chunks},
  {"oled_generation_protection", test_oled_generation_protection},
  {"oled_failure_backoff", test_oled_failure_backoff},
  {"shared_bus_priority", test_shared_bus_priority},
  {"mpu_failure_oled_isolation", test_mpu_failure_oled_isolation},
  {"device_failure_isolation", test_device_failure_isolation}
};

int main(int argc, char **argv)
{
  size_t i;

  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <test-name>\n", argv[0]);
    return 2;
  }

  for (i = 0u; i < (sizeof(s_tests) / sizeof(s_tests[0])); i++)
  {
    if (strcmp(argv[1], s_tests[i].name) == 0)
    {
      return s_tests[i].run() ? 0 : 1;
    }
  }

  fprintf(stderr, "unknown test: %s\n", argv[1]);
  return 2;
}
