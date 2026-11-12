/**
  ******************************************************************************
  * @file    sc_track.c
  * @brief   四路红外离散循迹的轮速目标策略实现。
  ******************************************************************************
  */

#include "sc_track.h"

#include <limits.h>
#include <stddef.h>

#define SC_TRACK_RATIO_SCALE          1000
#define SC_TRACK_CENTER_RATIO         1000
#define SC_TRACK_FINE_INNER_RATIO     558
#define SC_TRACK_FINE_OUTER_RATIO     1328
#define SC_TRACK_MEDIUM_INNER_RATIO   268
#define SC_TRACK_MEDIUM_OUTER_RATIO   1386
#define SC_TRACK_TURN_INNER_RATIO     0
#define SC_TRACK_TURN_OUTER_RATIO     1424
#define SC_TRACK_SEARCH_INNER_RATIO   76
#define SC_TRACK_SEARCH_OUTER_RATIO   1194
#define SC_TRACK_LOST_RECOVERY_MS     1000u
#define SC_TRACK_RECOVERY_SAMPLES     3u
#define SC_TRACK_RECOVERY_MIN_TIME_MS 20u
#define SC_TRACK_RECOVERY_MAX_GAP_MS  30u

static int32_t sc_track_speed_limit(const sc_track_controller_t *controller)
{
  int64_t limit = (int64_t)controller->cruise_mm_s * 3;

  return (limit > INT32_MAX) ? INT32_MAX : (int32_t)limit;
}

static int32_t sc_track_scaled_speed(const sc_track_controller_t *controller,
                                     uint32_t ratio_permille)
{
  int64_t scaled =
    (((int64_t)controller->cruise_mm_s * ratio_permille) +
     (SC_TRACK_RATIO_SCALE / 2)) /
    SC_TRACK_RATIO_SCALE;
  int32_t limit = sc_track_speed_limit(controller);

  if (scaled < 0)
  {
    return 0;
  }
  if (scaled > limit)
  {
    return limit;
  }
  return (int32_t)scaled;
}

static void sc_track_set_target(sc_track_controller_t *controller,
                                uint32_t left_ratio,
                                uint32_t right_ratio,
                                sc_wheel_target_t *target_out)
{
  target_out->left_mm_s =
    sc_track_scaled_speed(controller, left_ratio);
  target_out->right_mm_s =
    sc_track_scaled_speed(controller, right_ratio);
}

static void sc_track_clear_target(sc_wheel_target_t *target_out)
{
  target_out->left_mm_s = 0;
  target_out->right_mm_s = 0;
}

static void sc_track_reset_recovery_candidate(
  sc_track_controller_t *controller)
{
  controller->recovery_first_sample_time_ms = 0u;
  controller->recovery_last_sample_time_ms = 0u;
  controller->recovery_last_sequence = 0u;
  controller->recovery_sample_count = 0u;
  controller->recovery_has_sequence = 0u;
}

void sc_track_controller_init(sc_track_controller_t *controller,
                              int32_t cruise_mm_s)
{
  if (controller == NULL)
  {
    return;
  }
  controller->cruise_mm_s = (cruise_mm_s > 0) ? cruise_mm_s : 0;
  controller->last_turn_direction = 0;
  controller->lost_since_ms = 0u;
  controller->recovering = 0u;
  sc_track_reset_recovery_candidate(controller);
}

uint8_t sc_track_start_eligible(const sc_line_state_t *line)
{
  return (uint8_t)(
    (line != NULL) &&
    (line->sample.valid != 0u) &&
    (line->black_mask == 0x06u));
}

static int8_t sc_track_mask_direction(uint8_t black_mask)
{
  int8_t weighted_position = 0;

  if ((black_mask & 0x01u) != 0u)
  {
    weighted_position -= 3;
  }
  if ((black_mask & 0x02u) != 0u)
  {
    weighted_position--;
  }
  if ((black_mask & 0x04u) != 0u)
  {
    weighted_position++;
  }
  if ((black_mask & 0x08u) != 0u)
  {
    weighted_position += 3;
  }
  return (weighted_position > 0) ? 1 :
    ((weighted_position < 0) ? -1 : 0);
}

static sc_track_status_t sc_track_output_search(
  sc_track_controller_t *controller, sc_wheel_target_t *target_out)
{
  if (controller->last_turn_direction < 0)
  {
    sc_track_set_target(controller, SC_TRACK_SEARCH_INNER_RATIO,
                        SC_TRACK_SEARCH_OUTER_RATIO, target_out);
  }
  else if (controller->last_turn_direction > 0)
  {
    sc_track_set_target(controller, SC_TRACK_SEARCH_OUTER_RATIO,
                        SC_TRACK_SEARCH_INNER_RATIO, target_out);
  }
  else
  {
    sc_track_clear_target(target_out);
    return SC_TRACK_ERR_LOST;
  }
  return SC_TRACK_RECOVERING;
}

static sc_track_status_t sc_track_follow_ordinary(
  sc_track_controller_t *controller, uint8_t black_mask,
  sc_wheel_target_t *target_out)
{
  int8_t turn_direction;

  switch (black_mask)
  {
    case 0x06u:
    case 0x0Fu:
      sc_track_set_target(controller, SC_TRACK_CENTER_RATIO,
                          SC_TRACK_CENTER_RATIO, target_out);
      controller->last_turn_direction = 0;
      return SC_TRACK_OK;

    case 0x02u:
      sc_track_set_target(controller, SC_TRACK_FINE_INNER_RATIO,
                          SC_TRACK_FINE_OUTER_RATIO, target_out);
      controller->last_turn_direction = -1;
      return SC_TRACK_OK;

    case 0x04u:
      sc_track_set_target(controller, SC_TRACK_FINE_OUTER_RATIO,
                          SC_TRACK_FINE_INNER_RATIO, target_out);
      controller->last_turn_direction = 1;
      return SC_TRACK_OK;

    case 0x03u:
      sc_track_set_target(controller, SC_TRACK_MEDIUM_INNER_RATIO,
                          SC_TRACK_MEDIUM_OUTER_RATIO, target_out);
      controller->last_turn_direction = -1;
      return SC_TRACK_OK;

    case 0x0Cu:
      sc_track_set_target(controller, SC_TRACK_MEDIUM_OUTER_RATIO,
                          SC_TRACK_MEDIUM_INNER_RATIO, target_out);
      controller->last_turn_direction = 1;
      return SC_TRACK_OK;

    case 0x01u:
    case 0x07u:
      sc_track_set_target(controller, SC_TRACK_TURN_INNER_RATIO,
                          SC_TRACK_TURN_OUTER_RATIO, target_out);
      controller->last_turn_direction = -1;
      return SC_TRACK_OK;

    case 0x08u:
    case 0x0Eu:
      sc_track_set_target(controller, SC_TRACK_TURN_OUTER_RATIO,
                          SC_TRACK_TURN_INNER_RATIO, target_out);
      controller->last_turn_direction = 1;
      return SC_TRACK_OK;

    default:
      turn_direction = sc_track_mask_direction(black_mask);
      if (turn_direction < 0)
      {
        sc_track_set_target(controller, SC_TRACK_FINE_INNER_RATIO,
                            SC_TRACK_FINE_OUTER_RATIO, target_out);
      }
      else if (turn_direction > 0)
      {
        sc_track_set_target(controller, SC_TRACK_FINE_OUTER_RATIO,
                            SC_TRACK_FINE_INNER_RATIO, target_out);
      }
      else
      {
        sc_track_set_target(controller, SC_TRACK_CENTER_RATIO,
                            SC_TRACK_CENTER_RATIO, target_out);
      }
      controller->last_turn_direction = turn_direction;
      return SC_TRACK_OK;
  }
}

static uint8_t sc_track_recovery_confirm(
  sc_track_controller_t *controller, const sc_line_state_t *line)
{
  uint8_t fresh =
    (uint8_t)((controller->recovery_has_sequence == 0u) ||
              (line->sample.sequence !=
               controller->recovery_last_sequence));
  uint32_t sample_gap_ms;

  if (fresh == 0u)
  {
    sc_track_reset_recovery_candidate(controller);
    return 0u;
  }

  if (controller->recovery_sample_count != 0u)
  {
    sample_gap_ms =
      (uint32_t)(line->sample.sample_time_ms -
                 controller->recovery_last_sample_time_ms);
    if ((sample_gap_ms == 0u) ||
        (sample_gap_ms > SC_TRACK_RECOVERY_MAX_GAP_MS))
    {
      sc_track_reset_recovery_candidate(controller);
    }
  }
  if (controller->recovery_sample_count == 0u)
  {
    controller->recovery_first_sample_time_ms =
      line->sample.sample_time_ms;
  }
  if (controller->recovery_sample_count < UINT8_MAX)
  {
    controller->recovery_sample_count++;
  }
  controller->recovery_last_sample_time_ms =
    line->sample.sample_time_ms;
  controller->recovery_last_sequence = line->sample.sequence;
  controller->recovery_has_sequence = 1u;

  if ((controller->recovery_sample_count >= SC_TRACK_RECOVERY_SAMPLES) &&
      ((uint32_t)(line->sample.sample_time_ms -
                  controller->recovery_first_sample_time_ms) >=
       SC_TRACK_RECOVERY_MIN_TIME_MS))
  {
    controller->recovering = 0u;
    sc_track_reset_recovery_candidate(controller);
    return 1u;
  }
  return 0u;
}

sc_track_status_t sc_track_controller_step(sc_track_controller_t *controller,
                                           const sc_line_state_t *line,
                                           uint32_t now_ms,
                                           sc_wheel_target_t *target_out)
{
  if ((controller == NULL) || (line == NULL) || (target_out == NULL))
  {
    return SC_TRACK_ERR_ARGUMENT;
  }

  sc_track_clear_target(target_out);
  if (line->sample.valid == 0u)
  {
    sc_track_reset_recovery_candidate(controller);
    return SC_TRACK_ERR_INVALID_LINE;
  }
  if ((line->black_mask & 0xF0u) != 0u)
  {
    sc_track_reset_recovery_candidate(controller);
    return SC_TRACK_ERR_INVALID_LINE;
  }

  if (controller->recovering != 0u)
  {
    if ((uint32_t)(now_ms - controller->lost_since_ms) >=
        SC_TRACK_LOST_RECOVERY_MS)
    {
      sc_track_reset_recovery_candidate(controller);
      return SC_TRACK_ERR_LOST;
    }

    if (line->black_mask == 0x00u)
    {
      sc_track_reset_recovery_candidate(controller);
      return sc_track_output_search(controller, target_out);
    }

    if (sc_track_recovery_confirm(controller, line) == 0u)
    {
      return sc_track_output_search(controller, target_out);
    }
    return sc_track_follow_ordinary(controller, line->black_mask,
                                    target_out);
  }

  if (line->black_mask == 0x00u)
  {
    controller->recovering = 1u;
    controller->lost_since_ms = now_ms;
    sc_track_reset_recovery_candidate(controller);
    return sc_track_output_search(controller, target_out);
  }

  return sc_track_follow_ordinary(controller, line->black_mask, target_out);
}
