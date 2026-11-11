/**
  ******************************************************************************
  * @file    sc_lf04.c
  * @brief   LF04 四路反射式循迹传感器采样驱动。
  ******************************************************************************
  */

#include "sc_lf04.h"

#include <stddef.h>

#include "main.h"

static sc_line_sample_t s_latest;
static sc_lf04_state_t s_state;

static uint8_t sc_lf04_pin_config_valid(uint16_t pin)
{
  uint32_t shift;

  if (pin == GPIO_PIN_12)
  {
    shift = 16u;
  }
  else if (pin == GPIO_PIN_13)
  {
    shift = 20u;
  }
  else if (pin == GPIO_PIN_14)
  {
    shift = 24u;
  }
  else if (pin == GPIO_PIN_15)
  {
    shift = 28u;
  }
  else
  {
    return 0u;
  }

  return (uint8_t)(((GPIOB->CRH >> shift) & 0x0Fu) == 0x8u);
}

static uint8_t sc_lf04_config_valid(void)
{
  if ((RCC->APB2ENR & RCC_APB2ENR_IOPBEN) == 0u)
  {
    return 0u;
  }
  if ((LF04_O1_GPIO_Port != GPIOB) ||
      (LF04_O2_GPIO_Port != GPIOB) ||
      (LF04_O3_GPIO_Port != GPIOB) ||
      (LF04_O4_GPIO_Port != GPIOB))
  {
    return 0u;
  }
  if ((LF04_O1_Pin != GPIO_PIN_12) ||
      (LF04_O2_Pin != GPIO_PIN_13) ||
      (LF04_O3_Pin != GPIO_PIN_14) ||
      (LF04_O4_Pin != GPIO_PIN_15))
  {
    return 0u;
  }
  if (!sc_lf04_pin_config_valid(LF04_O1_Pin) ||
      !sc_lf04_pin_config_valid(LF04_O2_Pin) ||
      !sc_lf04_pin_config_valid(LF04_O3_Pin) ||
      !sc_lf04_pin_config_valid(LF04_O4_Pin))
  {
    return 0u;
  }
  return (uint8_t)((GPIOB->ODR & 0xF000u) == 0xF000u);
}

static void sc_lf04_publish_invalid(uint32_t now_ms,
                                    sc_lf04_status_t status,
                                    uint8_t config_failure,
                                    sc_line_sample_t *sample)
{
  s_latest.raw_levels = 0u;
  s_latest.sample_time_ms = now_ms;
  s_latest.valid = 0u;
  s_state.last_status = status;
  s_state.sample_failures++;
  if (config_failure != 0u)
  {
    s_state.config_failures++;
  }
  if (sample != NULL)
  {
    *sample = s_latest;
  }
}

sc_lf04_status_t sc_lf04_init(void)
{
  sc_line_sample_t sample;
  sc_lf04_status_t status;

  if (s_state.initialized != 0u)
  {
    return SC_LF04_OK;
  }
  if (!sc_lf04_config_valid())
  {
    sc_lf04_publish_invalid(HAL_GetTick(), SC_LF04_ERR_CONFIG, 1u, NULL);
    return SC_LF04_ERR_CONFIG;
  }

  s_state.initialized = 1u;
  status = sc_lf04_sample(HAL_GetTick(), &sample);
  if (status != SC_LF04_OK)
  {
    s_state.initialized = 0u;
  }
  return status;
}

sc_lf04_status_t sc_lf04_sample(uint32_t now_ms, sc_line_sample_t *sample)
{
  uint32_t idr;

  if (sample == NULL)
  {
    s_state.last_status = SC_LF04_ERR_ARGUMENT;
    return SC_LF04_ERR_ARGUMENT;
  }
  if (s_state.initialized == 0u)
  {
    sc_lf04_publish_invalid(now_ms, SC_LF04_ERR_NOT_READY, 0u, sample);
    return SC_LF04_ERR_NOT_READY;
  }
  if (!sc_lf04_config_valid())
  {
    sc_lf04_publish_invalid(now_ms, SC_LF04_ERR_CONFIG, 1u, sample);
    return SC_LF04_ERR_CONFIG;
  }

  idr = GPIOB->IDR;
  s_latest.raw_levels = (uint8_t)((idr >> 12u) & 0x0Fu);
  s_latest.sample_time_ms = now_ms;
  s_latest.sequence++;
  s_latest.valid = 1u;
  s_state.last_status = SC_LF04_OK;
  s_state.samples++;
  *sample = s_latest;
  return SC_LF04_OK;
}

sc_lf04_status_t sc_lf04_get_latest(sc_line_sample_t *sample)
{
  if (sample == NULL)
  {
    return SC_LF04_ERR_ARGUMENT;
  }
  if (s_state.initialized == 0u)
  {
    return SC_LF04_ERR_NOT_READY;
  }
  *sample = s_latest;
  return SC_LF04_OK;
}

sc_lf04_status_t sc_lf04_get_status(sc_lf04_state_t *state)
{
  if (state == NULL)
  {
    return SC_LF04_ERR_ARGUMENT;
  }
  *state = s_state;
  return SC_LF04_OK;
}
