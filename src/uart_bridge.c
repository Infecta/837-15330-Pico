#include "uart_bridge.h"

#include <stdbool.h>
#include <stdint.h>

#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"
#include "tusb.h"

#include "led_driver.h"
#include "sega_protocol.h"

#define CY8C_UART uart0
#define CY8C_UART_IRQ UART0_IRQ

#define CY8C_TX_PIN 12
#define CY8C_RX_PIN 13

#define DEFAULT_BAUD_RATE 115200u

// Must be a power of two.
// 8191 usable bytes: about 711 ms at continuous 115200/8N1.
#define RING_CAPACITY 8192
#define RING_MASK (RING_CAPACITY - 1u)

#define TRANSFER_BUDGET 64

typedef struct {
    uint8_t data[RING_CAPACITY];
    volatile uint32_t head;
    volatile uint32_t tail;
} byte_ring_t;

/*
 * CY8C -> USB:
 * - written by the UART interrupt
 * - read by the main loop
 */
static byte_ring_t uart_to_usb;

/*
 * USB -> CY8C:
 * - written and read by the main loop
 * - still useful because USB and UART do not run at identical speeds
 */
static byte_ring_t usb_to_uart;

static sega_protocol_t led_parser;
static uint32_t led_frame[LED_COUNT];

static volatile uart_bridge_stats_t bridge_stats;
static volatile bool session_active;
static bool host_dtr;

static bool ring_push(byte_ring_t *ring, uint8_t value) {
    uint32_t next = (ring->head + 1) & RING_MASK;

    if (next == ring->tail) {
        return false; // Ring is full.
    }

    ring->data[ring->head] = value;
    __dmb(); // Publish the byte before the ISR/main-loop shared head.
    ring->head = next;

    return true;
}

static bool ring_pop(byte_ring_t *ring, uint8_t *value) {
    if (ring->tail == ring->head) {
        return false; // Ring is empty.
    }

    *value = ring->data[ring->tail];
    __dmb();
    ring->tail = (ring->tail + 1) & RING_MASK;

    return true;
}

static uint32_t ring_free(const byte_ring_t *ring) {
    return (ring->tail - ring->head - 1) & RING_MASK;
}

/*
 * This runs immediately when the CY8C sends a UART byte.
 * It does not parse packets, write USB data, or drive LEDs.
 */
static void cy8c_uart_rx_irq(void) {
    while (uart_is_readable(CY8C_UART)) {
        uint32_t sample = uart_get_hw(CY8C_UART)->dr;
        if (sample & UART_UARTDR_OE_BITS) {
            // Indicates prior loss; the current byte may still be valid.
            bridge_stats.rx_overrun_flags++;
        }
        if (sample & UART_UARTDR_FE_BITS) bridge_stats.rx_framing_errors++;
        if (sample & UART_UARTDR_PE_BITS) bridge_stats.rx_parity_errors++;
        if (sample & UART_UARTDR_BE_BITS) bridge_stats.rx_breaks++;
        if (!session_active ||
            (sample & (UART_UARTDR_FE_BITS | UART_UARTDR_PE_BITS |
                       UART_UARTDR_BE_BITS))) {
            bridge_stats.rx_discarded++;
            continue;
        }
        if (!ring_push(&uart_to_usb, (uint8_t)sample)) {
            bridge_stats.rx_overflow++;
        }
    }
}

void uart_bridge_init(void) {
    uart_init(CY8C_UART, DEFAULT_BAUD_RATE);

    gpio_set_function(CY8C_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(CY8C_RX_PIN, GPIO_FUNC_UART);

    uart_set_format(CY8C_UART, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(CY8C_UART, false, false);
    uart_set_fifo_enabled(CY8C_UART, true);

    sega_protocol_reset(&led_parser);

    irq_set_exclusive_handler(CY8C_UART_IRQ, cy8c_uart_rx_irq);
    irq_set_enabled(CY8C_UART_IRQ, true);

    // Enable receive interrupts only. TX remains main-loop driven.
    uart_set_irq_enables(CY8C_UART, true, false);
}

/* Core 0 only. Abort queues from the old session, including the UART FIFOs.
 * DTR is optional: some slider clients use CDC without asserting it.
 */
static void reset_session(bool active) {
    irq_set_enabled(CY8C_UART_IRQ, false);
    session_active = false;
    bridge_stats.rx_discarded +=
        (uart_to_usb.head - uart_to_usb.tail) & RING_MASK;
    bridge_stats.tx_discarded +=
        (usb_to_uart.head - usb_to_uart.tail) & RING_MASK;
    uart_to_usb.head = uart_to_usb.tail = 0;
    usb_to_uart.head = usb_to_uart.tail = 0;
    sega_protocol_reset(&led_parser);
    // uart_init resets the peripheral, removing stale RX and queued TX.
    uart_init(CY8C_UART, DEFAULT_BAUD_RATE);
    uart_set_hw_flow(CY8C_UART, false, false);
    uart_set_irq_enables(CY8C_UART, true, false);
    tud_cdc_n_read_flush(0);
    tud_cdc_n_write_clear(0);
    bridge_stats.session_resets++;
    session_active = active;
    irq_set_enabled(CY8C_UART_IRQ, true);
}

void tud_mount_cb(void) {
    host_dtr = false;
    reset_session(true);
}

void tud_umount_cb(void) {
    host_dtr = false;
    reset_session(false);
}

void tud_cdc_line_state_cb(uint8_t instance, bool dtr, bool rts) {
    (void)rts;
    if (instance == 0 && dtr != host_dtr) {
        host_dtr = dtr;
        reset_session(tud_mounted());
    }
}

void uart_bridge_get_stats(uart_bridge_stats_t *stats) {
    if (stats == NULL) return;
    uint32_t interrupts = save_and_disable_interrupts();
    *stats = bridge_stats;
    restore_interrupts(interrupts);
}

static void receive_usb_bytes(void) {
    uint8_t buffer[TRANSFER_BUDGET];

    uint32_t available = tud_cdc_available();
    uint32_t free_bytes = ring_free(&usb_to_uart);

    if (available > free_bytes) {
        available = free_bytes;
    }

    if (available > TRANSFER_BUDGET) {
        available = TRANSFER_BUDGET;
    }

    if (available == 0) {
        return;
    }

    uint32_t received = tud_cdc_read(buffer, available);

    for (uint32_t i = 0; i < received; i++) {
        ring_push(&usb_to_uart, buffer[i]);
    }
}

static void transmit_uart_bytes(void) {
    for (uint32_t i = 0; i < TRANSFER_BUDGET; i++) {
        uint8_t byte;

        if (!uart_is_writable(CY8C_UART)) {
            break;
        }

        if (!ring_pop(&usb_to_uart, &byte)) {
            break;
        }

        // Forward first: never let LED parsing alter CY8C traffic.
        uart_putc_raw(CY8C_UART, byte);

        // Observe a copy after forwarding.
        if (sega_protocol_feed(&led_parser, byte, led_frame)) {
            led_driver_submit(led_frame);
        }
    }
}

static void transmit_usb_bytes(void) {
    uint8_t buffer[TRANSFER_BUDGET];
    uint32_t count = 0;

    uint32_t writable = tud_cdc_write_available();

    if (writable > TRANSFER_BUDGET) {
        writable = TRANSFER_BUDGET;
    }

    /*
     * Do not advance the actual ring tail yet.
     * We only remove bytes after TinyUSB accepts them.
     */
    uint32_t temporary_tail = uart_to_usb.tail;

    while (count < writable && temporary_tail != uart_to_usb.head) {
        buffer[count] = uart_to_usb.data[temporary_tail];
        temporary_tail = (temporary_tail + 1) & RING_MASK;
        count++;
    }

    if (count > 0) {
        uint32_t written = tud_cdc_write(buffer, count);
        __dmb(); // Finish copying before allowing the ISR to reuse slots.
        uart_to_usb.tail = (uart_to_usb.tail + written) & RING_MASK;
    }

    tud_cdc_write_flush();
}

void uart_bridge_task(void) {
    // TinyUSB does not call umount_cb on a USB bus reset.
    if (!tud_mounted()) {
        if (session_active) reset_session(false);
        return;
    }
    // Suspension pauses USB forwarding without starting a new session.
    if (!tud_ready()) return;
    if (!session_active) reset_session(true);
    receive_usb_bytes();
    transmit_uart_bytes();
    transmit_usb_bytes();
}

/* The original slider UART is fixed at 115200/8N1. CDC settings are cosmetic. */
void tud_cdc_line_coding_cb(uint8_t instance,
                            cdc_line_coding_t const *line_coding) {
    (void)instance;
    (void)line_coding;
}
