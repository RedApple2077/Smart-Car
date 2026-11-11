#include "main.h"

#include <string.h>

#define FAKE_EVENT_CAPACITY 256u

TIM_TypeDef fake_tim2_regs;
TIM_TypeDef fake_tim3_regs;
TIM_TypeDef fake_tim4_regs;
GPIO_TypeDef fake_gpio_a;
GPIO_TypeDef fake_gpio_b;
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim4;

static fake_event_t s_events[FAKE_EVENT_CAPACITY];
static size_t s_event_count;
static uint16_t s_gpio_a_level;
static uint16_t s_gpio_b_level;
static uint32_t s_counter_read_count;

static void fake_record_event(fake_event_kind_t kind,
                              TIM_HandleTypeDef *tim,
                              uint32_t channel,
                              GPIO_TypeDef *port,
                              uint16_t pin,
                              uint32_t value)
{
  fake_event_t *event;

  if (s_event_count >= FAKE_EVENT_CAPACITY)
  {
    return;
  }
  event = &s_events[s_event_count++];
  event->kind = kind;
  event->tim = tim;
  event->channel = channel;
  event->port = port;
  event->pin = pin;
  event->value = value;
}

void fake_hal_reset(void)
{
  memset(&htim2, 0, sizeof(htim2));
  memset(&htim3, 0, sizeof(htim3));
  memset(&htim4, 0, sizeof(htim4));
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 0u;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 65535u;
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 0u;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 65535u;
  htim4.Instance = TIM4;
  htim4.Init.Prescaler = 0u;
  htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4.Init.Period = 3599u;

  s_gpio_a_level = 0u;
  s_gpio_b_level = 0u;
  s_counter_read_count = 0u;
  fake_clear_events();
}

void fake_clear_events(void)
{
  memset(s_events, 0, sizeof(s_events));
  s_event_count = 0u;
}

size_t fake_event_count(void)
{
  return s_event_count;
}

const fake_event_t *fake_event_at(size_t index)
{
  if (index >= s_event_count)
  {
    return NULL;
  }
  return &s_events[index];
}

int fake_find_event(fake_event_kind_t kind, size_t start)
{
  size_t index;

  for (index = start; index < s_event_count; index++)
  {
    if (s_events[index].kind == kind)
    {
      return (int)index;
    }
  }
  return -1;
}

GPIO_PinState fake_gpio_level(GPIO_TypeDef *port, uint16_t pin)
{
  uint16_t *level = (port == &fake_gpio_a) ?
    &s_gpio_a_level : &s_gpio_b_level;

  return ((*level & pin) != 0u) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

uint32_t fake_counter_read_count(void)
{
  return s_counter_read_count;
}

void fake_fail_next_pwm_start(uint32_t channel)
{
  if (channel == TIM_CHANNEL_1)
  {
    htim4.fake_fail_pwm_ch1 = 1u;
  }
  else if (channel == TIM_CHANNEL_2)
  {
    htim4.fake_fail_pwm_ch2 = 1u;
  }
}

void fake_fail_next_encoder_start(TIM_HandleTypeDef *htim)
{
  htim->fake_fail_encoder_start = 1u;
}

void fake_fail_next_generate_event(void)
{
  htim4.fake_fail_generate_event = 1u;
}

HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *htim,
                                    uint32_t channel)
{
  fake_record_event(FAKE_EVENT_PWM_START, htim, channel, NULL, 0u, 0u);
  if ((htim != &htim4) ||
      ((channel != TIM_CHANNEL_1) && (channel != TIM_CHANNEL_2)))
  {
    return HAL_ERROR;
  }
  if ((channel == TIM_CHANNEL_1) && (htim->fake_fail_pwm_ch1 != 0u))
  {
    htim->fake_fail_pwm_ch1 = 0u;
    return HAL_ERROR;
  }
  if ((channel == TIM_CHANNEL_2) && (htim->fake_fail_pwm_ch2 != 0u))
  {
    htim->fake_fail_pwm_ch2 = 0u;
    return HAL_ERROR;
  }
  if (channel == TIM_CHANNEL_1)
  {
    htim->fake_pwm_ch1_started = 1u;
  }
  else
  {
    htim->fake_pwm_ch2_started = 1u;
  }
  return HAL_OK;
}

HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *htim,
                                   uint32_t channel)
{
  fake_record_event(FAKE_EVENT_PWM_STOP, htim, channel, NULL, 0u, 0u);
  if (htim != &htim4)
  {
    return HAL_ERROR;
  }
  if (channel == TIM_CHANNEL_1)
  {
    htim->fake_pwm_ch1_started = 0u;
  }
  else if (channel == TIM_CHANNEL_2)
  {
    htim->fake_pwm_ch2_started = 0u;
  }
  return HAL_OK;
}

HAL_StatusTypeDef HAL_TIM_Encoder_Start(TIM_HandleTypeDef *htim,
                                        uint32_t channel)
{
  fake_record_event(FAKE_EVENT_ENCODER_START, htim, channel, NULL, 0u, 0u);
  if ((channel != TIM_CHANNEL_ALL) || (htim->fake_fail_encoder_start != 0u))
  {
    htim->fake_fail_encoder_start = 0u;
    return HAL_ERROR;
  }
  htim->fake_encoder_started = 1u;
  return HAL_OK;
}

HAL_StatusTypeDef HAL_TIM_Encoder_Stop(TIM_HandleTypeDef *htim,
                                       uint32_t channel)
{
  fake_record_event(FAKE_EVENT_ENCODER_STOP, htim, channel, NULL, 0u, 0u);
  if (htim->fake_encoder_started != 0u)
  {
    htim->fake_encoder_started = 0u;
  }
  return HAL_OK;
}

HAL_StatusTypeDef HAL_TIM_GenerateEvent(TIM_HandleTypeDef *htim,
                                        uint32_t event_source)
{
  fake_record_event(FAKE_EVENT_TIM_UPDATE, htim, event_source, NULL, 0u, 0u);
  if ((htim != &htim4) ||
      (event_source != TIM_EVENTSOURCE_UPDATE) ||
      (htim->fake_fail_generate_event != 0u))
  {
    htim->fake_fail_generate_event = 0u;
    return HAL_ERROR;
  }
  return HAL_OK;
}

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin,
                       GPIO_PinState state)
{
  uint16_t *level = (port == &fake_gpio_a) ?
    &s_gpio_a_level : &s_gpio_b_level;

  fake_record_event(FAKE_EVENT_GPIO_WRITE, NULL, 0u, port, pin,
                    (uint32_t)state);
  if (state == GPIO_PIN_SET)
  {
    *level |= pin;
  }
  else
  {
    *level &= (uint16_t)~pin;
  }
}

void fake_tim_set_compare(TIM_HandleTypeDef *htim, uint32_t channel,
                          uint32_t value)
{
  fake_record_event(FAKE_EVENT_SET_COMPARE, htim, channel, NULL, 0u, value);
  if (channel == TIM_CHANNEL_1)
  {
    htim->fake_ccr1 = (uint16_t)value;
  }
  else if (channel == TIM_CHANNEL_2)
  {
    htim->fake_ccr2 = (uint16_t)value;
  }
}

uint32_t fake_tim_get_counter(const TIM_HandleTypeDef *htim)
{
  s_counter_read_count++;
  return htim->fake_counter;
}

void fake_tim_set_counter(TIM_HandleTypeDef *htim, uint16_t value)
{
  fake_record_event(FAKE_EVENT_SET_COUNTER, (TIM_HandleTypeDef *)htim,
                    0u, NULL, 0u, value);
  htim->fake_counter = value;
}
