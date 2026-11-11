#ifndef TEST_SC_LINE_FAKE_SUPPORT_H
#define TEST_SC_LINE_FAKE_SUPPORT_H

#include <stddef.h>
#include <stdint.h>

void fake_line_reset(void);
void fake_line_set_tick(uint32_t tick);
void fake_line_set_raw(uint8_t raw_levels);
void fake_line_set_idr(uint32_t idr);
void fake_line_set_clock(uint8_t enabled);
void fake_line_set_pin_config(uint16_t pin, uint8_t nibble);
void fake_line_set_pull(uint16_t pin, uint8_t enabled);

void fake_bench_clear_tx(void);
void fake_bench_inject_rx(const char *data, size_t length);
size_t fake_bench_tx_count(void);
const char *fake_bench_tx_at(size_t index);

#endif /* TEST_SC_LINE_FAKE_SUPPORT_H */
