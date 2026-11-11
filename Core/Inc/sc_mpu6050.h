/**
  ******************************************************************************
  * @file    sc_mpu6050.h
  * @brief   MPU6050 六轴传感器非阻塞采样驱动。
  ******************************************************************************
  */

#ifndef __SC_MPU6050_H
#define __SC_MPU6050_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define SC_MPU6050_I2C_ADDRESS       0x68u
#define SC_MPU6050_EXPECTED_WHO_AM_I 0x68u

typedef enum {
  SC_MPU6050_OK = 0,
  SC_MPU6050_ERR_ARGUMENT,
  SC_MPU6050_ERR_CONFIG,
  SC_MPU6050_ERR_NOT_READY,
  SC_MPU6050_ERR_BUSY,
  SC_MPU6050_ERR_TIMEOUT,
  SC_MPU6050_ERR_BUS
} sc_mpu6050_status_t;

typedef struct {
  int16_t accel_raw[3];
  int16_t gyro_raw[3];
  int16_t temp_raw;
  int32_t accel_mg[3];
  int32_t gyro_mdps[3];
  int32_t temp_mdeg_c;
  uint32_t sample_time_ms;
  uint32_t sequence;
  uint8_t valid;
  uint8_t calibrated;
} sc_mpu6050_sample_t;

typedef struct {
  int32_t gyro_z_mdps;
  uint8_t valid;
} sc_mpu6050_bias_t;

typedef struct {
  uint8_t initialized;
  uint8_t sampling;
  uint8_t calibrating;
  uint8_t calibrated;
  uint8_t latest_valid;
  uint16_t calibration_samples;
  uint16_t calibration_target;
  sc_mpu6050_status_t last_status;
} sc_mpu6050_state_t;

/**
  * @brief  校验 I2C2、WHO_AM_I，并写后回读全部工作配置。
  *         成功后可重复调用；失败只影响 IMU。
  */
sc_mpu6050_status_t sc_mpu6050_init(void);

/**
  * @brief  开始一次显式的有限样本静止校零。
  *         车辆是否静止由调用者保证；完成前不发布新零偏。
  */
sc_mpu6050_status_t sc_mpu6050_start_calibration(uint16_t sample_count);

/**
  * @brief  主循环服务函数；按 100 Hz 启动一次 14 B 异步读取。
  */
void sc_mpu6050_service(uint32_t now_ms);

/**
  * @brief  复制最近一次成功样本。
  */
sc_mpu6050_status_t sc_mpu6050_get_latest(
  sc_mpu6050_sample_t *sample,
  uint32_t *sequence);

/**
  * @brief  获取最近一次完整校零得到的 gyro-Z 零偏，单位为 mdeg/s。
  * @param  bias_mdps 输出零偏；未完成校零时返回 NOT_READY。
  */
sc_mpu6050_status_t sc_mpu6050_get_bias_mdps(int32_t *bias_mdps);

/**
  * @brief  兼容接口：获取 gyro-Z 零偏及有效标志。
  */
sc_mpu6050_status_t sc_mpu6050_get_bias(sc_mpu6050_bias_t *bias);

/**
  * @brief  获取驱动状态快照。
  */
sc_mpu6050_status_t sc_mpu6050_get_status(sc_mpu6050_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* __SC_MPU6050_H */
