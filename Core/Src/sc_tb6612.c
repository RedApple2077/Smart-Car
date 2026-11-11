/**
  ******************************************************************************
  * @file    sc_tb6612.c
  * @brief   TB6612FNG 双路电机输出驱动实现。
  ******************************************************************************
  */

#include "sc_tb6612.h"

#include <stddef.h>

#include "main.h"

#define SC_TB6612_PWM_PERIOD 3599u

extern TIM_HandleTypeDef htim4;

static sc_tb6612_state_t s_state;

static uint32_t sc_tb6612_command_ccr(int16_t command)
{
  uint32_t magnitude = (command < 0) ?
    (uint32_t)(-command) : (uint32_t)command;

  if (command == 0)
  {
    return 3600u;
  }
  return ((magnitude * 3600u) + 500u) / 1000u;
}

static int8_t sc_tb6612_command_direction(int16_t command)
{
  if (command > 0)
  {
    return 1;
  }
  if (command < 0)
  {
    return -1;
  }
  return 0;
}

static void sc_tb6612_set_compare(uint32_t left_ccr, uint32_t right_ccr)
{
  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_1, left_ccr);
  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_2, right_ccr);
  s_state.left_ccr = left_ccr;
  s_state.right_ccr = right_ccr;
}

static void sc_tb6612_set_direction(int16_t left, int16_t right)
{
  int8_t left_direction = sc_tb6612_command_direction(left);
  int8_t right_direction = sc_tb6612_command_direction(right);

  HAL_GPIO_WritePin(AIN1_GPIO_Port, AIN1_Pin,
                    (left_direction > 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(AIN2_GPIO_Port, AIN2_Pin,
                    (left_direction < 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(BIN1_GPIO_Port, BIN1_Pin,
                    (right_direction > 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(BIN2_GPIO_Port, BIN2_Pin,
                    (right_direction < 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void sc_tb6612_set_gpio_safe(void)
{
  HAL_GPIO_WritePin(TB6612_STBY_GPIO_Port, TB6612_STBY_Pin,
                    GPIO_PIN_RESET);
  sc_tb6612_set_direction(0, 0);
  s_state.standby = 0u;
}

static HAL_StatusTypeDef sc_tb6612_load_safe_outputs(void)
{
  sc_tb6612_set_gpio_safe();
  sc_tb6612_set_compare(0u, 0u);
  return HAL_TIM_GenerateEvent(&htim4, TIM_EVENTSOURCE_UPDATE);
}

static void sc_tb6612_record_update_failure(HAL_StatusTypeDef hal_status)
{
  s_state.update_failures++;
  s_state.last_hal_error = (uint32_t)hal_status;
}

static void sc_tb6612_enter_fault(void)
{
  HAL_StatusTypeDef hal_status = sc_tb6612_load_safe_outputs();

  if (hal_status != HAL_OK)
  {
    sc_tb6612_record_update_failure(hal_status);
  }
  s_state.enabled = 0u;
  s_state.mode = SC_TB6612_MODE_FAULT;
  s_state.left_command = 0;
  s_state.right_command = 0;
  s_state.safe_stop_count++;
}

static sc_tb6612_status_t sc_tb6612_validate_config(void)
{
  if ((htim4.Instance != TIM4) ||
      (htim4.Init.Prescaler != 0u) ||
      (htim4.Init.Period != SC_TB6612_PWM_PERIOD))
  {
    return SC_TB6612_ERR_CONFIG;
  }
  return SC_TB6612_OK;
}

sc_tb6612_status_t sc_tb6612_init(void)
{
  HAL_StatusTypeDef hal_status;
  sc_tb6612_status_t status;

  if (s_state.initialized != 0u)
  {
    return SC_TB6612_OK;
  }

  sc_tb6612_set_gpio_safe();
  status = sc_tb6612_validate_config();
  if (status != SC_TB6612_OK)
  {
    s_state.enabled = 0u;
    s_state.mode = SC_TB6612_MODE_FAULT;
    s_state.last_status = status;
    return status;
  }

  hal_status = HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_1);
  if (hal_status != HAL_OK)
  {
    s_state.pwm_start_failures++;
    s_state.last_hal_error = (uint32_t)hal_status;
    sc_tb6612_enter_fault();
    s_state.last_status = SC_TB6612_ERR_HAL;
    return SC_TB6612_ERR_HAL;
  }

  hal_status = HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_2);
  if (hal_status != HAL_OK)
  {
    HAL_StatusTypeDef stop_status;

    s_state.pwm_start_failures++;
    s_state.last_hal_error = (uint32_t)hal_status;
    stop_status = HAL_TIM_PWM_Stop(&htim4, TIM_CHANNEL_1);
    if (stop_status != HAL_OK)
    {
      s_state.last_hal_error = (uint32_t)stop_status;
    }
    s_state.rollback_count++;
    sc_tb6612_enter_fault();
    s_state.last_status = SC_TB6612_ERR_HAL;
    return SC_TB6612_ERR_HAL;
  }

  hal_status = sc_tb6612_load_safe_outputs();
  if (hal_status != HAL_OK)
  {
    HAL_StatusTypeDef stop1_status;
    HAL_StatusTypeDef stop2_status;

    sc_tb6612_record_update_failure(hal_status);
    stop1_status = HAL_TIM_PWM_Stop(&htim4, TIM_CHANNEL_1);
    if (stop1_status != HAL_OK)
    {
      s_state.last_hal_error = (uint32_t)stop1_status;
    }
    stop2_status = HAL_TIM_PWM_Stop(&htim4, TIM_CHANNEL_2);
    if (stop2_status != HAL_OK)
    {
      s_state.last_hal_error = (uint32_t)stop2_status;
    }
    s_state.rollback_count++;
    s_state.safe_stop_count++;
    s_state.enabled = 0u;
    s_state.mode = SC_TB6612_MODE_FAULT;
    s_state.left_command = 0;
    s_state.right_command = 0;
    s_state.last_status = SC_TB6612_ERR_HAL;
    return SC_TB6612_ERR_HAL;
  }

  s_state.initialized = 1u;
  s_state.enabled = 0u;
  s_state.standby = 0u;
  s_state.mode = SC_TB6612_MODE_STANDBY;
  s_state.last_status = SC_TB6612_OK;
  s_state.left_command = 0;
  s_state.right_command = 0;
  s_state.left_ccr = 0u;
  s_state.right_ccr = 0u;
  return SC_TB6612_OK;
}

sc_tb6612_status_t sc_tb6612_set_standby(uint8_t enable)
{
  HAL_StatusTypeDef hal_status;

  if (enable > 1u)
  {
    s_state.rejected_requests++;
    s_state.last_status = SC_TB6612_ERR_ARGUMENT;
    return SC_TB6612_ERR_ARGUMENT;
  }
  if (s_state.initialized == 0u)
  {
    s_state.last_status = SC_TB6612_ERR_NOT_READY;
    return SC_TB6612_ERR_NOT_READY;
  }

  if (enable == 0u)
  {
    s_state.left_command = 0;
    s_state.right_command = 0;
    s_state.enabled = 0u;
    hal_status = sc_tb6612_load_safe_outputs();
    if (hal_status != HAL_OK)
    {
      sc_tb6612_record_update_failure(hal_status);
      s_state.safe_stop_count++;
      s_state.mode = SC_TB6612_MODE_FAULT;
      s_state.last_status = SC_TB6612_ERR_HAL;
      return SC_TB6612_ERR_HAL;
    }
    if (s_state.mode == SC_TB6612_MODE_FAULT)
    {
      s_state.last_status = SC_TB6612_ERR_FAULT;
      return SC_TB6612_ERR_FAULT;
    }
    s_state.mode = SC_TB6612_MODE_STANDBY;
    s_state.last_status = SC_TB6612_OK;
    return SC_TB6612_OK;
  }

  if (s_state.mode == SC_TB6612_MODE_FAULT)
  {
    s_state.last_status = SC_TB6612_ERR_FAULT;
    return SC_TB6612_ERR_FAULT;
  }
  if (s_state.mode == SC_TB6612_MODE_ENABLED)
  {
    return SC_TB6612_OK;
  }
  if (s_state.mode != SC_TB6612_MODE_STANDBY)
  {
    s_state.last_status = SC_TB6612_ERR_NOT_READY;
    return SC_TB6612_ERR_NOT_READY;
  }

  sc_tb6612_set_gpio_safe();
  sc_tb6612_set_compare(3600u, 3600u);
  hal_status = HAL_TIM_GenerateEvent(&htim4, TIM_EVENTSOURCE_UPDATE);
  if (hal_status != HAL_OK)
  {
    sc_tb6612_record_update_failure(hal_status);
    sc_tb6612_enter_fault();
    s_state.last_status = SC_TB6612_ERR_HAL;
    return SC_TB6612_ERR_HAL;
  }

  HAL_GPIO_WritePin(TB6612_STBY_GPIO_Port, TB6612_STBY_Pin,
                    GPIO_PIN_SET);
  s_state.standby = 1u;
  s_state.enabled = 1u;
  s_state.mode = SC_TB6612_MODE_ENABLED;
  s_state.left_command = 0;
  s_state.right_command = 0;
  s_state.last_status = SC_TB6612_OK;
  return SC_TB6612_OK;
}

sc_tb6612_status_t sc_tb6612_apply(int16_t left_permille,
                                   int16_t right_permille)
{
  uint32_t left_ccr;
  uint32_t right_ccr;
  int8_t left_direction;
  int8_t right_direction;
  HAL_StatusTypeDef hal_status;

  if (s_state.initialized == 0u)
  {
    s_state.last_status = SC_TB6612_ERR_NOT_READY;
    return SC_TB6612_ERR_NOT_READY;
  }
  if (s_state.mode == SC_TB6612_MODE_FAULT)
  {
    s_state.last_status = SC_TB6612_ERR_FAULT;
    return SC_TB6612_ERR_FAULT;
  }
  if (s_state.mode != SC_TB6612_MODE_ENABLED)
  {
    s_state.last_status = SC_TB6612_ERR_NOT_READY;
    return SC_TB6612_ERR_NOT_READY;
  }
  if ((left_permille < -SC_TB6612_COMMAND_MAX) ||
      (left_permille > SC_TB6612_COMMAND_MAX) ||
      (right_permille < -SC_TB6612_COMMAND_MAX) ||
      (right_permille > SC_TB6612_COMMAND_MAX))
  {
    s_state.rejected_requests++;
    sc_tb6612_enter_fault();
    s_state.last_status = SC_TB6612_ERR_ARGUMENT;
    return SC_TB6612_ERR_ARGUMENT;
  }

  left_ccr = sc_tb6612_command_ccr(left_permille);
  right_ccr = sc_tb6612_command_ccr(right_permille);
  left_direction = sc_tb6612_command_direction(left_permille);
  right_direction = sc_tb6612_command_direction(right_permille);

  if ((left_direction != 0) && (right_direction != 0) &&
      (left_direction == sc_tb6612_command_direction(
        s_state.left_command)) &&
      (right_direction == sc_tb6612_command_direction(
        s_state.right_command)))
  {
    sc_tb6612_set_compare(left_ccr, right_ccr);
    s_state.left_command = left_permille;
    s_state.right_command = right_permille;
    s_state.last_status = SC_TB6612_OK;
    return SC_TB6612_OK;
  }

  HAL_GPIO_WritePin(TB6612_STBY_GPIO_Port, TB6612_STBY_Pin,
                    GPIO_PIN_RESET);
  s_state.standby = 0u;
  sc_tb6612_set_direction(left_permille, right_permille);
  sc_tb6612_set_compare(left_ccr, right_ccr);
  hal_status = HAL_TIM_GenerateEvent(&htim4, TIM_EVENTSOURCE_UPDATE);
  if (hal_status != HAL_OK)
  {
    sc_tb6612_record_update_failure(hal_status);
    sc_tb6612_enter_fault();
    s_state.last_status = SC_TB6612_ERR_HAL;
    return SC_TB6612_ERR_HAL;
  }

  HAL_GPIO_WritePin(TB6612_STBY_GPIO_Port, TB6612_STBY_Pin,
                    GPIO_PIN_SET);
  s_state.standby = 1u;
  s_state.left_command = left_permille;
  s_state.right_command = right_permille;
  s_state.enabled = 1u;
  s_state.mode = SC_TB6612_MODE_ENABLED;
  s_state.last_status = SC_TB6612_OK;
  return SC_TB6612_OK;
}

sc_tb6612_status_t sc_tb6612_get_state(sc_tb6612_state_t *state)
{
  if (state == NULL)
  {
    return SC_TB6612_ERR_ARGUMENT;
  }
  *state = s_state;
  return SC_TB6612_OK;
}
