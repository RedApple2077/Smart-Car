/**
  ******************************************************************************
  * @file    sc_track.c
  * @brief   四路红外离散循迹的低速差速控制策略实现。
  ******************************************************************************
  */

#include "sc_track.h"

#include <stddef.h>

#define SC_TRACK_SPEED_SCALE_NUM         3817
#define SC_TRACK_SPEED_SCALE_DEN         2500
#define SC_TRACK_SCALE(value) \
  (((value) * SC_TRACK_SPEED_SCALE_NUM) / SC_TRACK_SPEED_SCALE_DEN)

/* All motion cases use the same scale factor to preserve steering ratios. */
#define SC_TRACK_FORWARD_PERMILLE       SC_TRACK_SCALE(260)
/* At the higher cruise speed, widen the differential response by error size.
   This returns the line to the O2/O3 center more quickly. */
#define SC_TRACK_FINE_INNER_PERMILLE    SC_TRACK_SCALE(145)
#define SC_TRACK_FINE_OUTER_PERMILLE    SC_TRACK_SCALE(345)
#define SC_TRACK_MEDIUM_INNER_PERMILLE  SC_TRACK_SCALE(70)
#define SC_TRACK_MEDIUM_OUTER_PERMILLE  SC_TRACK_SCALE(360)
#define SC_TRACK_TURN_INNER_PERMILLE    0
#define SC_TRACK_TURN_OUTER_PERMILLE    SC_TRACK_SCALE(370)
/* A tight, low-forward-speed arc reacquires a nearby line before the car
   can trace a large circle after a momentary all-white sample. */
#define SC_TRACK_SEARCH_INNER_PERMILLE  SC_TRACK_SCALE(20)
#define SC_TRACK_SEARCH_OUTER_PERMILLE  SC_TRACK_SCALE(310)
#define SC_TRACK_LOST_RECOVERY_MS      1000u

static void sc_track_set_command(sc_track_command_t *command,
                                 int16_t left,
                                 int16_t right)
{
  command->left_permille = left;
  command->right_permille = right;
}

void sc_track_controller_init(sc_track_controller_t *controller)
{
  if (controller == NULL)
  {
    return;
  }
  controller->last_turn_direction = 0;
  controller->lost_since_ms = 0u;
  controller->recovering = 0u;
}

static void sc_track_finish_valid_sample(sc_track_controller_t *controller,
                                         int8_t turn_direction)
{
  if (turn_direction != 0)
  {
    controller->last_turn_direction = turn_direction;
  }
  controller->recovering = 0u;
}

static sc_track_status_t sc_track_recover_lost_line(
  sc_track_controller_t *controller, uint32_t now_ms,
  sc_track_command_t *command)
{
  if (controller->recovering == 0u)
  {
    controller->recovering = 1u;
    controller->lost_since_ms = now_ms;
  }
  if ((uint32_t)(now_ms - controller->lost_since_ms) >=
      SC_TRACK_LOST_RECOVERY_MS)
  {
    return SC_TRACK_ERR_LOST;
  }

  if (controller->last_turn_direction < 0)
  {
    sc_track_set_command(command, SC_TRACK_SEARCH_INNER_PERMILLE,
                         SC_TRACK_SEARCH_OUTER_PERMILLE);
  }
  else if (controller->last_turn_direction > 0)
  {
    sc_track_set_command(command, SC_TRACK_SEARCH_OUTER_PERMILLE,
                         SC_TRACK_SEARCH_INNER_PERMILLE);
  }
  else
  {
    sc_track_set_command(command, SC_TRACK_FORWARD_PERMILLE,
                         SC_TRACK_FORWARD_PERMILLE);
  }
  return SC_TRACK_RECOVERING;
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

static sc_track_status_t sc_track_follow_ambiguous_mask(
  sc_track_controller_t *controller, uint8_t black_mask,
  sc_track_command_t *command)
{
  int8_t turn_direction = sc_track_mask_direction(black_mask);

  if (turn_direction < 0)
  {
    sc_track_set_command(command, SC_TRACK_FINE_INNER_PERMILLE,
                         SC_TRACK_FINE_OUTER_PERMILLE);
  }
  else if (turn_direction > 0)
  {
    sc_track_set_command(command, SC_TRACK_FINE_OUTER_PERMILLE,
                         SC_TRACK_FINE_INNER_PERMILLE);
  }
  else
  {
    sc_track_set_command(command, SC_TRACK_FORWARD_PERMILLE,
                         SC_TRACK_FORWARD_PERMILLE);
  }
  sc_track_finish_valid_sample(controller, turn_direction);
  return SC_TRACK_OK;
}

sc_track_status_t sc_track_controller_step(sc_track_controller_t *controller,
                                           const sc_line_state_t *line,
                                           uint32_t now_ms,
                                           sc_track_command_t *command)
{
  if ((controller == NULL) || (line == NULL) || (command == NULL))
  {
    return SC_TRACK_ERR_ARGUMENT;
  }

  sc_track_set_command(command, 0, 0);
  if (line->sample.valid == 0u)
  {
    return SC_TRACK_ERR_INVALID_LINE;
  }
  if ((line->black_mask & 0xF0u) != 0u)
  {
    return SC_TRACK_ERR_INVALID_LINE;
  }

  /* Use the bit mask directly so contiguous two-sensor coverage remains
     steerable even though the generic semantic service labels it unknown. */
  switch (line->black_mask)
  {
    case 0x06u:
    case 0x0Fu: /* Four sensors on black: crossroad, continue straight. */
      sc_track_set_command(command, SC_TRACK_FORWARD_PERMILLE,
                           SC_TRACK_FORWARD_PERMILLE);
      sc_track_finish_valid_sample(controller, 0);
      return SC_TRACK_OK;

    case 0x02u:
      sc_track_set_command(command, SC_TRACK_FINE_INNER_PERMILLE,
                           SC_TRACK_FINE_OUTER_PERMILLE);
      sc_track_finish_valid_sample(controller, -1);
      return SC_TRACK_OK;

    case 0x04u:
      sc_track_set_command(command, SC_TRACK_FINE_OUTER_PERMILLE,
                           SC_TRACK_FINE_INNER_PERMILLE);
      sc_track_finish_valid_sample(controller, 1);
      return SC_TRACK_OK;

    case 0x03u:
      sc_track_set_command(command, SC_TRACK_MEDIUM_INNER_PERMILLE,
                           SC_TRACK_MEDIUM_OUTER_PERMILLE);
      sc_track_finish_valid_sample(controller, -1);
      return SC_TRACK_OK;

    case 0x0Cu:
      sc_track_set_command(command, SC_TRACK_MEDIUM_OUTER_PERMILLE,
                           SC_TRACK_MEDIUM_INNER_PERMILLE);
      sc_track_finish_valid_sample(controller, 1);
      return SC_TRACK_OK;

    case 0x01u:
    case 0x07u:
      sc_track_set_command(command, SC_TRACK_TURN_INNER_PERMILLE,
                           SC_TRACK_TURN_OUTER_PERMILLE);
      sc_track_finish_valid_sample(controller, -1);
      return SC_TRACK_OK;

    case 0x08u:
    case 0x0Eu:
      sc_track_set_command(command, SC_TRACK_TURN_OUTER_PERMILLE,
                           SC_TRACK_TURN_INNER_PERMILLE);
      sc_track_finish_valid_sample(controller, 1);
      return SC_TRACK_OK;

    case 0x00u:
      return sc_track_recover_lost_line(controller, now_ms, command);

    default:
      return sc_track_follow_ambiguous_mask(controller, line->black_mask,
                                            command);
  }
}
