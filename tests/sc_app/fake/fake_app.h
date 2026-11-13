#ifndef TEST_FAKE_APP_H
#define TEST_FAKE_APP_H

#include <stddef.h>
#include <stdint.h>

#include "sc_button.h"
#include "sc_encoder.h"
#include "sc_line_service.h"
#include "sc_tb6612.h"

#define FAKE_APP_EVENT_CAPACITY 64u

typedef enum {
  FAKE_TB_EVENT_SET_STANDBY = 0,
  FAKE_TB_EVENT_APPLY,
  FAKE_TB_EVENT_EMERGENCY
} fake_tb_event_kind_t;

typedef struct {
  fake_tb_event_kind_t kind;
  int16_t left;
  int16_t right;
  uint8_t value;
} fake_tb_event_t;

void fake_app_reset(void);

void fake_line_set_mask(uint8_t black_mask);
void fake_line_set_raw(uint8_t raw_levels);
void fake_line_fail_next(void);

void fake_encoder_set_deltas(int32_t left_delta, int32_t right_delta);
void fake_encoder_set_valid(uint8_t left_valid, uint8_t right_valid);
void fake_encoder_set_lost_sync(uint8_t left_lost, uint8_t right_lost);
void fake_encoder_freeze(uint8_t frozen);
void fake_encoder_fail_next(void);

void fake_button_queue(uint8_t events);

void fake_tb_fail_init(void);
void fake_tb_fail_apply(void);
void fake_tb_fail_standby(void);
void fake_tb_fail_get(void);

size_t fake_tb_event_count(void);
const fake_tb_event_t *fake_tb_event_at(size_t index);
void fake_tb_clear_events(void);
const sc_tb6612_state_t *fake_tb_state(void);

uint32_t fake_emergency_count(void);
uint32_t fake_led_set_count(void);
uint8_t fake_led_last_value(void);

#endif /* TEST_FAKE_APP_H */
