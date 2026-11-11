/**
  ******************************************************************************
  * @file    sc_line_service.c
  * @brief   LF04 原始电平到黑线掩码和线路语义的纯软件转换。
  ******************************************************************************
  */

#include "sc_line_service.h"

#include <stddef.h>

static sc_line_state_t s_latest;
static uint8_t s_has_snapshot;

static sc_line_class_t sc_line_service_classify(uint8_t black_mask)
{
  switch (black_mask)
  {
    case 0x00u:
      return SC_LINE_LOST_CANDIDATE;
    case 0x06u:
      return SC_LINE_STRAIGHT;
    case 0x07u:
      return SC_LINE_LEFT_RIGHT_ANGLE_CANDIDATE;
    case 0x0Eu:
      return SC_LINE_RIGHT_RIGHT_ANGLE_CANDIDATE;
    case 0x01u:
      return SC_LINE_LEFT_LARGE_BEND_CANDIDATE;
    case 0x08u:
      return SC_LINE_RIGHT_LARGE_BEND_CANDIDATE;
    case 0x02u:
      return SC_LINE_LEFT_FINE_ADJUST_CANDIDATE;
    case 0x04u:
      return SC_LINE_RIGHT_FINE_ADJUST_CANDIDATE;
    case 0x0Fu:
      return SC_LINE_JUNCTION_CANDIDATE;
    default:
      return SC_LINE_UNKNOWN_PATTERN;
  }
}

sc_line_status_t sc_line_service_update(const sc_line_sample_t *sample)
{
  if (sample == NULL)
  {
    return SC_LINE_ERR_ARGUMENT;
  }

  s_latest.sample = *sample;
  s_has_snapshot = 1u;
  if ((sample->valid == 0u) || ((sample->raw_levels & 0xF0u) != 0u))
  {
    s_latest.sample.valid = 0u;
    s_latest.black_mask = 0u;
    s_latest.semantic_state = SC_LINE_INVALID;
    return SC_LINE_ERR_INVALID_DATA;
  }

  s_latest.black_mask = (uint8_t)(~sample->raw_levels) & 0x0Fu;
  s_latest.semantic_state =
    sc_line_service_classify(s_latest.black_mask);
  return SC_LINE_OK;
}

sc_line_status_t sc_line_service_get_latest(sc_line_state_t *state)
{
  if (state == NULL)
  {
    return SC_LINE_ERR_ARGUMENT;
  }
  if (s_has_snapshot == 0u)
  {
    return SC_LINE_ERR_NOT_READY;
  }
  *state = s_latest;
  return SC_LINE_OK;
}
