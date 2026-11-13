#ifndef TEST_FAKE_TELEMETRY_H
#define TEST_FAKE_TELEMETRY_H

#include <stddef.h>
#include <stdint.h>

#include "sc_ssd1306.h"

void fake_telemetry_reset(void);
void fake_uart_fail_next(void);
size_t fake_uart_call_count(void);
const char *fake_uart_last(void);

void fake_oled_fail_next_write(void);
void fake_oled_fail_next_refresh(void);
void fake_oled_fail_next_service(void);
size_t fake_oled_write_count(void);
size_t fake_oled_refresh_count(void);
size_t fake_oled_service_count(void);
const char *fake_oled_line(uint8_t row);
const sc_ssd1306_status_info_t *fake_oled_status(void);

uint32_t fake_led_call_count(void);
uint8_t fake_led_last(void);

#endif /* TEST_FAKE_TELEMETRY_H */
