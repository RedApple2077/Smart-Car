#include "fake_app.h"

#include <stdio.h>
#include <string.h>

#include "sc_app.h"
#include "sc_motor_defaults.h"

#define CHECK(expression)                                                     \
  do                                                                          \
  {                                                                           \
    if (!(expression))                                                        \
    {                                                                         \
      fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__,     \
              #expression);                                                   \
      return 1;                                                               \
    }                                                                         \
  } while (0)

static sc_app_config_t valid_config(void)
{
  sc_app_config_t config;

  sc_app_get_default_config(&config);
  config.calibration_valid = 1u;
  config.tuning_valid = 1u;
  config.cruise_mm_s = 396;
  config.speed_controller.left.kp_milli = 0;
  config.speed_controller.left.ki_milli = 0;
  config.speed_controller.left.kd_milli = 0;
  config.speed_controller.left.ff_milli = 1000;
  config.speed_controller.right = config.speed_controller.left;
  return config;
}

static sc_app_config_t negative_command_config(void)
{
  sc_app_config_t config = valid_config();

  config.speed_controller.left.kp_milli = 0;
  config.speed_controller.left.ki_milli = 0;
  config.speed_controller.left.kd_milli = 0;
  config.speed_controller.left.ff_milli = -1000;
  config.speed_controller.right.ff_milli = 1000;
  return config;
}

static int init_app(sc_app_t *app, const sc_app_config_t *config)
{
  fake_app_reset();
  fake_line_set_mask(0x06u);
  fake_encoder_set_deltas(-1, 1);
  CHECK(sc_app_init(app, config, 0u) == SC_APP_OK);
  CHECK(app->phase == SC_APP_PHASE_IDLE);
  return 0;
}

static int pump_ready(sc_app_t *app, uint32_t end_ms)
{
  uint32_t now_ms;

  for (now_ms = 0u; now_ms <= end_ms; now_ms += 10u)
  {
    CHECK(sc_app_service(app, now_ms) == SC_APP_OK);
  }
  CHECK(app->phase == SC_APP_PHASE_READY);
  return 0;
}

static int start_active(sc_app_t *app)
{
  CHECK(pump_ready(app, 60u) == 0);
  fake_button_queue(SC_BUTTON_EVENT_PRESSED);
  CHECK(sc_app_service(app, 70u) == SC_APP_OK);
  fake_tb_clear_events();
  fake_button_queue(SC_BUTTON_EVENT_RELEASED);
  CHECK(sc_app_service(app, 80u) == SC_APP_OK);
  CHECK(app->phase == SC_APP_PHASE_ACTIVE);
  CHECK(app->software_stby == 1u);
  return 0;
}

static int start_recovering_search(sc_app_t *app)
{
  sc_app_config_t config = valid_config();

  config.cruise_mm_s = 400;
  CHECK(init_app(app, &config) == 0);
  CHECK(start_active(app) == 0);
  fake_line_set_mask(0x02u);
  fake_encoder_set_deltas(-20, 20);
  CHECK(sc_app_service(app, 90u) == SC_APP_OK);
  CHECK(app->phase == SC_APP_PHASE_ACTIVE);
  CHECK(app->track.last_turn_direction == -1);

  fake_line_set_mask(0x00u);
  fake_encoder_set_deltas(-10, 30);
  CHECK(sc_app_service(app, 100u) == SC_APP_OK);
  CHECK(app->phase == SC_APP_PHASE_ACTIVE);
  CHECK(app->track.recovering == 1u);
  CHECK(app->track.lost_since_ms == 100u);
  CHECK(app->target.left_mm_s == 120);
  CHECK(app->target.right_mm_s == 360);
  CHECK(app->software_stby == 1u);
  CHECK(fake_tb_state()->standby == 1u);
  return 0;
}

static int start_full_black_recovering_search(sc_app_t *app)
{
  CHECK(start_recovering_search(app) == 0);
  fake_line_set_mask(0x0Fu);
  fake_encoder_set_deltas(-10, 30);
  CHECK(sc_app_service(app, 110u) == SC_APP_OK);
  CHECK(app->phase == SC_APP_PHASE_ACTIVE);
  CHECK(app->fault_bits == 0u);
  CHECK(app->track.recovering == 1u);
  CHECK(app->track.last_turn_direction == -1);
  CHECK(app->track.lost_since_ms == 100u);
  CHECK(app->track.recovery_sample_count == 0u);
  CHECK(app->track.crossing_entry_sample_count == 0u);
  CHECK(app->track.crossing_active == 0u);
  CHECK(app->target.left_mm_s == 120);
  CHECK(app->target.right_mm_s == 360);
  CHECK(app->software_stby == 1u);
  CHECK(fake_tb_state()->enabled == 1u);
  CHECK(fake_tb_state()->standby == 1u);
  return 0;
}

static int check_safe_fault(const sc_app_t *app, uint32_t expected_faults,
                            sc_app_stop_reason_t expected_reason)
{
  CHECK(app->phase == SC_APP_PHASE_FAULT);
  CHECK(app->fault_bits == expected_faults);
  CHECK(app->stop_reason == expected_reason);
  CHECK(app->target.left_mm_s == 0);
  CHECK(app->target.right_mm_s == 0);
  CHECK(app->speed_output.left_permille == 0);
  CHECK(app->speed_output.right_permille == 0);
  CHECK(app->software_stby == 0u);
  CHECK(fake_tb_state()->enabled == 0u);
  CHECK(fake_tb_state()->standby == 0u);
  CHECK(fake_tb_state()->left_command == 0);
  CHECK(fake_tb_state()->right_command == 0);
  return 0;
}

static int test_defaults_match_bench_state(void)
{
  sc_app_config_t config;

  sc_app_get_default_config(&config);
  CHECK(config.calibration_valid == 1u);
  CHECK(config.tuning_valid == 1u);
  CHECK(config.cruise_mm_s == 400);
  CHECK(config.wheel_calibration.left_counts_per_rev == 1061u);
  CHECK(config.wheel_calibration.right_counts_per_rev == 1061u);
  CHECK(config.wheel_calibration.left_wheel_diameter_um == 48600);
  CHECK(config.wheel_calibration.right_wheel_diameter_um == 48600);
  CHECK(config.speed_controller.left.ff_milli == 810);
  CHECK(config.speed_controller.left.kp_milli == 100);
  CHECK(config.speed_controller.left.ki_milli == 750);
  CHECK(config.speed_controller.left.kd_milli == 0);
  CHECK(config.speed_controller.right.ff_milli == 815);
  CHECK(config.speed_controller.right.kp_milli == 100);
  CHECK(config.speed_controller.right.ki_milli == 750);
  CHECK(config.speed_controller.right.kd_milli == 0);
  CHECK(config.wheel_calibration.left_encoder_sign == -1);
  CHECK(config.wheel_calibration.right_encoder_sign == 1);
  return 0;
}

static int test_isolated_release_does_not_start(void)
{
  sc_app_config_t config = valid_config();
  sc_app_t app;

  CHECK(init_app(&app, &config) == 0);
  CHECK(pump_ready(&app, 60u) == 0);
  fake_button_queue(SC_BUTTON_EVENT_RELEASED);
  CHECK(sc_app_service(&app, 70u) == SC_APP_OK);
  CHECK(app.phase != SC_APP_PHASE_ACTIVE);
  CHECK(app.software_stby == 0u);
  return 0;
}

static int test_power_on_held_release_does_not_start(void)
{
  sc_app_config_t config = valid_config();
  sc_app_t app;

  CHECK(init_app(&app, &config) == 0);
  CHECK(pump_ready(&app, 60u) == 0);
  /* 真实 Button Driver 在上电按住时不产生 PRESSED，首次释放即为孤立 RELEASED。 */
  fake_button_queue(SC_BUTTON_EVENT_RELEASED);
  CHECK(sc_app_service(&app, 70u) == SC_APP_OK);
  CHECK(app.phase != SC_APP_PHASE_ACTIVE);
  CHECK(app.software_stby == 0u);
  return 0;
}

static int test_center_window_and_start_order(void)
{
  sc_app_config_t config = valid_config();
  sc_app_t app;
  const fake_tb_event_t *event;

  CHECK(init_app(&app, &config) == 0);
  CHECK(pump_ready(&app, 60u) == 0);
  fake_button_queue(SC_BUTTON_EVENT_PRESSED);
  CHECK(sc_app_service(&app, 70u) == SC_APP_OK);
  fake_tb_clear_events();
  fake_button_queue(SC_BUTTON_EVENT_RELEASED);
  CHECK(sc_app_service(&app, 80u) == SC_APP_OK);
  CHECK(app.phase == SC_APP_PHASE_ACTIVE);
  CHECK(fake_tb_event_count() >= 2u);
  event = fake_tb_event_at(0u);
  CHECK((event->kind == FAKE_TB_EVENT_SET_STANDBY) &&
        (event->value == 1u));
  event = fake_tb_event_at(1u);
  CHECK((event->kind == FAKE_TB_EVENT_APPLY) &&
        (event->left == 0) && (event->right == 0));
  CHECK(fake_tb_state()->enabled == 1u);
  CHECK(fake_tb_state()->standby == 1u);
  return 0;
}

static int test_center_interruption_blocks_start(void)
{
  sc_app_config_t config = valid_config();
  sc_app_t app;
  uint32_t now_ms;

  CHECK(init_app(&app, &config) == 0);
  for (now_ms = 0u; now_ms <= 40u; now_ms += 10u)
  {
    CHECK(sc_app_service(&app, now_ms) == SC_APP_OK);
  }
  CHECK(app.center_sample_count == 5u);
  CHECK(app.phase != SC_APP_PHASE_READY);

  fake_button_queue(SC_BUTTON_EVENT_PRESSED);
  fake_line_set_mask(0x00u);
  CHECK(sc_app_service(&app, 50u) == SC_APP_OK);
  fake_button_queue(SC_BUTTON_EVENT_RELEASED);
  CHECK(sc_app_service(&app, 60u) == SC_APP_OK);
  CHECK(app.center_sample_count == 0u);
  CHECK(app.phase != SC_APP_PHASE_ACTIVE);
  CHECK(app.software_stby == 0u);
  return 0;
}

static int test_single_side_feedback_invalid_blocks_start(void)
{
  sc_app_config_t config = valid_config();
  sc_app_t app;
  uint32_t now_ms;

  CHECK(init_app(&app, &config) == 0);
  fake_encoder_set_valid(0u, 1u);
  for (now_ms = 0u; now_ms <= 60u; now_ms += 10u)
  {
    CHECK(sc_app_service(&app, now_ms) == SC_APP_OK);
  }
  CHECK(app.phase == SC_APP_PHASE_IDLE);
  fake_button_queue(SC_BUTTON_EVENT_PRESSED);
  CHECK(sc_app_service(&app, 70u) == SC_APP_OK);
  fake_button_queue(SC_BUTTON_EVENT_RELEASED);
  CHECK(sc_app_service(&app, 80u) == SC_APP_OK);
  CHECK(app.phase != SC_APP_PHASE_ACTIVE);
  CHECK(app.software_stby == 0u);
  return 0;
}

static int test_full_white_and_invalid_config_block_start(void)
{
  sc_app_config_t config = valid_config();
  sc_app_t app;
  uint32_t now_ms;

  fake_app_reset();
  fake_line_set_mask(0x00u);
  fake_encoder_set_deltas(-1, 1);
  CHECK(sc_app_init(&app, &config, 0u) == SC_APP_OK);
  for (now_ms = 0u; now_ms <= 60u; now_ms += 10u)
  {
    CHECK(sc_app_service(&app, now_ms) == SC_APP_OK);
  }
  CHECK(app.phase != SC_APP_PHASE_READY);
  fake_button_queue(SC_BUTTON_EVENT_PRESSED);
  CHECK(sc_app_service(&app, 70u) == SC_APP_OK);
  fake_button_queue(SC_BUTTON_EVENT_RELEASED);
  CHECK(sc_app_service(&app, 80u) == SC_APP_OK);
  CHECK(app.phase != SC_APP_PHASE_ACTIVE);

  config.calibration_valid = 0u;
  config.tuning_valid = 1u;
  CHECK(init_app(&app, &config) == 0);
  for (now_ms = 0u; now_ms <= 60u; now_ms += 10u)
  {
    CHECK(sc_app_service(&app, now_ms) == SC_APP_OK);
  }
  CHECK(app.phase == SC_APP_PHASE_IDLE);
  CHECK(app.phase != SC_APP_PHASE_ACTIVE);
  CHECK(app.center_sample_count >= SC_APP_START_CENTER_SAMPLES);
  CHECK(app.wheel.valid != 0u);
  CHECK(app.config.calibration_valid == 0u);
  CHECK(app.config.tuning_valid == 1u);
  fake_button_queue(SC_BUTTON_EVENT_PRESSED);
  CHECK(sc_app_service(&app, 70u) == SC_APP_OK);
  fake_button_queue(SC_BUTTON_EVENT_RELEASED);
  CHECK(sc_app_service(&app, 80u) == SC_APP_OK);
  CHECK(app.phase == SC_APP_PHASE_IDLE);
  CHECK(app.software_stby == 0u);
  CHECK(app.stop_reason == SC_APP_STOP_START_REJECTED);
  CHECK(fake_tb_state()->standby == 0u);

  config.calibration_valid = 1u;
  config.tuning_valid = 0u;
  CHECK(init_app(&app, &config) == 0);
  for (now_ms = 0u; now_ms <= 60u; now_ms += 10u)
  {
    CHECK(sc_app_service(&app, now_ms) == SC_APP_OK);
  }
  CHECK(app.phase == SC_APP_PHASE_IDLE);
  CHECK(app.center_sample_count >= SC_APP_START_CENTER_SAMPLES);
  CHECK(app.wheel.valid != 0u);
  CHECK(app.config.calibration_valid == 1u);
  CHECK(app.config.tuning_valid == 0u);
  fake_button_queue(SC_BUTTON_EVENT_PRESSED);
  CHECK(sc_app_service(&app, 70u) == SC_APP_OK);
  fake_button_queue(SC_BUTTON_EVENT_RELEASED);
  CHECK(sc_app_service(&app, 80u) == SC_APP_OK);
  CHECK(app.phase == SC_APP_PHASE_IDLE);
  CHECK(app.software_stby == 0u);
  CHECK(app.stop_reason == SC_APP_STOP_START_REJECTED);
  CHECK(fake_tb_state()->standby == 0u);
  return 0;
}

static int test_long_press_stops_then_new_click_restarts(void)
{
  sc_app_config_t config = valid_config();
  sc_app_t app;

  CHECK(init_app(&app, &config) == 0);
  CHECK(start_active(&app) == 0);
  fake_button_queue(SC_BUTTON_EVENT_LONG_PRESS);
  CHECK(sc_app_service(&app, 90u) == SC_APP_OK);
  CHECK(app.phase == SC_APP_PHASE_STOPPING);
  CHECK(app.software_stby == 0u);
  CHECK(fake_emergency_count() > 0u);

  fake_button_queue(SC_BUTTON_EVENT_RELEASED);
  CHECK(sc_app_service(&app, 100u) == SC_APP_OK);
  CHECK(app.phase != SC_APP_PHASE_ACTIVE);

  fake_button_queue(SC_BUTTON_EVENT_PRESSED);
  CHECK(sc_app_service(&app, 110u) == SC_APP_OK);
  fake_button_queue(SC_BUTTON_EVENT_RELEASED);
  CHECK(sc_app_service(&app, 120u) == SC_APP_OK);
  CHECK(app.phase == SC_APP_PHASE_ACTIVE);
  return 0;
}

static int test_active_all_white_faults(void)
{
  sc_app_config_t config = valid_config();
  sc_app_t app;

  CHECK(init_app(&app, &config) == 0);
  CHECK(start_active(&app) == 0);
  fake_line_set_mask(0x00u);
  CHECK(sc_app_service(&app, 90u) == SC_APP_FAULT_RECOVERABLE);
  CHECK(app.phase == SC_APP_PHASE_FAULT);
  CHECK((app.fault_bits & SC_APP_FAULT_LINE_LOST) != 0u);
  CHECK(fake_tb_state()->standby == 0u);
  return 0;
}

static int test_known_direction_white_search_stays_active(void)
{
  sc_app_config_t config = valid_config();
  sc_app_t app;
  uint32_t now_ms;

  config.cruise_mm_s = 400;
  CHECK(init_app(&app, &config) == 0);
  CHECK(start_active(&app) == 0);
  fake_line_set_mask(0x02u);
  fake_encoder_set_deltas(-20, 20);
  CHECK(sc_app_service(&app, 90u) == SC_APP_OK);
  CHECK(app.track.last_turn_direction == -1);

  fake_line_set_mask(0x00u);
  for (now_ms = 100u; now_ms <= 1110u; now_ms += 10u)
  {
    CHECK(sc_app_service(&app, now_ms) == SC_APP_OK);
    CHECK(app.phase == SC_APP_PHASE_ACTIVE);
    CHECK((app.fault_bits & SC_APP_FAULT_LINE_LOST) == 0u);
    CHECK(app.track.recovering == 1u);
    CHECK(app.track.lost_since_ms == 100u);
    CHECK(app.software_stby == 1u);
    CHECK(fake_tb_state()->standby == 1u);
  }
  CHECK(app.target.left_mm_s == 120);
  CHECK(app.target.right_mm_s == 360);
  return 0;
}

static int test_recovering_junction_search_stays_active(void)
{
  static const uint8_t junction_masks[] = {
    0x07u, 0x0Bu, 0x0Du, 0x0Eu, 0x0Fu
  };
  sc_app_t app;
  uint32_t index;
  uint32_t now_ms;

  CHECK(start_recovering_search(&app) == 0);
  for (index = 0u;
       index < (sizeof(junction_masks) / sizeof(junction_masks[0]));
       index++)
  {
    now_ms = 110u + (index * 10u);
    fake_line_set_mask(junction_masks[index]);
    fake_encoder_set_deltas(-10, 30);
    CHECK(sc_app_service(&app, now_ms) == SC_APP_OK);
    CHECK(app.phase == SC_APP_PHASE_ACTIVE);
    CHECK(app.fault_bits == 0u);
    CHECK(app.track.recovering == 1u);
    CHECK(app.track.last_turn_direction == -1);
    CHECK(app.track.lost_since_ms == 100u);
    CHECK(app.track.recovery_sample_count == 0u);
    CHECK(app.track.crossing_entry_sample_count == 0u);
    CHECK(app.track.crossing_active == 0u);
    CHECK(app.target.left_mm_s == 120);
    CHECK(app.target.right_mm_s == 360);
  }

  fake_line_set_mask(0x02u);
  fake_encoder_set_deltas(-10, 30);
  CHECK(sc_app_service(&app, 160u) == SC_APP_OK);
  CHECK(app.track.recovery_sample_count == 1u);
  CHECK(sc_app_service(&app, 170u) == SC_APP_OK);
  CHECK(app.track.recovery_sample_count == 2u);
  CHECK(sc_app_service(&app, 180u) == SC_APP_OK);
  CHECK(app.track.recovering == 0u);
  CHECK(app.phase == SC_APP_PHASE_ACTIVE);
  CHECK(app.target.left_mm_s == 340);
  CHECK(app.target.right_mm_s == 460);

  fake_line_set_mask(0x0Eu);
  CHECK(sc_app_service(&app, 190u) == SC_APP_OK);
  CHECK(app.track.crossing_entry_sample_count == 1u);
  CHECK(app.track.crossing_active == 0u);
  CHECK(sc_app_service(&app, 200u) == SC_APP_OK);
  CHECK(app.track.crossing_entry_sample_count == 2u);
  CHECK(app.track.crossing_active == 0u);
  CHECK(sc_app_service(&app, 210u) == SC_APP_OK);
  CHECK(app.track.crossing_entry_sample_count == 3u);
  CHECK(app.track.crossing_active == 1u);
  CHECK(app.track.crossing_start_ms == 190u);
  CHECK(app.phase == SC_APP_PHASE_ACTIVE);
  CHECK(app.fault_bits == 0u);
  return 0;
}

static int test_recovering_full_black_timeline_stays_active(void)
{
  sc_app_config_t config = valid_config();
  sc_app_t app;
  sc_app_status_t final_status = SC_APP_OK;
  uint32_t now_ms;
  uint32_t last_now_ms = 0u;

  config.cruise_mm_s = 400;
  CHECK(init_app(&app, &config) == 0);
  CHECK(start_active(&app) == 0);
  fake_line_set_mask(0x02u);
  fake_encoder_set_deltas(-20, 20);
  CHECK(sc_app_service(&app, 90u) == SC_APP_OK);
  CHECK(app.track.last_turn_direction == -1);

  for (now_ms = 100u; now_ms <= 1510u; now_ms += 10u)
  {
    fake_line_set_mask(0x00u);
    fake_encoder_set_deltas(-10, 30);
    final_status = sc_app_service(&app, now_ms);
  }
  for (now_ms = 1520u; now_ms <= 11530u; now_ms += 10u)
  {
    fake_line_set_mask(0x0Fu);
    fake_encoder_set_deltas(-10, 30);
    final_status = sc_app_service(&app, now_ms);
    last_now_ms = now_ms;
  }

  CHECK((last_now_ms == 11530u) && (final_status == SC_APP_OK));
  CHECK(app.phase == SC_APP_PHASE_ACTIVE);
  CHECK(app.fault_bits == 0u);
  CHECK(app.track.recovering == 1u);
  CHECK(app.track.last_turn_direction == -1);
  CHECK(app.track.lost_since_ms == 100u);
  CHECK(app.track.recovery_sample_count == 0u);
  CHECK(app.track.crossing_entry_sample_count == 0u);
  CHECK(app.track.crossing_active == 0u);
  CHECK(app.target.left_mm_s == 120);
  CHECK(app.target.right_mm_s == 360);
  CHECK(app.software_stby == 1u);
  CHECK(fake_tb_state()->enabled == 1u);
  CHECK(fake_tb_state()->standby == 1u);
  return 0;
}

static int test_recovering_search_outer_stall_faults(void)
{
  sc_app_t app;
  sc_app_status_t status = SC_APP_OK;
  uint32_t now_ms;
  uint8_t gate_seen = 0u;

  CHECK(start_full_black_recovering_search(&app) == 0);
  for (now_ms = 120u; now_ms <= 800u; now_ms += 10u)
  {
    fake_line_set_mask(0x0Fu);
    fake_encoder_set_deltas(-20, 0);
    status = sc_app_service(&app, now_ms);
    if (status != SC_APP_OK)
    {
      break;
    }
    CHECK(app.phase == SC_APP_PHASE_ACTIVE);
    CHECK(app.track.recovering == 1u);
    if ((gate_seen == 0u) &&
        (app.target.right_mm_s >= SC_APP_STALL_TARGET_MM_S) &&
        (app.speed_output.right_permille >= SC_APP_STALL_PWM_PERMILLE))
    {
      gate_seen = 1u;
      CHECK(app.stall_active[1] == 1u);
    }
  }
  CHECK(gate_seen != 0u);
  CHECK(status == SC_APP_FAULT_RECOVERABLE);
  CHECK(check_safe_fault(
          &app, SC_APP_FAULT_STALL_RIGHT, SC_APP_STOP_STALL_RIGHT) == 0);
  return 0;
}

static int test_recovering_search_feedback_faults(void)
{
  sc_app_t app;

  CHECK(start_full_black_recovering_search(&app) == 0);
  fake_encoder_freeze(1u);
  fake_line_set_mask(0x0Fu);
  CHECK(sc_app_service(&app, 120u) == SC_APP_OK);
  CHECK(sc_app_service(&app, 130u) == SC_APP_OK);
  CHECK(sc_app_service(&app, 140u) == SC_APP_OK);
  CHECK(sc_app_service(&app, 150u) == SC_APP_FAULT_RECOVERABLE);
  CHECK(check_safe_fault(
          &app, SC_APP_FAULT_FEEDBACK, SC_APP_STOP_FEEDBACK) == 0);

  CHECK(start_full_black_recovering_search(&app) == 0);
  fake_encoder_set_valid(0u, 1u);
  fake_line_set_mask(0x0Fu);
  CHECK(sc_app_service(&app, 120u) == SC_APP_FAULT_RECOVERABLE);
  CHECK(check_safe_fault(
          &app, SC_APP_FAULT_FEEDBACK, SC_APP_STOP_FEEDBACK) == 0);
  return 0;
}

static int test_recovering_search_wrong_direction_fault(void)
{
  sc_app_t app;
  sc_app_status_t status = SC_APP_OK;
  uint32_t now_ms;
  uint8_t gate_seen = 0u;
  uint8_t reverse_index;

  CHECK(start_full_black_recovering_search(&app) == 0);
  for (now_ms = 120u; now_ms <= 200u; now_ms += 10u)
  {
    fake_line_set_mask(0x0Fu);
    fake_encoder_set_deltas(-20, 20);
    status = sc_app_service(&app, now_ms);
    CHECK(status == SC_APP_OK);
    if ((app.target.right_mm_s >= SC_APP_STALL_TARGET_MM_S) &&
        (app.speed_output.right_permille >= SC_APP_STALL_PWM_PERMILLE))
    {
      gate_seen = 1u;
      break;
    }
  }
  CHECK(gate_seen != 0u);
  CHECK(app.track.recovering == 1u);

  for (reverse_index = 0u; reverse_index < SC_APP_REVERSE_SAMPLES;
       reverse_index++)
  {
    now_ms += 10u;
    fake_line_set_mask(0x0Fu);
    fake_encoder_set_deltas(-20, -30);
    status = sc_app_service(&app, now_ms);
    if (reverse_index + 1u < SC_APP_REVERSE_SAMPLES)
    {
      CHECK(status == SC_APP_OK);
    }
  }
  CHECK(status == SC_APP_FAULT_FATAL);
  CHECK(check_safe_fault(
          &app, SC_APP_FAULT_DIRECTION_RIGHT,
          SC_APP_STOP_DIRECTION_RIGHT) == 0);
  return 0;
}

static int test_recovering_search_tb_and_line_faults(void)
{
  sc_app_t app;

  CHECK(start_full_black_recovering_search(&app) == 0);
  fake_tb_fail_apply();
  fake_line_set_mask(0x0Fu);
  CHECK(sc_app_service(&app, 120u) == SC_APP_FAULT_FATAL);
  CHECK(check_safe_fault(
          &app, SC_APP_FAULT_TB6612, SC_APP_STOP_TB6612) == 0);

  CHECK(start_full_black_recovering_search(&app) == 0);
  fake_line_fail_next();
  CHECK(sc_app_service(&app, 120u) == SC_APP_FAULT_RECOVERABLE);
  CHECK(check_safe_fault(
          &app, SC_APP_FAULT_LINE_INVALID,
          SC_APP_STOP_LINE_INVALID) == 0);
  return 0;
}

static int test_recoverable_health_without_new_press_no_restart(void)
{
  sc_app_config_t config = valid_config();
  sc_app_t app;
  uint32_t now_ms;

  CHECK(init_app(&app, &config) == 0);
  CHECK(start_active(&app) == 0);
  fake_line_fail_next();
  CHECK(sc_app_service(&app, 90u) == SC_APP_FAULT_RECOVERABLE);
  CHECK(app.phase == SC_APP_PHASE_FAULT);

  fake_line_set_mask(0x06u);
  for (now_ms = 100u; now_ms <= 360u; now_ms += 10u)
  {
    (void)sc_app_service(&app, now_ms);
  }
  CHECK(app.phase == SC_APP_PHASE_READY);
  CHECK(app.phase != SC_APP_PHASE_ACTIVE);

  fake_button_queue(SC_BUTTON_EVENT_PRESSED);
  CHECK(sc_app_service(&app, 370u) == SC_APP_OK);
  fake_button_queue(SC_BUTTON_EVENT_RELEASED);
  CHECK(sc_app_service(&app, 380u) == SC_APP_OK);
  CHECK(app.phase == SC_APP_PHASE_ACTIVE);
  return 0;
}

static int test_line_lost_recovery_requires_center(void)
{
  sc_app_config_t config = valid_config();
  sc_app_t app;
  uint32_t now_ms;

  CHECK(init_app(&app, &config) == 0);
  CHECK(start_active(&app) == 0);
  fake_line_set_mask(0x00u);
  CHECK(sc_app_service(&app, 90u) == SC_APP_FAULT_RECOVERABLE);
  CHECK((app.fault_bits & SC_APP_FAULT_LINE_LOST) != 0u);

  for (now_ms = 100u; now_ms <= 290u; now_ms += 10u)
  {
    CHECK(sc_app_service(&app, now_ms) == SC_APP_FAULT_RECOVERABLE);
  }
  CHECK(app.phase == SC_APP_PHASE_FAULT);

  fake_line_set_mask(0x06u);
  for (now_ms = 300u; now_ms <= 540u; now_ms += 10u)
  {
    (void)sc_app_service(&app, now_ms);
  }
  CHECK(app.phase == SC_APP_PHASE_FAULT);
  CHECK((app.fault_bits & SC_APP_FAULT_LINE_LOST) != 0u);

  CHECK(sc_app_service(&app, 550u) == SC_APP_OK);
  CHECK(app.phase != SC_APP_PHASE_FAULT);
  CHECK((app.fault_bits & SC_APP_FAULT_LINE_LOST) == 0u);
  return 0;
}

static int test_feedback_recovery_requires_nonwhite_line(void)
{
  sc_app_config_t config = valid_config();
  sc_app_t app;
  uint32_t now_ms;

  CHECK(init_app(&app, &config) == 0);
  CHECK(start_active(&app) == 0);
  fake_encoder_set_lost_sync(1u, 0u);
  CHECK(sc_app_service(&app, 90u) == SC_APP_FAULT_RECOVERABLE);
  CHECK((app.fault_bits & SC_APP_FAULT_FEEDBACK) != 0u);

  fake_encoder_set_lost_sync(0u, 0u);
  fake_line_set_mask(0x00u);
  for (now_ms = 100u; now_ms <= 400u; now_ms += 10u)
  {
    CHECK(sc_app_service(&app, now_ms) == SC_APP_FAULT_RECOVERABLE);
  }
  CHECK(app.phase == SC_APP_PHASE_FAULT);
  CHECK((app.fault_bits & SC_APP_FAULT_FEEDBACK) != 0u);
  return 0;
}

static int test_tb_fault_is_fatal(void)
{
  sc_app_config_t config = valid_config();
  sc_app_t app;
  uint32_t now_ms;

  CHECK(init_app(&app, &config) == 0);
  CHECK(start_active(&app) == 0);
  fake_tb_fail_apply();
  CHECK(sc_app_service(&app, 90u) == SC_APP_FAULT_FATAL);
  CHECK((app.fault_bits & SC_APP_FAULT_TB6612) != 0u);
  for (now_ms = 100u; now_ms <= 320u; now_ms += 10u)
  {
    (void)sc_app_service(&app, now_ms);
  }
  fake_button_queue(SC_BUTTON_EVENT_PRESSED);
  (void)sc_app_service(&app, 330u);
  fake_button_queue(SC_BUTTON_EVENT_RELEASED);
  (void)sc_app_service(&app, 340u);
  CHECK(app.phase == SC_APP_PHASE_FAULT);
  CHECK(app.phase != SC_APP_PHASE_ACTIVE);
  return 0;
}

static int test_stall_gate_thresholds(void)
{
  CHECK(sc_app_stall_gate(0, 150) == 0u);
  CHECK(sc_app_stall_gate(49, 150) == 0u);
  CHECK(sc_app_stall_gate(50, 149) == 0u);
  CHECK(sc_app_stall_gate(50, 150) == 1u);
  CHECK(sc_app_stall_gate(50, -150) == 1u);
  return 0;
}

static int test_single_wheel_stall(void)
{
  sc_app_config_t config = valid_config();
  sc_app_t app;
  uint32_t now_ms;

  CHECK(init_app(&app, &config) == 0);
  CHECK(start_active(&app) == 0);
  fake_encoder_set_deltas(0, 20);
  for (now_ms = 90u; now_ms <= 460u; now_ms += 10u)
  {
    (void)sc_app_service(&app, now_ms);
  }
  CHECK(app.phase == SC_APP_PHASE_FAULT);
  CHECK((app.fault_bits & SC_APP_FAULT_STALL_LEFT) != 0u);
  CHECK((app.fault_bits & SC_APP_FAULT_STALL_RIGHT) == 0u);
  return 0;
}

static int test_jittery_counts_fail_progress_window(void)
{
  sc_app_config_t config = valid_config();
  sc_app_t app;
  uint32_t now_ms;

  CHECK(init_app(&app, &config) == 0);
  CHECK(start_active(&app) == 0);
  for (now_ms = 90u; now_ms <= 460u; now_ms += 10u)
  {
    int32_t left_delta = (((now_ms / 10u) & 1u) == 0u) ? -1 : 0;

    fake_encoder_set_deltas(left_delta, 20);
    (void)sc_app_service(&app, now_ms);
  }
  CHECK(app.phase == SC_APP_PHASE_FAULT);
  CHECK((app.fault_bits & SC_APP_FAULT_STALL_LEFT) != 0u);
  return 0;
}

static int test_real_progress_passes_stall_window(void)
{
  sc_app_config_t config = valid_config();
  sc_app_t app;
  uint32_t now_ms;

  CHECK(init_app(&app, &config) == 0);
  CHECK(start_active(&app) == 0);
  fake_encoder_set_deltas(-20, 20);
  for (now_ms = 90u; now_ms <= 460u; now_ms += 10u)
  {
    CHECK(sc_app_service(&app, now_ms) == SC_APP_OK);
  }
  CHECK(app.phase == SC_APP_PHASE_ACTIVE);
  CHECK((app.fault_bits &
         (SC_APP_FAULT_STALL_LEFT | SC_APP_FAULT_STALL_RIGHT)) == 0u);
  CHECK(app.stall_window_start_ms[0] != 0u);
  CHECK(app.stall_window_start_ms[1] != 0u);
  return 0;
}

static int test_inner_zero_target_no_stall(void)
{
  sc_app_config_t config = valid_config();
  sc_app_t app;
  uint32_t now_ms;

  CHECK(init_app(&app, &config) == 0);
  CHECK(start_active(&app) == 0);
  app.track.cruise_mm_s = 0;
  fake_encoder_set_deltas(0, 0);

  for (now_ms = 90u; now_ms <= 410u; now_ms += 10u)
  {
    CHECK(sc_app_service(&app, now_ms) == SC_APP_OK);
    CHECK(app.target.left_mm_s == 0);
    CHECK(app.target.right_mm_s == 0);
    CHECK(app.stall_active[0] == 0u);
    CHECK(app.stall_window_start_ms[0] == 0u);
  }

  CHECK(app.phase == SC_APP_PHASE_ACTIVE);
  CHECK((app.fault_bits &
         (SC_APP_FAULT_STALL_LEFT | SC_APP_FAULT_STALL_RIGHT)) == 0u);
  CHECK(app.speed_output.left_target_ramped_mm_s == 0);
  CHECK(app.speed_output.left_permille == 0);
  return 0;
}

static int test_subthreshold_inner_target_no_stall(void)
{
  sc_app_config_t config = valid_config();
  sc_app_t app;
  uint32_t now_ms;

  config.cruise_mm_s = 300;
  config.speed_controller.left.kp_milli = 0;
  config.speed_controller.left.ki_milli = 0;
  config.speed_controller.left.kd_milli = 0;
  config.speed_controller.left.ff_milli = 5000;
  CHECK(init_app(&app, &config) == 0);
  CHECK(start_active(&app) == 0);
  fake_line_set_mask(0x01u);
  fake_encoder_set_deltas(0, 20);
  for (now_ms = 90u; now_ms <= 410u; now_ms += 10u)
  {
    CHECK(sc_app_service(&app, now_ms) == SC_APP_OK);
    CHECK(app.stall_active[0] == 0u);
  }
  CHECK(app.phase == SC_APP_PHASE_ACTIVE);
  CHECK((app.fault_bits & SC_APP_FAULT_STALL_LEFT) == 0u);
  CHECK(app.stall_active[0] == 0u);
  CHECK(app.stall_window_start_ms[0] == 0u);
  CHECK(app.speed_output.left_target_ramped_mm_s == 30);
  CHECK(app.speed_output.left_permille >= SC_APP_STALL_PWM_PERMILLE);
  CHECK(app.speed_output.left_permille == 150);
  return 0;
}

static int test_subthreshold_pwm_no_stall(void)
{
  sc_app_config_t config = valid_config();
  sc_app_t app;
  uint32_t now_ms;

  config.cruise_mm_s = 100;
  config.speed_controller.left.kp_milli = 0;
  config.speed_controller.left.ki_milli = 0;
  config.speed_controller.left.kd_milli = 0;
  config.speed_controller.left.ff_milli = 1000;
  CHECK(init_app(&app, &config) == 0);
  CHECK(start_active(&app) == 0);
  fake_encoder_set_deltas(0, 20);
  for (now_ms = 90u; now_ms <= 410u; now_ms += 10u)
  {
    CHECK(sc_app_service(&app, now_ms) == SC_APP_OK);
    CHECK(app.stall_active[0] == 0u);
  }
  CHECK(app.phase == SC_APP_PHASE_ACTIVE);
  CHECK((app.fault_bits & SC_APP_FAULT_STALL_LEFT) == 0u);
  CHECK(app.stall_active[0] == 0u);
  CHECK(app.stall_window_start_ms[0] == 0u);
  CHECK(app.speed_output.left_target_ramped_mm_s == 100);
  CHECK(app.speed_output.left_permille < SC_APP_STALL_PWM_PERMILLE);
  CHECK(app.speed_output.left_permille == 100);
  return 0;
}

static int test_negative_command_forward_counts_no_direction_fault(void)
{
  sc_app_config_t config = negative_command_config();
  sc_app_t app;
  sc_app_snapshot_t snapshot;
  uint32_t now_ms;

  CHECK(init_app(&app, &config) == 0);
  fake_encoder_set_deltas(-20, 20);
  CHECK(start_active(&app) == 0);
  CHECK(sc_app_service(&app, 90u) == SC_APP_OK);
  CHECK(sc_app_snapshot(&app, &snapshot) == SC_APP_OK);
  CHECK(snapshot.target.left_mm_s > 0);
  CHECK(snapshot.left_pwm < 0);
  CHECK(app.reverse_samples[0] == 0u);
  for (now_ms = 100u; now_ms <= 180u; now_ms += 10u)
  {
    CHECK(sc_app_service(&app, now_ms) == SC_APP_OK);
  }
  CHECK((app.fault_bits & SC_APP_FAULT_DIRECTION_LEFT) == 0u);
  CHECK(app.phase == SC_APP_PHASE_ACTIVE);
  return 0;
}

static int test_negative_command_reverse_counts_not_direction_fatal(void)
{
  sc_app_config_t config = negative_command_config();
  sc_app_t app;
  sc_app_snapshot_t snapshot;
  uint32_t now_ms;

  CHECK(init_app(&app, &config) == 0);
  fake_encoder_set_deltas(20, 20);
  CHECK(start_active(&app) == 0);
  CHECK(sc_app_service(&app, 90u) == SC_APP_OK);
  CHECK(sc_app_snapshot(&app, &snapshot) == SC_APP_OK);
  CHECK(snapshot.left_pwm < 0);
  CHECK(app.reverse_samples[0] == 0u);
  for (now_ms = 100u; now_ms <= 180u; now_ms += 10u)
  {
    CHECK(sc_app_service(&app, now_ms) == SC_APP_OK);
  }
  CHECK((app.fault_bits & SC_APP_FAULT_DIRECTION_LEFT) == 0u);
  CHECK(app.phase == SC_APP_PHASE_ACTIVE);
  return 0;
}

static int test_reverse_direction_is_fatal(void)
{
  sc_app_config_t config = valid_config();
  sc_app_t app;

  CHECK(init_app(&app, &config) == 0);
  CHECK(start_active(&app) == 0);
  fake_encoder_set_deltas(1, 1);
  (void)sc_app_service(&app, 90u);
  (void)sc_app_service(&app, 100u);
  CHECK(sc_app_service(&app, 110u) == SC_APP_FAULT_FATAL);
  CHECK((app.fault_bits & SC_APP_FAULT_DIRECTION_LEFT) != 0u);
  CHECK((app.fault_bits & SC_APP_FATAL_MASK) != 0u);
  return 0;
}

static int test_stale_feedback_faults(void)
{
  sc_app_config_t config = valid_config();
  sc_app_t app;

  CHECK(init_app(&app, &config) == 0);
  CHECK(start_active(&app) == 0);
  fake_encoder_freeze(1u);
  CHECK(sc_app_service(&app, 90u) == SC_APP_OK);
  CHECK(sc_app_service(&app, 111u) == SC_APP_FAULT_RECOVERABLE);
  CHECK((app.fault_bits & SC_APP_FAULT_FEEDBACK) != 0u);
  return 0;
}

static int test_scheduling_overrun_faults(void)
{
  sc_app_config_t config = valid_config();
  sc_app_t app;

  CHECK(init_app(&app, &config) == 0);
  CHECK(sc_app_service(&app, 101u) == SC_APP_FAULT_RECOVERABLE);
  CHECK((app.fault_bits & SC_APP_FAULT_SCHEDULE) != 0u);
  CHECK(app.stop_reason == SC_APP_STOP_SCHEDULE);
  return 0;
}

static int test_degraded_bits_do_not_stop(void)
{
  sc_app_config_t config = valid_config();
  sc_app_t app;
  sc_app_snapshot_t snapshot;

  CHECK(init_app(&app, &config) == 0);
  CHECK(sc_app_report_degraded(
          &app, SC_APP_DEGRADED_IMU | SC_APP_DEGRADED_DISPLAY) ==
        SC_APP_OK);
  CHECK(sc_app_service(&app, 10u) == SC_APP_OK);
  CHECK(sc_app_snapshot(&app, &snapshot) == SC_APP_OK);
  CHECK(snapshot.degraded_bits ==
        (SC_APP_DEGRADED_IMU | SC_APP_DEGRADED_DISPLAY));
  CHECK(snapshot.fault_bits == 0u);
  CHECK(app.phase != SC_APP_PHASE_FAULT);
  return 0;
}

static int test_snapshot_pwm_uses_motor_sign(void)
{
  sc_app_config_t config = valid_config();
  sc_app_t app;
  sc_app_snapshot_t snapshot;

  config.left_motor_sign = -1;
  config.right_motor_sign = 1;
  CHECK(init_app(&app, &config) == 0);
  CHECK(start_active(&app) == 0);
  CHECK(sc_app_service(&app, 90u) == SC_APP_OK);
  CHECK(sc_app_service(&app, 100u) == SC_APP_OK);
  CHECK(sc_app_snapshot(&app, &snapshot) == SC_APP_OK);
  CHECK(snapshot.speed_output.left_permille == 40);
  CHECK(snapshot.speed_output.right_permille == 40);
  CHECK(snapshot.left_pwm == -40);
  CHECK(snapshot.right_pwm == 40);
  CHECK(fake_tb_state()->left_command == -40);
  CHECK(fake_tb_state()->right_command == 40);
  return 0;
}

typedef struct {
  const char *name;
  int (*run)(void);
} test_case_t;

static const test_case_t s_tests[] = {
  {"defaults_match_bench_state", test_defaults_match_bench_state},
  {"power_on_held_release_does_not_start",
   test_power_on_held_release_does_not_start},
  {"isolated_release_does_not_start",
   test_isolated_release_does_not_start},
  {"center_window_and_start_order", test_center_window_and_start_order},
  {"center_interruption_blocks_start",
   test_center_interruption_blocks_start},
  {"single_side_feedback_invalid_blocks_start",
   test_single_side_feedback_invalid_blocks_start},
  {"full_white_and_invalid_config_block_start",
   test_full_white_and_invalid_config_block_start},
  {"long_press_stops_then_new_click_restarts",
   test_long_press_stops_then_new_click_restarts},
  {"active_all_white_faults", test_active_all_white_faults},
  {"known_direction_white_search_stays_active",
   test_known_direction_white_search_stays_active},
  {"recovering_junction_search_stays_active",
   test_recovering_junction_search_stays_active},
  {"recovering_full_black_timeline_stays_active",
   test_recovering_full_black_timeline_stays_active},
  {"recovering_search_outer_stall_faults",
   test_recovering_search_outer_stall_faults},
  {"recovering_search_feedback_faults",
   test_recovering_search_feedback_faults},
  {"recovering_search_wrong_direction_fault",
   test_recovering_search_wrong_direction_fault},
  {"recovering_search_tb_and_line_faults",
   test_recovering_search_tb_and_line_faults},
  {"recoverable_health_without_new_press_no_restart",
   test_recoverable_health_without_new_press_no_restart},
  {"line_lost_recovery_requires_center",
   test_line_lost_recovery_requires_center},
  {"feedback_recovery_requires_nonwhite_line",
   test_feedback_recovery_requires_nonwhite_line},
  {"tb_fault_is_fatal", test_tb_fault_is_fatal},
  {"stall_gate_thresholds", test_stall_gate_thresholds},
  {"single_wheel_stall", test_single_wheel_stall},
  {"jittery_counts_fail_progress_window",
   test_jittery_counts_fail_progress_window},
  {"real_progress_passes_stall_window",
   test_real_progress_passes_stall_window},
  {"inner_zero_target_no_stall", test_inner_zero_target_no_stall},
  {"subthreshold_inner_target_no_stall",
   test_subthreshold_inner_target_no_stall},
  {"subthreshold_pwm_no_stall", test_subthreshold_pwm_no_stall},
  {"negative_command_forward_counts_no_direction_fault",
   test_negative_command_forward_counts_no_direction_fault},
  {"negative_command_reverse_counts_not_direction_fatal",
   test_negative_command_reverse_counts_not_direction_fatal},
  {"reverse_direction_is_fatal", test_reverse_direction_is_fatal},
  {"stale_feedback_faults", test_stale_feedback_faults},
  {"scheduling_overrun_faults", test_scheduling_overrun_faults},
  {"degraded_bits_do_not_stop", test_degraded_bits_do_not_stop},
  {"snapshot_pwm_uses_motor_sign", test_snapshot_pwm_uses_motor_sign}
};

int main(int argc, char **argv)
{
  size_t index;

  if (argc != 2)
  {
    return 2;
  }
  for (index = 0u; index < (sizeof(s_tests) / sizeof(s_tests[0])); index++)
  {
    if (strcmp(argv[1], s_tests[index].name) == 0)
    {
      return s_tests[index].run();
    }
  }
  return 2;
}
