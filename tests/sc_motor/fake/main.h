#ifndef TEST_FAKE_MAIN_H
#define TEST_FAKE_MAIN_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
  HAL_OK = 0,
  HAL_ERROR = 1,
  HAL_BUSY = 2,
  HAL_TIMEOUT = 3
} HAL_StatusTypeDef;

typedef enum {
  GPIO_PIN_RESET = 0,
  GPIO_PIN_SET = 1
} GPIO_PinState;

typedef struct {
  uint32_t unused;
} GPIO_TypeDef;

typedef struct {
  uint32_t Prescaler;
  uint32_t CounterMode;
  uint32_t Period;
  uint32_t ClockDivision;
  uint32_t AutoReloadPreload;
} TIM_Base_InitTypeDef;

typedef struct {
  uint32_t unused;
} TIM_TypeDef;

typedef struct {
  TIM_TypeDef *Instance;
  TIM_Base_InitTypeDef Init;
  uint16_t fake_ccr1;
  uint16_t fake_ccr2;
  uint16_t fake_counter;
  uint8_t fake_pwm_ch1_started;
  uint8_t fake_pwm_ch2_started;
  uint8_t fake_encoder_started;
  uint8_t fake_fail_pwm_ch1;
  uint8_t fake_fail_pwm_ch2;
  uint8_t fake_fail_encoder_start;
  uint8_t fake_fail_generate_event;
} TIM_HandleTypeDef;

#define TIM_COUNTERMODE_UP 0x00000000u
#define TIM_CHANNEL_1      0x00000004u
#define TIM_CHANNEL_2      0x00000008u
#define TIM_CHANNEL_ALL    0x0000000Cu
#define TIM_EVENTSOURCE_UPDATE 0x00000001u

extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim3;
extern TIM_HandleTypeDef htim4;

extern TIM_TypeDef fake_tim2_regs;
extern TIM_TypeDef fake_tim3_regs;
extern TIM_TypeDef fake_tim4_regs;

#define TIM2 (&fake_tim2_regs)
#define TIM3 (&fake_tim3_regs)
#define TIM4 (&fake_tim4_regs)

extern GPIO_TypeDef fake_gpio_a;
extern GPIO_TypeDef fake_gpio_b;

#define AIN1_Pin          ((uint16_t)0x0010u)
#define AIN1_GPIO_Port    (&fake_gpio_a)
#define AIN2_Pin          ((uint16_t)0x0020u)
#define AIN2_GPIO_Port    (&fake_gpio_a)
#define BIN1_Pin          ((uint16_t)0x0001u)
#define BIN1_GPIO_Port    (&fake_gpio_b)
#define BIN2_Pin          ((uint16_t)0x0002u)
#define BIN2_GPIO_Port    (&fake_gpio_b)
#define TB6612_STBY_Pin   ((uint16_t)0x0100u)
#define TB6612_STBY_GPIO_Port (&fake_gpio_b)

typedef enum {
  FAKE_EVENT_PWM_START = 0,
  FAKE_EVENT_PWM_STOP,
  FAKE_EVENT_ENCODER_START,
  FAKE_EVENT_ENCODER_STOP,
  FAKE_EVENT_TIM_UPDATE,
  FAKE_EVENT_GPIO_WRITE,
  FAKE_EVENT_SET_COMPARE,
  FAKE_EVENT_SET_COUNTER
} fake_event_kind_t;

typedef struct {
  fake_event_kind_t kind;
  TIM_HandleTypeDef *tim;
  uint32_t channel;
  GPIO_TypeDef *port;
  uint16_t pin;
  uint32_t value;
} fake_event_t;

HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *htim,
                                    uint32_t channel);
HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *htim,
                                   uint32_t channel);
HAL_StatusTypeDef HAL_TIM_Encoder_Start(TIM_HandleTypeDef *htim,
                                        uint32_t channel);
HAL_StatusTypeDef HAL_TIM_Encoder_Stop(TIM_HandleTypeDef *htim,
                                       uint32_t channel);
HAL_StatusTypeDef HAL_TIM_GenerateEvent(TIM_HandleTypeDef *htim,
                                        uint32_t event_source);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin,
                       GPIO_PinState state);

void fake_tim_set_compare(TIM_HandleTypeDef *htim, uint32_t channel,
                          uint32_t value);
uint32_t fake_tim_get_counter(const TIM_HandleTypeDef *htim);
void fake_tim_set_counter(TIM_HandleTypeDef *htim, uint16_t value);

#define __HAL_TIM_SET_COMPARE(handle, channel, value) \
  fake_tim_set_compare((handle), (channel), (uint32_t)(value))
#define __HAL_TIM_GET_COUNTER(handle) \
  fake_tim_get_counter((handle))
#define __HAL_TIM_SET_COUNTER(handle, value) \
  fake_tim_set_counter((handle), (uint16_t)(value))

void fake_hal_reset(void);
void fake_clear_events(void);
size_t fake_event_count(void);
const fake_event_t *fake_event_at(size_t index);
int fake_find_event(fake_event_kind_t kind, size_t start);
GPIO_PinState fake_gpio_level(GPIO_TypeDef *port, uint16_t pin);
uint32_t fake_counter_read_count(void);
void fake_fail_next_pwm_start(uint32_t channel);
void fake_fail_next_encoder_start(TIM_HandleTypeDef *htim);
void fake_fail_next_generate_event(void);

#endif /* TEST_FAKE_MAIN_H */
