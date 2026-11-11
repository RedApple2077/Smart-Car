/**
  ******************************************************************************
  * @file    sc_encoder.c
  * @brief   TIM2/TIM3 双路正交霍尔编码器轮询驱动实现。
  ******************************************************************************
  */

#include "sc_encoder.h"

#include <limits.h>
#include <stddef.h>

#include "main.h"

extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim3;

static uint8_t s_initialized;
static uint8_t s_first_sample;
static uint32_t s_last_sample_time_ms;
static sc_encoder_snapshot_t s_latest;
static sc_encoder_state_t s_state;

static sc_encoder_status_t sc_encoder_validate_config(void)
{
  if ((htim2.Instance != TIM2) || (htim3.Instance != TIM3) ||
      (htim2.Init.Prescaler != 0u) || (htim3.Init.Prescaler != 0u) ||
      (htim2.Init.Period != 65535u) || (htim3.Init.Period != 65535u) ||
      (htim2.Init.CounterMode != TIM_COUNTERMODE_UP) ||
      (htim3.Init.CounterMode != TIM_COUNTERMODE_UP))
  {
    return SC_ENCODER_ERR_CONFIG;
  }
  return SC_ENCODER_OK;
}

static int32_t sc_encoder_delta(uint16_t current, uint16_t previous,
                                uint8_t *ambiguous)
{
  uint16_t difference = (uint16_t)(current - previous);

  *ambiguous = 0u;
  if (difference == 0x8000u)
  {
    *ambiguous = 1u;
    return 0;
  }
  if (difference <= 0x7FFFu)
  {
    return (int32_t)difference;
  }
  return (int32_t)difference - 65536;
}

static void sc_encoder_mark_lost(sc_encoder_side_snapshot_t *side)
{
  if (side->lost_sync == 0u)
  {
    side->lost_sync = 1u;
    s_state.lost_sync_events++;
  }
  side->valid = 0u;
  side->delta_count = 0;
}

static void sc_encoder_update_side(sc_encoder_side_snapshot_t *side,
                                   uint16_t current,
                                   uint8_t force_lost)
{
  uint8_t ambiguous;
  int32_t delta;
  uint16_t previous = side->raw_count;

  if ((force_lost != 0u) || (side->lost_sync != 0u))
  {
    side->raw_count = current;
    if (force_lost != 0u)
    {
      sc_encoder_mark_lost(side);
    }
    side->delta_count = 0;
    side->valid = 0u;
    return;
  }

  delta = sc_encoder_delta(current, previous, &ambiguous);
  side->raw_count = current;
  if (ambiguous != 0u)
  {
    s_state.ambiguous_events++;
    sc_encoder_mark_lost(side);
    return;
  }
  if (((delta > 0) && (side->total_count > (INT64_MAX - delta))) ||
      ((delta < 0) && (side->total_count < (INT64_MIN - delta))))
  {
    sc_encoder_mark_lost(side);
    return;
  }

  side->total_count += delta;
  side->delta_count = delta;
  side->valid = 1u;
}

static void sc_encoder_update_snapshot(uint32_t now_ms, uint32_t interval_ms)
{
  s_latest.left = s_state.left;
  s_latest.right = s_state.right;
  s_latest.sample_time_ms = now_ms;
  s_latest.interval_ms = interval_ms;
  s_latest.sequence++;
  s_latest.valid =
    (uint8_t)((s_latest.left.valid != 0u) &&
              (s_latest.right.valid != 0u));

  s_state.sample_time_ms = s_latest.sample_time_ms;
  s_state.interval_ms = s_latest.interval_ms;
  s_state.sequence = s_latest.sequence;
  s_state.lost_sync =
    (uint8_t)((s_state.left.lost_sync != 0u) ||
              (s_state.right.lost_sync != 0u));
}

sc_encoder_status_t sc_encoder_init(void)
{
  HAL_StatusTypeDef hal_status;
  sc_encoder_status_t status;

  if (s_initialized != 0u)
  {
    return SC_ENCODER_OK;
  }

  status = sc_encoder_validate_config();
  if (status != SC_ENCODER_OK)
  {
    s_state.last_status = status;
    return status;
  }

  __HAL_TIM_SET_COUNTER(&htim2, 0u);
  __HAL_TIM_SET_COUNTER(&htim3, 0u);

  hal_status = HAL_TIM_Encoder_Start(&htim2, TIM_CHANNEL_ALL);
  if (hal_status != HAL_OK)
  {
    s_state.start_failures++;
    s_state.last_hal_error = (uint32_t)hal_status;
    s_state.last_status = SC_ENCODER_ERR_HAL;
    return SC_ENCODER_ERR_HAL;
  }

  hal_status = HAL_TIM_Encoder_Start(&htim3, TIM_CHANNEL_ALL);
  if (hal_status != HAL_OK)
  {
    HAL_StatusTypeDef stop_status;

    s_state.start_failures++;
    s_state.last_hal_error = (uint32_t)hal_status;
    stop_status = HAL_TIM_Encoder_Stop(&htim2, TIM_CHANNEL_ALL);
    if (stop_status != HAL_OK)
    {
      s_state.last_hal_error = (uint32_t)stop_status;
    }
    s_state.rollback_count++;
    s_state.last_status = SC_ENCODER_ERR_HAL;
    return SC_ENCODER_ERR_HAL;
  }

  s_latest.left.raw_count = 0u;
  s_latest.left.total_count = 0;
  s_latest.left.delta_count = 0;
  s_latest.left.valid = 0u;
  s_latest.left.lost_sync = 0u;
  s_latest.right = s_latest.left;
  s_latest.sample_time_ms = 0u;
  s_latest.interval_ms = 0u;
  s_latest.sequence = 0u;
  s_latest.valid = 0u;

  s_state.left = s_latest.left;
  s_state.right = s_latest.right;
  s_state.sample_time_ms = 0u;
  s_state.interval_ms = 0u;
  s_state.sequence = 0u;
  s_state.lost_sync = 0u;
  s_state.initialized = 1u;
  s_state.last_status = SC_ENCODER_OK;
  s_first_sample = 1u;
  s_last_sample_time_ms = 0u;
  s_initialized = 1u;
  return SC_ENCODER_OK;
}

sc_encoder_status_t sc_encoder_service(uint32_t now_ms)
{
  uint16_t left_raw;
  uint16_t right_raw;
  uint32_t interval_ms;
  uint8_t force_lost;

  if (s_initialized == 0u)
  {
    s_state.last_status = SC_ENCODER_ERR_NOT_READY;
    return SC_ENCODER_ERR_NOT_READY;
  }

  if ((s_first_sample == 0u) &&
      ((uint32_t)(now_ms - s_last_sample_time_ms) <
       SC_ENCODER_SAMPLE_PERIOD_MS))
  {
    return s_state.last_status;
  }

  interval_ms = (s_first_sample != 0u) ? 0u :
    (uint32_t)(now_ms - s_last_sample_time_ms);
  force_lost =
    (uint8_t)((s_first_sample == 0u) &&
              (interval_ms > SC_ENCODER_MAX_INTERVAL_MS));
  if (force_lost != 0u)
  {
    s_state.long_interval_events++;
  }

  left_raw = (uint16_t)__HAL_TIM_GET_COUNTER(&htim2);
  right_raw = (uint16_t)__HAL_TIM_GET_COUNTER(&htim3);

  if (s_first_sample != 0u)
  {
    s_state.left.raw_count = left_raw;
    s_state.right.raw_count = right_raw;
    s_state.left.total_count = 0;
    s_state.right.total_count = 0;
    s_state.left.delta_count = 0;
    s_state.right.delta_count = 0;
    s_state.left.valid = 1u;
    s_state.right.valid = 1u;
    s_state.left.lost_sync = 0u;
    s_state.right.lost_sync = 0u;
    s_first_sample = 0u;
  }
  else
  {
    sc_encoder_update_side(&s_state.left, left_raw, force_lost);
    sc_encoder_update_side(&s_state.right, right_raw, force_lost);
  }

  s_last_sample_time_ms = now_ms;
  sc_encoder_update_snapshot(now_ms, interval_ms);
  if (s_state.lost_sync != 0u)
  {
    s_state.last_status = SC_ENCODER_ERR_LOST_SYNC;
    return SC_ENCODER_ERR_LOST_SYNC;
  }
  s_state.last_status = SC_ENCODER_OK;
  return SC_ENCODER_OK;
}

sc_encoder_status_t sc_encoder_get_latest(sc_encoder_snapshot_t *snapshot)
{
  if (snapshot == NULL)
  {
    return SC_ENCODER_ERR_ARGUMENT;
  }
  *snapshot = s_latest;
  return (s_initialized != 0u) ? SC_ENCODER_OK :
    SC_ENCODER_ERR_NOT_READY;
}

sc_encoder_status_t sc_encoder_get_status(sc_encoder_state_t *state)
{
  if (state == NULL)
  {
    return SC_ENCODER_ERR_ARGUMENT;
  }
  *state = s_state;
  return (s_initialized != 0u) ? SC_ENCODER_OK :
    SC_ENCODER_ERR_NOT_READY;
}

sc_encoder_status_t sc_encoder_reset_counts(uint32_t now_ms)
{
  if (s_initialized == 0u)
  {
    s_state.last_status = SC_ENCODER_ERR_NOT_READY;
    return SC_ENCODER_ERR_NOT_READY;
  }

  __HAL_TIM_SET_COUNTER(&htim2, 0u);
  __HAL_TIM_SET_COUNTER(&htim3, 0u);

  s_state.left.raw_count = 0u;
  s_state.left.total_count = 0;
  s_state.left.delta_count = 0;
  s_state.left.valid = 1u;
  s_state.left.lost_sync = 0u;
  s_state.right = s_state.left;
  s_state.sample_time_ms = now_ms;
  s_state.interval_ms = 0u;
  s_state.sequence++;
  s_state.lost_sync = 0u;
  s_state.resets++;
  s_state.last_status = SC_ENCODER_OK;

  s_latest.left = s_state.left;
  s_latest.right = s_state.right;
  s_latest.sample_time_ms = now_ms;
  s_latest.interval_ms = 0u;
  s_latest.sequence = s_state.sequence;
  s_latest.valid = 1u;
  s_last_sample_time_ms = now_ms;
  s_first_sample = 0u;
  return SC_ENCODER_OK;
}
