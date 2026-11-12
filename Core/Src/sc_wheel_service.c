/**
  ******************************************************************************
  * @file    sc_wheel_service.c
  * @brief   双编码器累计计数到有符号轮速的窗口服务实现。
  ******************************************************************************
  */

#include "sc_wheel_service.h"

#include <limits.h>
#include <stddef.h>

#define SC_WHEEL_PI_NUM  355
#define SC_WHEEL_PI_DEN  113

static uint8_t sc_wheel_calibration_equal(
  const sc_wheel_calibration_t *left,
  const sc_wheel_calibration_t *right)
{
  return (uint8_t)(
    (left->left_encoder_sign == right->left_encoder_sign) &&
    (left->right_encoder_sign == right->right_encoder_sign) &&
    (left->left_counts_per_rev == right->left_counts_per_rev) &&
    (left->right_counts_per_rev == right->right_counts_per_rev) &&
    (left->left_wheel_diameter_um == right->left_wheel_diameter_um) &&
    (left->right_wheel_diameter_um == right->right_wheel_diameter_um));
}

static uint8_t sc_wheel_calibration_valid(
  const sc_wheel_calibration_t *calibration)
{
  return (uint8_t)(
    ((calibration->left_encoder_sign == -1) ||
     (calibration->left_encoder_sign == 1)) &&
    ((calibration->right_encoder_sign == -1) ||
     (calibration->right_encoder_sign == 1)) &&
    (calibration->left_counts_per_rev > 0u) &&
    (calibration->right_counts_per_rev > 0u) &&
    (calibration->left_wheel_diameter_um > 0) &&
    (calibration->right_wheel_diameter_um > 0));
}

static void sc_wheel_reset_window(sc_wheel_window_t *window)
{
  window->next_index = 0u;
  window->sample_count = 0u;
}

static void sc_wheel_reset_windows(sc_wheel_service_t *service)
{
  sc_wheel_reset_window(&service->left);
  sc_wheel_reset_window(&service->right);
}

static int32_t sc_wheel_speed_mm_s(int64_t total_delta, int8_t encoder_sign,
                                   uint32_t counts_per_rev,
                                   int32_t wheel_diameter_um,
                                   uint32_t elapsed_ms)
{
  int64_t physical_delta;
  int64_t numerator;
  int64_t denominator;
  int64_t magnitude;
  int64_t rounded;
  uint8_t negative;

  if (encoder_sign < 0)
  {
    if (total_delta == INT64_MIN)
    {
      return INT32_MAX;
    }
    physical_delta = -total_delta;
  }
  else
  {
    if (total_delta == INT64_MIN)
    {
      return INT32_MIN;
    }
    physical_delta = total_delta;
  }
  negative = (uint8_t)(physical_delta < 0);
  magnitude = (physical_delta < 0) ? -physical_delta : physical_delta;

  denominator = (int64_t)SC_WHEEL_PI_DEN *
                (int64_t)counts_per_rev * (int64_t)elapsed_ms;
  if (denominator <= 0)
  {
    return 0;
  }
  if (magnitude >
      ((INT64_MAX - (denominator / 2)) /
       ((int64_t)wheel_diameter_um * SC_WHEEL_PI_NUM)))
  {
    return (negative != 0u) ? INT32_MIN : INT32_MAX;
  }

  numerator = magnitude * (int64_t)wheel_diameter_um * SC_WHEEL_PI_NUM;
  rounded = (numerator + (denominator / 2)) / denominator;
  if (negative != 0u)
  {
    rounded = -rounded;
  }
  if (rounded > INT32_MAX)
  {
    return INT32_MAX;
  }
  if (rounded < INT32_MIN)
  {
    return INT32_MIN;
  }
  return (int32_t)rounded;
}

static uint8_t sc_wheel_update_side(
  sc_wheel_window_t *window, int64_t current_total_count,
  uint32_t current_sample_time_ms, int8_t encoder_sign,
  uint32_t counts_per_rev, int32_t wheel_diameter_um,
  int32_t *speed_mm_s, int64_t *window_delta_count,
  uint32_t *window_span_ms)
{
  uint8_t oldest_index;
  uint8_t newest_index;
  uint32_t elapsed_ms;
  int64_t count_delta;

  *speed_mm_s = 0;
  *window_delta_count = 0;
  *window_span_ms = 0u;

  window->total_count[window->next_index] = current_total_count;
  window->sample_time_ms[window->next_index] = current_sample_time_ms;
  window->next_index =
    (uint8_t)((window->next_index + 1u) % SC_WHEEL_HISTORY_SAMPLES);
  if (window->sample_count < SC_WHEEL_HISTORY_SAMPLES)
  {
    window->sample_count++;
  }
  if (window->sample_count < SC_WHEEL_HISTORY_SAMPLES)
  {
    return 0u;
  }

  oldest_index = window->next_index;
  newest_index =
    (uint8_t)((window->next_index + SC_WHEEL_WINDOW_SAMPLES) %
              SC_WHEEL_HISTORY_SAMPLES);
  elapsed_ms =
    (uint32_t)(window->sample_time_ms[newest_index] -
               window->sample_time_ms[oldest_index]);
  if (elapsed_ms == 0u)
  {
    sc_wheel_reset_window(window);
    return 0u;
  }

  count_delta =
    window->total_count[newest_index] -
    window->total_count[oldest_index];
  *window_delta_count = count_delta;
  *window_span_ms = elapsed_ms;
  *speed_mm_s = sc_wheel_speed_mm_s(
    count_delta, encoder_sign, counts_per_rev, wheel_diameter_um,
    elapsed_ms);
  return 1u;
}

sc_wheel_service_status_t sc_wheel_service_init(
  sc_wheel_service_t *service, const sc_wheel_calibration_t *calibration)
{
  if ((service == NULL) || (calibration == NULL))
  {
    return SC_WHEEL_SERVICE_ERR_ARGUMENT;
  }
  if (sc_wheel_calibration_valid(calibration) == 0u)
  {
    return SC_WHEEL_SERVICE_ERR_CALIBRATION;
  }

  service->calibration = *calibration;
  sc_wheel_reset_windows(service);
  service->last_sequence = 0u;
  service->last_sample_time_ms = 0u;
  service->has_snapshot = 0u;
  service->initialized = 1u;
  return SC_WHEEL_SERVICE_OK;
}

sc_wheel_service_status_t sc_wheel_service_update(
  sc_wheel_service_t *service, const sc_encoder_snapshot_t *snapshot,
  const sc_wheel_calibration_t *calibration, uint32_t now_ms,
  sc_wheel_feedback_t *feedback)
{
  uint8_t left_input_valid;
  uint8_t right_input_valid;
  uint8_t left_lost;
  uint8_t right_lost;

  if ((service == NULL) || (snapshot == NULL) ||
      (calibration == NULL) || (feedback == NULL))
  {
    return SC_WHEEL_SERVICE_ERR_ARGUMENT;
  }
  if (sc_wheel_calibration_valid(calibration) == 0u)
  {
    return SC_WHEEL_SERVICE_ERR_CALIBRATION;
  }
  if (service->initialized == 0u)
  {
    return SC_WHEEL_SERVICE_ERR_NOT_READY;
  }
  if (sc_wheel_calibration_equal(&service->calibration, calibration) == 0u)
  {
    service->calibration = *calibration;
    sc_wheel_reset_windows(service);
    service->has_snapshot = 0u;
  }

  *feedback = (sc_wheel_feedback_t){0};
  feedback->left_total_count = snapshot->left.total_count;
  feedback->right_total_count = snapshot->right.total_count;
  feedback->left_raw_count = snapshot->left.raw_count;
  feedback->right_raw_count = snapshot->right.raw_count;
  feedback->sample_time_ms = snapshot->sample_time_ms;
  feedback->sequence = snapshot->sequence;

  if ((uint32_t)(now_ms - snapshot->sample_time_ms) >
      SC_WHEEL_MAX_SNAPSHOT_AGE_MS)
  {
    sc_wheel_reset_windows(service);
    return SC_WHEEL_SERVICE_ERR_STALE;
  }
  if ((service->has_snapshot != 0u) &&
      (snapshot->sequence == service->last_sequence))
  {
    return SC_WHEEL_SERVICE_ERR_DUPLICATE_SEQUENCE;
  }

  if ((service->has_snapshot != 0u) &&
      (snapshot->sample_time_ms == service->last_sample_time_ms))
  {
    sc_wheel_reset_windows(service);
    service->last_sequence = snapshot->sequence;
    service->last_sample_time_ms = snapshot->sample_time_ms;
    return SC_WHEEL_SERVICE_ERR_RESET_FRAME;
  }

  service->last_sequence = snapshot->sequence;
  service->last_sample_time_ms = snapshot->sample_time_ms;
  service->has_snapshot = 1u;

  if (snapshot->interval_ms == 0u)
  {
    sc_wheel_reset_windows(service);
    return SC_WHEEL_SERVICE_ERR_RESET_FRAME;
  }
  if (snapshot->interval_ms > SC_WHEEL_MAX_SNAPSHOT_AGE_MS)
  {
    sc_wheel_reset_windows(service);
    return SC_WHEEL_SERVICE_ERR_STALE;
  }

  left_lost = snapshot->left.lost_sync;
  right_lost = snapshot->right.lost_sync;
  left_input_valid =
    (uint8_t)((left_lost == 0u) && (snapshot->left.valid != 0u));
  right_input_valid =
    (uint8_t)((right_lost == 0u) && (snapshot->right.valid != 0u));
  if ((snapshot->valid == 0u) &&
      (left_input_valid != 0u) && (right_input_valid != 0u))
  {
    left_input_valid = 0u;
    right_input_valid = 0u;
  }

  if (left_input_valid != 0u)
  {
    feedback->left_valid = sc_wheel_update_side(
      &service->left, snapshot->left.total_count,
      snapshot->sample_time_ms, calibration->left_encoder_sign,
      calibration->left_counts_per_rev,
      calibration->left_wheel_diameter_um,
      &feedback->left_speed_mm_s,
      &feedback->left_window_delta_count,
      &feedback->left_window_span_ms);
  }
  else
  {
    sc_wheel_reset_window(&service->left);
  }

  if (right_input_valid != 0u)
  {
    feedback->right_valid = sc_wheel_update_side(
      &service->right, snapshot->right.total_count,
      snapshot->sample_time_ms, calibration->right_encoder_sign,
      calibration->right_counts_per_rev,
      calibration->right_wheel_diameter_um,
      &feedback->right_speed_mm_s,
      &feedback->right_window_delta_count,
      &feedback->right_window_span_ms);
  }
  else
  {
    sc_wheel_reset_window(&service->right);
  }

  feedback->valid =
    (uint8_t)((feedback->left_valid != 0u) &&
              (feedback->right_valid != 0u));
  if (feedback->valid != 0u)
  {
    return SC_WHEEL_SERVICE_OK;
  }
  if ((left_lost != 0u) || (right_lost != 0u))
  {
    return SC_WHEEL_SERVICE_ERR_LOST_SYNC;
  }
  if ((left_input_valid == 0u) || (right_input_valid == 0u))
  {
    return SC_WHEEL_SERVICE_ERR_SNAPSHOT;
  }
  return SC_WHEEL_SERVICE_WARMUP;
}
