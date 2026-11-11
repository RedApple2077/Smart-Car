#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "sc_encoder.h"
#include "sc_tb6612.h"

#define CHECK(condition) \
  do { \
    if (!(condition)) { \
      fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
              __FILE__, __LINE__, #condition); \
      return 1; \
    } \
  } while (0)

static int prepare_ready_tb(sc_tb6612_state_t *state)
{
  CHECK(sc_tb6612_init() == SC_TB6612_OK);
  CHECK(sc_tb6612_set_standby(1u) == SC_TB6612_OK);
  CHECK(sc_tb6612_get_state(state) == SC_TB6612_OK);
  CHECK(state->mode == SC_TB6612_MODE_ENABLED);
  return 0;
}

static int test_tb_init_standby_idempotent(void)
{
  sc_tb6612_state_t state;
  size_t event_count;
  const fake_event_t *event;

  fake_hal_reset();
  CHECK(sc_tb6612_init() == SC_TB6612_OK);
  CHECK(fake_event_count() == 15u);
  CHECK(fake_gpio_level(TB6612_STBY_GPIO_Port, TB6612_STBY_Pin) ==
        GPIO_PIN_RESET);
  CHECK(fake_gpio_level(AIN1_GPIO_Port, AIN1_Pin) == GPIO_PIN_RESET);
  CHECK(fake_gpio_level(AIN2_GPIO_Port, AIN2_Pin) == GPIO_PIN_RESET);
  CHECK(fake_gpio_level(BIN1_GPIO_Port, BIN1_Pin) == GPIO_PIN_RESET);
  CHECK(fake_gpio_level(BIN2_GPIO_Port, BIN2_Pin) == GPIO_PIN_RESET);
  CHECK(htim4.fake_ccr1 == 0u);
  CHECK(htim4.fake_ccr2 == 0u);

  event = fake_event_at(0u);
  CHECK((event->kind == FAKE_EVENT_GPIO_WRITE) &&
        (event->pin == TB6612_STBY_Pin) &&
        (event->value == GPIO_PIN_RESET));
  event = fake_event_at(1u);
  CHECK((event->kind == FAKE_EVENT_GPIO_WRITE) &&
        (event->pin == AIN1_Pin) && (event->value == GPIO_PIN_RESET));
  event = fake_event_at(2u);
  CHECK((event->kind == FAKE_EVENT_GPIO_WRITE) &&
        (event->pin == AIN2_Pin) && (event->value == GPIO_PIN_RESET));
  event = fake_event_at(3u);
  CHECK((event->kind == FAKE_EVENT_GPIO_WRITE) &&
        (event->pin == BIN1_Pin) && (event->value == GPIO_PIN_RESET));
  event = fake_event_at(4u);
  CHECK((event->kind == FAKE_EVENT_GPIO_WRITE) &&
        (event->pin == BIN2_Pin) && (event->value == GPIO_PIN_RESET));
  event = fake_event_at(5u);
  CHECK((event->kind == FAKE_EVENT_PWM_START) &&
        (event->channel == TIM_CHANNEL_1));
  event = fake_event_at(6u);
  CHECK((event->kind == FAKE_EVENT_PWM_START) &&
        (event->channel == TIM_CHANNEL_2));
  event = fake_event_at(7u);
  CHECK((event->kind == FAKE_EVENT_GPIO_WRITE) &&
        (event->pin == TB6612_STBY_Pin) &&
        (event->value == GPIO_PIN_RESET));
  event = fake_event_at(8u);
  CHECK((event->kind == FAKE_EVENT_GPIO_WRITE) &&
        (event->pin == AIN1_Pin) && (event->value == GPIO_PIN_RESET));
  event = fake_event_at(9u);
  CHECK((event->kind == FAKE_EVENT_GPIO_WRITE) &&
        (event->pin == AIN2_Pin) && (event->value == GPIO_PIN_RESET));
  event = fake_event_at(10u);
  CHECK((event->kind == FAKE_EVENT_GPIO_WRITE) &&
        (event->pin == BIN1_Pin) && (event->value == GPIO_PIN_RESET));
  event = fake_event_at(11u);
  CHECK((event->kind == FAKE_EVENT_GPIO_WRITE) &&
        (event->pin == BIN2_Pin) && (event->value == GPIO_PIN_RESET));
  event = fake_event_at(12u);
  CHECK((event->kind == FAKE_EVENT_SET_COMPARE) &&
        (event->channel == TIM_CHANNEL_1) && (event->value == 0u));
  event = fake_event_at(13u);
  CHECK((event->kind == FAKE_EVENT_SET_COMPARE) &&
        (event->channel == TIM_CHANNEL_2) && (event->value == 0u));
  event = fake_event_at(14u);
  CHECK(event->kind == FAKE_EVENT_TIM_UPDATE);

  CHECK(sc_tb6612_get_state(&state) == SC_TB6612_OK);
  CHECK(state.initialized == 1u);
  CHECK(state.enabled == 0u);
  CHECK(state.standby == 0u);
  CHECK(state.mode == SC_TB6612_MODE_STANDBY);

  fake_clear_events();
  CHECK(sc_tb6612_apply(1001, 0) == SC_TB6612_ERR_NOT_READY);
  CHECK(fake_find_event(FAKE_EVENT_SET_COMPARE, 0u) < 0);
  CHECK(fake_find_event(FAKE_EVENT_TIM_UPDATE, 0u) < 0);
  CHECK(fake_find_event(FAKE_EVENT_PWM_START, 0u) < 0);

  event_count = fake_event_count();
  CHECK(sc_tb6612_init() == SC_TB6612_OK);
  CHECK(fake_event_count() == event_count);
  CHECK(sc_tb6612_get_state(&state) == SC_TB6612_OK);
  CHECK(state.mode == SC_TB6612_MODE_STANDBY);
  CHECK(sc_tb6612_get_state(NULL) == SC_TB6612_ERR_ARGUMENT);
  return 0;
}

static int test_tb_second_channel_rollback(void)
{
  sc_tb6612_state_t state;
  int stop_index;

  fake_hal_reset();
  fake_fail_next_pwm_start(TIM_CHANNEL_2);
  CHECK(sc_tb6612_init() == SC_TB6612_ERR_HAL);
  CHECK(fake_gpio_level(TB6612_STBY_GPIO_Port, TB6612_STBY_Pin) ==
        GPIO_PIN_RESET);
  CHECK(htim4.fake_pwm_ch1_started == 0u);
  CHECK(htim4.fake_pwm_ch2_started == 0u);
  stop_index = fake_find_event(FAKE_EVENT_PWM_STOP, 0u);
  CHECK(stop_index >= 0);
  CHECK(fake_event_at((size_t)stop_index)->channel == TIM_CHANNEL_1);

  CHECK(sc_tb6612_get_state(&state) == SC_TB6612_OK);
  CHECK(state.initialized == 0u);
  CHECK(state.mode == SC_TB6612_MODE_FAULT);
  CHECK(state.pwm_start_failures == 1u);
  CHECK(state.rollback_count == 1u);
  CHECK(state.last_status == SC_TB6612_ERR_HAL);
  return 0;
}

static int test_tb_init_event_failure_rollback(void)
{
  sc_tb6612_state_t state;

  fake_hal_reset();
  fake_fail_next_generate_event();
  CHECK(sc_tb6612_init() == SC_TB6612_ERR_HAL);
  CHECK(fake_find_event(FAKE_EVENT_PWM_START, 0u) >= 0);
  CHECK(htim4.fake_pwm_ch1_started == 0u);
  CHECK(htim4.fake_pwm_ch2_started == 0u);
  CHECK(fake_gpio_level(TB6612_STBY_GPIO_Port, TB6612_STBY_Pin) ==
        GPIO_PIN_RESET);
  CHECK(htim4.fake_ccr1 == 0u);
  CHECK(htim4.fake_ccr2 == 0u);
  CHECK(sc_tb6612_get_state(&state) == SC_TB6612_OK);
  CHECK(state.initialized == 0u);
  CHECK(state.mode == SC_TB6612_MODE_FAULT);
  CHECK(state.update_failures == 1u);
  CHECK(state.rollback_count == 1u);
  CHECK(state.safe_stop_count == 1u);
  CHECK(state.last_hal_error == (uint32_t)HAL_ERROR);
  return 0;
}

static int test_tb_config_reject(void)
{
  sc_tb6612_state_t state;
  const fake_event_t *event;

  fake_hal_reset();
  HAL_GPIO_WritePin(TB6612_STBY_GPIO_Port, TB6612_STBY_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(AIN1_GPIO_Port, AIN1_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(AIN2_GPIO_Port, AIN2_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(BIN1_GPIO_Port, BIN1_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(BIN2_GPIO_Port, BIN2_Pin, GPIO_PIN_SET);
  fake_clear_events();
  htim4.Instance = NULL;
  CHECK(sc_tb6612_init() == SC_TB6612_ERR_CONFIG);
  CHECK(fake_event_count() == 5u);
  event = fake_event_at(0u);
  CHECK((event->kind == FAKE_EVENT_GPIO_WRITE) &&
        (event->pin == TB6612_STBY_Pin) &&
        (event->value == GPIO_PIN_RESET));
  event = fake_event_at(1u);
  CHECK((event->kind == FAKE_EVENT_GPIO_WRITE) &&
        (event->pin == AIN1_Pin) && (event->value == GPIO_PIN_RESET));
  event = fake_event_at(2u);
  CHECK((event->kind == FAKE_EVENT_GPIO_WRITE) &&
        (event->pin == AIN2_Pin) && (event->value == GPIO_PIN_RESET));
  event = fake_event_at(3u);
  CHECK((event->kind == FAKE_EVENT_GPIO_WRITE) &&
        (event->pin == BIN1_Pin) && (event->value == GPIO_PIN_RESET));
  event = fake_event_at(4u);
  CHECK((event->kind == FAKE_EVENT_GPIO_WRITE) &&
        (event->pin == BIN2_Pin) && (event->value == GPIO_PIN_RESET));
  CHECK(fake_gpio_level(TB6612_STBY_GPIO_Port, TB6612_STBY_Pin) ==
        GPIO_PIN_RESET);
  CHECK(fake_gpio_level(AIN1_GPIO_Port, AIN1_Pin) == GPIO_PIN_RESET);
  CHECK(fake_gpio_level(AIN2_GPIO_Port, AIN2_Pin) == GPIO_PIN_RESET);
  CHECK(fake_gpio_level(BIN1_GPIO_Port, BIN1_Pin) == GPIO_PIN_RESET);
  CHECK(fake_gpio_level(BIN2_GPIO_Port, BIN2_Pin) == GPIO_PIN_RESET);
  CHECK(sc_tb6612_get_state(&state) == SC_TB6612_OK);
  CHECK(state.initialized == 0u);
  CHECK(state.mode == SC_TB6612_MODE_FAULT);
  CHECK(state.last_status == SC_TB6612_ERR_CONFIG);
  return 0;
}

static int test_tb_apply_uninitialized_no_tim(void)
{
  sc_tb6612_state_t state;

  fake_hal_reset();
  CHECK(sc_tb6612_apply(2000, 0) == SC_TB6612_ERR_NOT_READY);
  CHECK(fake_event_count() == 0u);
  CHECK(sc_tb6612_get_state(&state) == SC_TB6612_OK);
  CHECK(state.initialized == 0u);
  CHECK(state.mode == SC_TB6612_MODE_UNINITIALIZED);
  CHECK(state.last_status == SC_TB6612_ERR_NOT_READY);
  CHECK(state.rejected_requests == 0u);
  CHECK(state.safe_stop_count == 0u);
  return 0;
}

static int test_tb_scaling_same_direction(void)
{
  sc_tb6612_state_t state;
  size_t before_updates;
  size_t after_updates;

  fake_hal_reset();
  if (prepare_ready_tb(&state) != 0)
  {
    return 1;
  }

  fake_clear_events();
  CHECK(sc_tb6612_apply(1, -500) == SC_TB6612_OK);
  CHECK(htim4.fake_ccr1 == 4u);
  CHECK(htim4.fake_ccr2 == 1800u);
  CHECK(fake_gpio_level(AIN1_GPIO_Port, AIN1_Pin) == GPIO_PIN_SET);
  CHECK(fake_gpio_level(AIN2_GPIO_Port, AIN2_Pin) == GPIO_PIN_RESET);
  CHECK(fake_gpio_level(BIN1_GPIO_Port, BIN1_Pin) == GPIO_PIN_RESET);
  CHECK(fake_gpio_level(BIN2_GPIO_Port, BIN2_Pin) == GPIO_PIN_SET);
  CHECK(fake_gpio_level(TB6612_STBY_GPIO_Port, TB6612_STBY_Pin) ==
        GPIO_PIN_SET);

  before_updates =
    (size_t)(fake_find_event(FAKE_EVENT_TIM_UPDATE, 0u) >= 0);
  CHECK(before_updates == 1u);
  fake_clear_events();
  CHECK(sc_tb6612_apply(1000, -1000) == SC_TB6612_OK);
  CHECK(htim4.fake_ccr1 == 3600u);
  CHECK(htim4.fake_ccr2 == 3600u);
  after_updates =
    (size_t)(fake_find_event(FAKE_EVENT_TIM_UPDATE, 0u) >= 0);
  CHECK(after_updates == 0u);
  CHECK(sc_tb6612_get_state(&state) == SC_TB6612_OK);
  CHECK(state.left_command == 1000);
  CHECK(state.right_command == -1000);
  CHECK(state.left_ccr == 3600u);
  CHECK(state.right_ccr == 3600u);
  return 0;
}

static int test_tb_zero_high_impedance(void)
{
  sc_tb6612_state_t state;

  fake_hal_reset();
  if (prepare_ready_tb(&state) != 0)
  {
    return 1;
  }
  CHECK(sc_tb6612_apply(400, -400) == SC_TB6612_OK);
  CHECK(sc_tb6612_apply(0, 0) == SC_TB6612_OK);
  CHECK(fake_gpio_level(AIN1_GPIO_Port, AIN1_Pin) == GPIO_PIN_RESET);
  CHECK(fake_gpio_level(AIN2_GPIO_Port, AIN2_Pin) == GPIO_PIN_RESET);
  CHECK(fake_gpio_level(BIN1_GPIO_Port, BIN1_Pin) == GPIO_PIN_RESET);
  CHECK(fake_gpio_level(BIN2_GPIO_Port, BIN2_Pin) == GPIO_PIN_RESET);
  CHECK(fake_gpio_level(TB6612_STBY_GPIO_Port, TB6612_STBY_Pin) ==
        GPIO_PIN_SET);
  CHECK(htim4.fake_ccr1 == 3600u);
  CHECK(htim4.fake_ccr2 == 3600u);
  CHECK(sc_tb6612_get_state(&state) == SC_TB6612_OK);
  CHECK(state.left_command == 0);
  CHECK(state.right_command == 0);
  CHECK(state.mode == SC_TB6612_MODE_ENABLED);
  return 0;
}

static int test_tb_out_of_range_safe_fault(void)
{
  sc_tb6612_state_t state;
  const fake_event_t *event;

  fake_hal_reset();
  if (prepare_ready_tb(&state) != 0)
  {
    return 1;
  }
  CHECK(sc_tb6612_apply(500, 500) == SC_TB6612_OK);
  fake_clear_events();
  CHECK(sc_tb6612_apply(1001, 0) == SC_TB6612_ERR_ARGUMENT);
  CHECK(fake_event_count() == 8u);
  event = fake_event_at(0u);
  CHECK((event->kind == FAKE_EVENT_GPIO_WRITE) &&
        (event->pin == TB6612_STBY_Pin) &&
        (event->value == GPIO_PIN_RESET));
  event = fake_event_at(1u);
  CHECK((event->kind == FAKE_EVENT_GPIO_WRITE) &&
        (event->pin == AIN1_Pin) && (event->value == GPIO_PIN_RESET));
  event = fake_event_at(2u);
  CHECK((event->kind == FAKE_EVENT_GPIO_WRITE) &&
        (event->pin == AIN2_Pin) && (event->value == GPIO_PIN_RESET));
  event = fake_event_at(3u);
  CHECK((event->kind == FAKE_EVENT_GPIO_WRITE) &&
        (event->pin == BIN1_Pin) && (event->value == GPIO_PIN_RESET));
  event = fake_event_at(4u);
  CHECK((event->kind == FAKE_EVENT_GPIO_WRITE) &&
        (event->pin == BIN2_Pin) && (event->value == GPIO_PIN_RESET));
  event = fake_event_at(5u);
  CHECK((event->kind == FAKE_EVENT_SET_COMPARE) &&
        (event->channel == TIM_CHANNEL_1) &&
        (event->value == 0u));
  event = fake_event_at(6u);
  CHECK((event->kind == FAKE_EVENT_SET_COMPARE) &&
        (event->channel == TIM_CHANNEL_2) &&
        (event->value == 0u));
  event = fake_event_at(7u);
  CHECK(event->kind == FAKE_EVENT_TIM_UPDATE);
  CHECK(fake_gpio_level(TB6612_STBY_GPIO_Port, TB6612_STBY_Pin) ==
        GPIO_PIN_RESET);
  CHECK(fake_gpio_level(AIN1_GPIO_Port, AIN1_Pin) == GPIO_PIN_RESET);
  CHECK(fake_gpio_level(AIN2_GPIO_Port, AIN2_Pin) == GPIO_PIN_RESET);
  CHECK(fake_gpio_level(BIN1_GPIO_Port, BIN1_Pin) == GPIO_PIN_RESET);
  CHECK(fake_gpio_level(BIN2_GPIO_Port, BIN2_Pin) == GPIO_PIN_RESET);
  CHECK(htim4.fake_ccr1 == 0u);
  CHECK(htim4.fake_ccr2 == 0u);
  CHECK(sc_tb6612_get_state(&state) == SC_TB6612_OK);
  CHECK(state.mode == SC_TB6612_MODE_FAULT);
  CHECK(state.rejected_requests == 1u);
  CHECK(state.safe_stop_count == 1u);
  CHECK(state.left_command == 0);
  CHECK(state.right_command == 0);
  CHECK(sc_tb6612_apply(10, 10) == SC_TB6612_ERR_FAULT);
  CHECK(sc_tb6612_set_standby(1u) == SC_TB6612_ERR_FAULT);
  return 0;
}

static int test_tb_set_standby_clear_sequence(void)
{
  sc_tb6612_state_t state;
  const fake_event_t *event;

  fake_hal_reset();
  if (prepare_ready_tb(&state) != 0)
  {
    return 1;
  }
  CHECK(sc_tb6612_apply(300, -300) == SC_TB6612_OK);
  fake_clear_events();
  CHECK(sc_tb6612_set_standby(0u) == SC_TB6612_OK);
  CHECK(fake_event_count() == 8u);

  event = fake_event_at(0u);
  CHECK((event->kind == FAKE_EVENT_GPIO_WRITE) &&
        (event->pin == TB6612_STBY_Pin) &&
        (event->value == GPIO_PIN_RESET));
  event = fake_event_at(1u);
  CHECK((event->kind == FAKE_EVENT_GPIO_WRITE) &&
        (event->pin == AIN1_Pin) && (event->value == GPIO_PIN_RESET));
  event = fake_event_at(2u);
  CHECK((event->kind == FAKE_EVENT_GPIO_WRITE) &&
        (event->pin == AIN2_Pin) && (event->value == GPIO_PIN_RESET));
  event = fake_event_at(3u);
  CHECK((event->kind == FAKE_EVENT_GPIO_WRITE) &&
        (event->pin == BIN1_Pin) && (event->value == GPIO_PIN_RESET));
  event = fake_event_at(4u);
  CHECK((event->kind == FAKE_EVENT_GPIO_WRITE) &&
        (event->pin == BIN2_Pin) && (event->value == GPIO_PIN_RESET));
  event = fake_event_at(5u);
  CHECK((event->kind == FAKE_EVENT_SET_COMPARE) &&
        (event->channel == TIM_CHANNEL_1) &&
        (event->value == 0u));
  event = fake_event_at(6u);
  CHECK((event->kind == FAKE_EVENT_SET_COMPARE) &&
        (event->channel == TIM_CHANNEL_2) &&
        (event->value == 0u));
  CHECK(fake_event_at(7u)->kind == FAKE_EVENT_TIM_UPDATE);
  CHECK(fake_gpio_level(TB6612_STBY_GPIO_Port, TB6612_STBY_Pin) ==
        GPIO_PIN_RESET);
  CHECK(fake_gpio_level(AIN1_GPIO_Port, AIN1_Pin) == GPIO_PIN_RESET);
  CHECK(fake_gpio_level(AIN2_GPIO_Port, AIN2_Pin) == GPIO_PIN_RESET);
  CHECK(fake_gpio_level(BIN1_GPIO_Port, BIN1_Pin) == GPIO_PIN_RESET);
  CHECK(fake_gpio_level(BIN2_GPIO_Port, BIN2_Pin) == GPIO_PIN_RESET);

  CHECK(sc_tb6612_get_state(&state) == SC_TB6612_OK);
  CHECK(state.mode == SC_TB6612_MODE_STANDBY);
  CHECK(state.enabled == 0u);
  CHECK(state.standby == 0u);
  CHECK(state.left_command == 0);
  CHECK(state.right_command == 0);
  CHECK(state.left_ccr == 0u);
  CHECK(state.right_ccr == 0u);
  return 0;
}

static int test_tb_set_standby_update_failure(void)
{
  sc_tb6612_state_t state;

  fake_hal_reset();
  if (prepare_ready_tb(&state) != 0)
  {
    return 1;
  }
  CHECK(sc_tb6612_apply(300, -300) == SC_TB6612_OK);
  fake_fail_next_generate_event();
  CHECK(sc_tb6612_set_standby(0u) == SC_TB6612_ERR_HAL);
  CHECK(fake_gpio_level(TB6612_STBY_GPIO_Port, TB6612_STBY_Pin) ==
        GPIO_PIN_RESET);
  CHECK(htim4.fake_ccr1 == 0u);
  CHECK(htim4.fake_ccr2 == 0u);
  CHECK(sc_tb6612_get_state(&state) == SC_TB6612_OK);
  CHECK(state.mode == SC_TB6612_MODE_FAULT);
  CHECK(state.enabled == 0u);
  CHECK(state.standby == 0u);
  CHECK(state.update_failures == 1u);
  CHECK(state.safe_stop_count == 1u);
  CHECK(state.last_hal_error == (uint32_t)HAL_ERROR);
  return 0;
}

static int test_tb_direction_transition_order(void)
{
  sc_tb6612_state_t state;
  const fake_event_t *event;

  fake_hal_reset();
  if (prepare_ready_tb(&state) != 0)
  {
    return 1;
  }
  fake_clear_events();
  CHECK(sc_tb6612_apply(-250, 250) == SC_TB6612_OK);
  CHECK(fake_event_count() == 9u);

  event = fake_event_at(0u);
  CHECK((event->kind == FAKE_EVENT_GPIO_WRITE) &&
        (event->pin == TB6612_STBY_Pin) &&
        (event->value == GPIO_PIN_RESET));
  event = fake_event_at(1u);
  CHECK((event->kind == FAKE_EVENT_GPIO_WRITE) &&
        (event->pin == AIN1_Pin) && (event->value == GPIO_PIN_RESET));
  event = fake_event_at(2u);
  CHECK((event->kind == FAKE_EVENT_GPIO_WRITE) &&
        (event->pin == AIN2_Pin) && (event->value == GPIO_PIN_SET));
  event = fake_event_at(3u);
  CHECK((event->kind == FAKE_EVENT_GPIO_WRITE) &&
        (event->pin == BIN1_Pin) && (event->value == GPIO_PIN_SET));
  event = fake_event_at(4u);
  CHECK((event->kind == FAKE_EVENT_GPIO_WRITE) &&
        (event->pin == BIN2_Pin) && (event->value == GPIO_PIN_RESET));
  event = fake_event_at(5u);
  CHECK((event->kind == FAKE_EVENT_SET_COMPARE) &&
        (event->channel == TIM_CHANNEL_1) && (event->value == 900u));
  event = fake_event_at(6u);
  CHECK((event->kind == FAKE_EVENT_SET_COMPARE) &&
        (event->channel == TIM_CHANNEL_2) && (event->value == 900u));
  CHECK(fake_event_at(7u)->kind == FAKE_EVENT_TIM_UPDATE);
  event = fake_event_at(8u);
  CHECK((event->kind == FAKE_EVENT_GPIO_WRITE) &&
        (event->pin == TB6612_STBY_Pin) &&
        (event->value == GPIO_PIN_SET));
  CHECK(fake_gpio_level(TB6612_STBY_GPIO_Port, TB6612_STBY_Pin) ==
        GPIO_PIN_SET);
  return 0;
}

static int test_tb_generate_event_failure(void)
{
  sc_tb6612_state_t state;

  fake_hal_reset();
  if (prepare_ready_tb(&state) != 0)
  {
    return 1;
  }
  fake_fail_next_generate_event();
  CHECK(sc_tb6612_apply(-500, 500) == SC_TB6612_ERR_HAL);
  CHECK(fake_gpio_level(TB6612_STBY_GPIO_Port, TB6612_STBY_Pin) ==
        GPIO_PIN_RESET);
  CHECK(fake_gpio_level(AIN1_GPIO_Port, AIN1_Pin) == GPIO_PIN_RESET);
  CHECK(fake_gpio_level(AIN2_GPIO_Port, AIN2_Pin) == GPIO_PIN_RESET);
  CHECK(fake_gpio_level(BIN1_GPIO_Port, BIN1_Pin) == GPIO_PIN_RESET);
  CHECK(fake_gpio_level(BIN2_GPIO_Port, BIN2_Pin) == GPIO_PIN_RESET);
  CHECK(htim4.fake_ccr1 == 0u);
  CHECK(htim4.fake_ccr2 == 0u);
  CHECK(sc_tb6612_get_state(&state) == SC_TB6612_OK);
  CHECK(state.mode == SC_TB6612_MODE_FAULT);
  CHECK(state.update_failures == 1u);
  CHECK(state.safe_stop_count == 1u);
  CHECK(state.last_hal_error == (uint32_t)HAL_ERROR);
  return 0;
}

static int test_encoder_init_idempotent(void)
{
  sc_encoder_state_t state;
  sc_encoder_snapshot_t snapshot;
  size_t event_count;
  uint32_t sequence;

  fake_hal_reset();
  CHECK(sc_encoder_init() == SC_ENCODER_OK);
  CHECK(htim2.fake_encoder_started == 1u);
  CHECK(htim3.fake_encoder_started == 1u);
  CHECK(fake_find_event(FAKE_EVENT_ENCODER_START, 0u) >= 0);
  CHECK(fake_event_at((size_t)fake_find_event(
          FAKE_EVENT_ENCODER_START, 0u))->tim == &htim2);
  CHECK(fake_event_at((size_t)fake_find_event(
          FAKE_EVENT_ENCODER_START,
          (size_t)fake_find_event(
            FAKE_EVENT_ENCODER_START, 0u) + 1u))->tim == &htim3);
  CHECK(htim2.fake_counter == 0u);
  CHECK(htim3.fake_counter == 0u);

  htim2.fake_counter = 123u;
  fake_clear_events();
  event_count = fake_event_count();
  CHECK(sc_encoder_init() == SC_ENCODER_OK);
  CHECK(fake_event_count() == event_count);
  CHECK(htim2.fake_counter == 123u);
  CHECK(sc_encoder_get_status(&state) == SC_ENCODER_OK);
  CHECK(state.initialized == 1u);
  CHECK(state.last_status == SC_ENCODER_OK);

  htim2.fake_counter = 0u;
  htim3.fake_counter = 0u;
  CHECK(sc_encoder_service(0u) == SC_ENCODER_OK);
  htim2.fake_counter = 5u;
  htim3.fake_counter = 7u;
  CHECK(sc_encoder_service(10u) == SC_ENCODER_OK);
  CHECK(sc_encoder_get_latest(&snapshot) == SC_ENCODER_OK);
  CHECK(snapshot.left.total_count == 5);
  CHECK(snapshot.right.total_count == 7);
  sequence = snapshot.sequence;

  fake_clear_events();
  CHECK(sc_encoder_init() == SC_ENCODER_OK);
  CHECK(fake_event_count() == 0u);
  CHECK(sc_encoder_get_latest(&snapshot) == SC_ENCODER_OK);
  CHECK(snapshot.left.total_count == 5);
  CHECK(snapshot.right.total_count == 7);
  CHECK(snapshot.sequence == sequence);
  return 0;
}

static int test_encoder_second_start_rollback(void)
{
  sc_encoder_state_t state;
  int stop_index;

  fake_hal_reset();
  fake_fail_next_encoder_start(&htim3);
  CHECK(sc_encoder_init() == SC_ENCODER_ERR_HAL);
  CHECK(htim2.fake_encoder_started == 0u);
  CHECK(htim3.fake_encoder_started == 0u);
  stop_index = fake_find_event(FAKE_EVENT_ENCODER_STOP, 0u);
  CHECK(stop_index >= 0);
  CHECK(fake_event_at((size_t)stop_index)->tim == &htim2);
  CHECK(sc_encoder_get_status(&state) == SC_ENCODER_ERR_NOT_READY);
  CHECK(state.initialized == 0u);
  CHECK(state.start_failures == 1u);
  CHECK(state.rollback_count == 1u);
  CHECK(state.last_status == SC_ENCODER_ERR_HAL);
  return 0;
}

static int test_encoder_config_reject(void)
{
  sc_encoder_state_t state;

  fake_hal_reset();
  htim3.Init.Prescaler = 1u;
  CHECK(sc_encoder_init() == SC_ENCODER_ERR_CONFIG);
  CHECK(fake_find_event(FAKE_EVENT_ENCODER_START, 0u) < 0);
  CHECK(sc_encoder_get_status(&state) == SC_ENCODER_ERR_NOT_READY);
  CHECK(state.initialized == 0u);
  CHECK(state.last_status == SC_ENCODER_ERR_CONFIG);
  return 0;
}

static int test_encoder_baseline_and_period(void)
{
  sc_encoder_snapshot_t snapshot;

  fake_hal_reset();
  CHECK(sc_encoder_init() == SC_ENCODER_OK);
  htim2.fake_counter = 100u;
  htim3.fake_counter = 200u;
  CHECK(sc_encoder_service(1000u) == SC_ENCODER_OK);
  CHECK(sc_encoder_get_latest(&snapshot) == SC_ENCODER_OK);
  CHECK(snapshot.valid == 1u);
  CHECK(snapshot.left.raw_count == 100u);
  CHECK(snapshot.right.raw_count == 200u);
  CHECK(snapshot.left.total_count == 0);
  CHECK(snapshot.right.total_count == 0);
  CHECK(snapshot.interval_ms == 0u);
  CHECK(snapshot.sequence == 1u);

  htim2.fake_counter = 105u;
  htim3.fake_counter = 198u;
  CHECK(sc_encoder_service(1009u) == SC_ENCODER_OK);
  CHECK(sc_encoder_get_latest(&snapshot) == SC_ENCODER_OK);
  CHECK(snapshot.sequence == 1u);
  CHECK(sc_encoder_service(1010u) == SC_ENCODER_OK);
  CHECK(sc_encoder_get_latest(&snapshot) == SC_ENCODER_OK);
  CHECK(snapshot.sequence == 2u);
  CHECK(snapshot.interval_ms == 10u);
  CHECK(snapshot.left.delta_count == 5);
  CHECK(snapshot.right.delta_count == -2);
  CHECK(snapshot.left.total_count == 5);
  CHECK(snapshot.right.total_count == -2);
  return 0;
}

static int test_encoder_signed_wrap(void)
{
  sc_encoder_snapshot_t snapshot;

  fake_hal_reset();
  CHECK(sc_encoder_init() == SC_ENCODER_OK);
  CHECK(sc_encoder_reset_counts(5000u) == SC_ENCODER_OK);
  htim2.fake_counter = UINT16_C(0xFFFE);
  htim3.fake_counter = 2u;
  CHECK(sc_encoder_service(5010u) == SC_ENCODER_OK);
  CHECK(sc_encoder_get_latest(&snapshot) == SC_ENCODER_OK);
  CHECK(snapshot.left.delta_count == -2);
  CHECK(snapshot.right.delta_count == 2);

  htim2.fake_counter = 1u;
  htim3.fake_counter = UINT16_C(0xFFFF);
  CHECK(sc_encoder_service(5020u) == SC_ENCODER_OK);
  CHECK(sc_encoder_get_latest(&snapshot) == SC_ENCODER_OK);
  CHECK(snapshot.left.delta_count == 3);
  CHECK(snapshot.right.delta_count == -3);
  CHECK(snapshot.left.total_count == 1);
  CHECK(snapshot.right.total_count == -1);
  return 0;
}

static int test_encoder_ambiguous_side(void)
{
  sc_encoder_snapshot_t snapshot;
  sc_encoder_state_t state;

  fake_hal_reset();
  CHECK(sc_encoder_init() == SC_ENCODER_OK);
  CHECK(sc_encoder_reset_counts(5000u) == SC_ENCODER_OK);
  htim2.fake_counter = UINT16_C(0x8000);
  htim3.fake_counter = 1u;
  CHECK(sc_encoder_service(5010u) == SC_ENCODER_ERR_LOST_SYNC);
  CHECK(sc_encoder_get_latest(&snapshot) == SC_ENCODER_OK);
  CHECK(snapshot.valid == 0u);
  CHECK(snapshot.left.valid == 0u);
  CHECK(snapshot.left.lost_sync == 1u);
  CHECK(snapshot.left.total_count == 0);
  CHECK(snapshot.left.delta_count == 0);
  CHECK(snapshot.right.valid == 1u);
  CHECK(snapshot.right.total_count == 1);

  htim2.fake_counter = UINT16_C(0x8001);
  htim3.fake_counter = 3u;
  CHECK(sc_encoder_service(5020u) == SC_ENCODER_ERR_LOST_SYNC);
  CHECK(sc_encoder_get_latest(&snapshot) == SC_ENCODER_OK);
  CHECK(snapshot.left.total_count == 0);
  CHECK(snapshot.left.raw_count == UINT16_C(0x8001));
  CHECK(snapshot.right.total_count == 3);
  CHECK(sc_encoder_get_status(&state) == SC_ENCODER_OK);
  CHECK(state.ambiguous_events == 1u);
  CHECK(state.lost_sync_events == 1u);
  return 0;
}

static int test_encoder_long_interval(void)
{
  sc_encoder_snapshot_t snapshot;
  sc_encoder_state_t state;

  fake_hal_reset();
  CHECK(sc_encoder_init() == SC_ENCODER_OK);
  CHECK(sc_encoder_reset_counts(1000u) == SC_ENCODER_OK);
  htim2.fake_counter = 1u;
  htim3.fake_counter = 1u;
  CHECK(sc_encoder_service(1101u) == SC_ENCODER_ERR_LOST_SYNC);
  CHECK(sc_encoder_get_latest(&snapshot) == SC_ENCODER_OK);
  CHECK(snapshot.interval_ms == 101u);
  CHECK(snapshot.valid == 0u);
  CHECK(snapshot.left.total_count == 0);
  CHECK(snapshot.right.total_count == 0);
  CHECK(snapshot.left.delta_count == 0);
  CHECK(snapshot.right.delta_count == 0);

  htim2.fake_counter = 2u;
  htim3.fake_counter = 2u;
  CHECK(sc_encoder_service(1111u) == SC_ENCODER_ERR_LOST_SYNC);
  CHECK(sc_encoder_get_latest(&snapshot) == SC_ENCODER_OK);
  CHECK(snapshot.left.raw_count == 2u);
  CHECK(snapshot.left.total_count == 0);
  CHECK(snapshot.right.total_count == 0);
  CHECK(sc_encoder_get_status(&state) == SC_ENCODER_OK);
  CHECK(state.long_interval_events == 1u);
  CHECK(state.lost_sync == 1u);
  return 0;
}

static int test_encoder_delta_zero(void)
{
  sc_encoder_snapshot_t snapshot;

  fake_hal_reset();
  CHECK(sc_encoder_init() == SC_ENCODER_OK);
  CHECK(sc_encoder_reset_counts(2000u) == SC_ENCODER_OK);
  CHECK(sc_encoder_service(2010u) == SC_ENCODER_OK);
  CHECK(sc_encoder_get_latest(&snapshot) == SC_ENCODER_OK);
  CHECK(snapshot.valid == 1u);
  CHECK(snapshot.left.valid == 1u);
  CHECK(snapshot.right.valid == 1u);
  CHECK(snapshot.left.delta_count == 0);
  CHECK(snapshot.right.delta_count == 0);
  CHECK(snapshot.left.total_count == 0);
  CHECK(snapshot.right.total_count == 0);
  return 0;
}

static int test_encoder_latest_cache(void)
{
  sc_encoder_snapshot_t snapshot;
  uint32_t reads_before;

  fake_hal_reset();
  CHECK(sc_encoder_init() == SC_ENCODER_OK);
  CHECK(sc_encoder_reset_counts(3000u) == SC_ENCODER_OK);
  htim2.fake_counter = 10u;
  htim3.fake_counter = 20u;
  CHECK(sc_encoder_service(3010u) == SC_ENCODER_OK);
  reads_before = fake_counter_read_count();
  htim2.fake_counter = 500u;
  htim3.fake_counter = 600u;
  CHECK(sc_encoder_get_latest(&snapshot) == SC_ENCODER_OK);
  CHECK(snapshot.left.raw_count == 10u);
  CHECK(snapshot.right.raw_count == 20u);
  CHECK(fake_counter_read_count() == reads_before);
  CHECK(sc_encoder_get_latest(NULL) == SC_ENCODER_ERR_ARGUMENT);
  CHECK(sc_encoder_get_status(NULL) == SC_ENCODER_ERR_ARGUMENT);
  return 0;
}

static int test_encoder_reset_recovery(void)
{
  sc_encoder_snapshot_t snapshot;
  sc_encoder_state_t state;

  fake_hal_reset();
  CHECK(sc_encoder_init() == SC_ENCODER_OK);
  CHECK(sc_encoder_reset_counts(4000u) == SC_ENCODER_OK);
  htim2.fake_counter = UINT16_C(0x8000);
  htim3.fake_counter = 1u;
  CHECK(sc_encoder_service(4010u) == SC_ENCODER_ERR_LOST_SYNC);
  htim2.fake_counter = 77u;
  htim3.fake_counter = 88u;
  CHECK(sc_encoder_reset_counts(4020u) == SC_ENCODER_OK);
  CHECK(htim2.fake_counter == 0u);
  CHECK(htim3.fake_counter == 0u);
  CHECK(sc_encoder_get_latest(&snapshot) == SC_ENCODER_OK);
  CHECK(snapshot.valid == 1u);
  CHECK(snapshot.left.lost_sync == 0u);
  CHECK(snapshot.right.lost_sync == 0u);
  CHECK(snapshot.left.total_count == 0);
  CHECK(snapshot.right.total_count == 0);
  CHECK(snapshot.sample_time_ms == 4020u);
  CHECK(sc_encoder_get_status(&state) == SC_ENCODER_OK);
  CHECK(state.resets == 2u);
  CHECK(state.lost_sync == 0u);

  htim2.fake_counter = 5u;
  htim3.fake_counter = 6u;
  CHECK(sc_encoder_service(4030u) == SC_ENCODER_OK);
  CHECK(sc_encoder_get_latest(&snapshot) == SC_ENCODER_OK);
  CHECK(snapshot.left.total_count == 5);
  CHECK(snapshot.right.total_count == 6);
  return 0;
}

static int test_encoder_time_wrap(void)
{
  sc_encoder_snapshot_t snapshot;

  fake_hal_reset();
  CHECK(sc_encoder_init() == SC_ENCODER_OK);
  CHECK(sc_encoder_reset_counts(UINT32_C(0xFFFFFFFA)) == SC_ENCODER_OK);
  htim2.fake_counter = 10u;
  htim3.fake_counter = 20u;
  CHECK(sc_encoder_service(5u) == SC_ENCODER_OK);
  CHECK(sc_encoder_get_latest(&snapshot) == SC_ENCODER_OK);
  CHECK(snapshot.interval_ms == 11u);
  CHECK(snapshot.valid == 1u);
  CHECK(snapshot.left.total_count == 10);
  CHECK(snapshot.right.total_count == 20);
  return 0;
}

typedef struct {
  const char *name;
  int (*run)(void);
} test_case_t;

static const test_case_t s_tests[] = {
  {"tb_init_standby_idempotent", test_tb_init_standby_idempotent},
  {"tb_second_channel_rollback", test_tb_second_channel_rollback},
  {"tb_init_event_failure_rollback", test_tb_init_event_failure_rollback},
  {"tb_config_reject", test_tb_config_reject},
  {"tb_apply_uninitialized_no_tim", test_tb_apply_uninitialized_no_tim},
  {"tb_scaling_same_direction", test_tb_scaling_same_direction},
  {"tb_zero_high_impedance", test_tb_zero_high_impedance},
  {"tb_out_of_range_safe_fault", test_tb_out_of_range_safe_fault},
  {"tb_set_standby_clear_sequence", test_tb_set_standby_clear_sequence},
  {"tb_set_standby_update_failure", test_tb_set_standby_update_failure},
  {"tb_direction_transition_order", test_tb_direction_transition_order},
  {"tb_generate_event_failure", test_tb_generate_event_failure},
  {"encoder_init_idempotent", test_encoder_init_idempotent},
  {"encoder_second_start_rollback", test_encoder_second_start_rollback},
  {"encoder_config_reject", test_encoder_config_reject},
  {"encoder_baseline_and_period", test_encoder_baseline_and_period},
  {"encoder_signed_wrap", test_encoder_signed_wrap},
  {"encoder_ambiguous_side", test_encoder_ambiguous_side},
  {"encoder_long_interval", test_encoder_long_interval},
  {"encoder_delta_zero", test_encoder_delta_zero},
  {"encoder_latest_cache", test_encoder_latest_cache},
  {"encoder_reset_recovery", test_encoder_reset_recovery},
  {"encoder_time_wrap", test_encoder_time_wrap}
};

int main(int argc, char **argv)
{
  size_t index;

  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <test-name>\n", argv[0]);
    return 2;
  }
  for (index = 0u; index < (sizeof(s_tests) / sizeof(s_tests[0])); index++)
  {
    if (strcmp(argv[1], s_tests[index].name) == 0)
    {
      return s_tests[index].run();
    }
  }
  fprintf(stderr, "unknown test: %s\n", argv[1]);
  return 2;
}
