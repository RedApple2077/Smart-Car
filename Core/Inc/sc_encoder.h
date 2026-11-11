/**
  ******************************************************************************
  * @file    sc_encoder.h
  * @brief   TIM2/TIM3 双路正交霍尔编码器轮询驱动。
  ******************************************************************************
  */

#ifndef __SC_ENCODER_H
#define __SC_ENCODER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define SC_ENCODER_SAMPLE_PERIOD_MS 10u
#define SC_ENCODER_MAX_INTERVAL_MS  100u

typedef enum {
  SC_ENCODER_OK = 0,
  SC_ENCODER_ERR_ARGUMENT,
  SC_ENCODER_ERR_CONFIG,
  SC_ENCODER_ERR_NOT_READY,
  SC_ENCODER_ERR_HAL,
  SC_ENCODER_ERR_LOST_SYNC
} sc_encoder_status_t;

typedef struct {
  uint16_t raw_count;
  int64_t total_count;
  int32_t delta_count;
  uint8_t valid;
  uint8_t lost_sync;
} sc_encoder_side_snapshot_t;

typedef struct {
  sc_encoder_side_snapshot_t left;
  sc_encoder_side_snapshot_t right;
  uint32_t sample_time_ms;
  uint32_t interval_ms;
  uint32_t sequence;
  uint8_t valid;
} sc_encoder_snapshot_t;

typedef struct {
  uint8_t initialized;
  uint8_t lost_sync;
  sc_encoder_status_t last_status;
  sc_encoder_side_snapshot_t left;
  sc_encoder_side_snapshot_t right;
  uint32_t sample_time_ms;
  uint32_t interval_ms;
  uint32_t sequence;
  uint32_t start_failures;
  uint32_t rollback_count;
  uint32_t ambiguous_events;
  uint32_t long_interval_events;
  uint32_t lost_sync_events;
  uint32_t resets;
  uint32_t last_hal_error;
} sc_encoder_state_t;

/**
  * @brief  校验并启动 CubeMX 已初始化的 TIM2/TIM3 编码器接口。
  *         成功后的重复调用不重启定时器，也不清除累计计数。
  */
sc_encoder_status_t sc_encoder_init(void);

/**
  * @brief  按 10 ms 目标周期读取左右 CNT，并在主循环上下文发布快照。
  *         任一侧失同步时返回 SC_ENCODER_ERR_LOST_SYNC。
  *         轮询法要求两次采样间真实净变化绝对值小于 32768，且不能隐藏
  *         恰好 65536 次的整圈变化；超过 100 ms 的间隔会主动标记失同步。
  */
sc_encoder_status_t sc_encoder_service(uint32_t now_ms);

/**
  * @brief  复制最近快照，不访问定时器硬件。
  */
sc_encoder_status_t sc_encoder_get_latest(sc_encoder_snapshot_t *snapshot);

/**
  * @brief  获取编码器驱动状态与统计快照。
  */
sc_encoder_status_t sc_encoder_get_status(sc_encoder_state_t *state);

/**
  * @brief  清除 TIM2/TIM3 计数和累计值，并用 now_ms 重建采样基线。
  *         这是失同步后恢复累计计数的唯一入口。
  */
sc_encoder_status_t sc_encoder_reset_counts(uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* __SC_ENCODER_H */
