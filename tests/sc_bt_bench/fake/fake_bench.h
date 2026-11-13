#ifndef TEST_FAKE_BENCH_H
#define TEST_FAKE_BENCH_H

#include <stddef.h>
#include <stdint.h>

#include "sc_encoder.h"
#include "sc_line_service.h"
#include "sc_tb6612.h"
#include "sc_uart.h"

#define FAKE_BENCH_TX_CAPACITY 64u
#define FAKE_BENCH_TX_LINE_SIZE 1024u

void fake_bench_reset(void);
void fake_bench_inject_rx(const char *data, size_t length);
void fake_bench_clear_tx(void);
size_t fake_bench_tx_count(void);
const char *fake_bench_tx_at(size_t index);

void fake_bench_fail_next_read(void);
void fake_bench_fail_next_stats(void);
void fake_bench_fail_next_write(void);
void fake_bench_full_next_write(void);
void fake_bench_fail_next_standby_enable(void);
void fake_bench_fail_next_standby_disable(void);
void fake_bench_fail_next_apply(void);
void fake_bench_fail_next_encoder_reset(void);
void fake_bench_fail_encoder_snapshot(void);
void fake_bench_fail_tb_state(void);
void fake_bench_fail_line_snapshot(void);

sc_uart_stats_t *fake_bench_uart_stats(void);
sc_tb6612_state_t *fake_bench_tb_state(void);
sc_encoder_snapshot_t *fake_bench_encoder_snapshot(void);
sc_line_state_t *fake_bench_line_state(void);
void fake_bench_advance_encoder(uint32_t sample_time_ms,
                                int32_t left_delta,
                                int32_t right_delta);

uint32_t fake_bench_standby_calls(void);
uint8_t fake_bench_last_standby_enable(void);
uint32_t fake_bench_apply_calls(void);
int16_t fake_bench_last_left(void);
int16_t fake_bench_last_right(void);
uint32_t fake_bench_encoder_reset_calls(void);

void fake_bench_clear_calls(void);

#endif /* TEST_FAKE_BENCH_H */
