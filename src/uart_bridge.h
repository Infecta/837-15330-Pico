#ifndef UART_BRIDGE_H
#define UART_BRIDGE_H

#include <stdint.h>

/* Lifetime counters; read on core 0. Never inserted into raw CDC 0 traffic. */
typedef struct {
    uint32_t rx_overflow;
    uint32_t rx_framing_errors;
    uint32_t rx_parity_errors;
    uint32_t rx_breaks;
    uint32_t rx_overrun_flags;
    uint32_t rx_discarded;
    uint32_t tx_discarded;
    uint32_t session_resets;
} uart_bridge_stats_t;

void uart_bridge_init(void);
void uart_bridge_task(void);
void uart_bridge_get_stats(uart_bridge_stats_t *stats);

#endif
