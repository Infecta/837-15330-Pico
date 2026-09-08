#include "uart_bridge.h"

#include <stdbool.h>
#include <stdint.h>

#include "hardware/irq.h"
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
#define RING_CAPACITY 1024u
#define RING_MASK (RING_CAPACITY - 1u)

#define TRANSFER_BUDGET 64u

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

static uint32_t active_baud_rate = DEFAULT_BAUD_RATE;
static uint8_t active_data_bits = 8;
static uint8_t active_stop_bits = 1;
static uart_parity_t active_parity = UART_PARITY_NONE;

static bool ring_push(byte_ring_t *ring, uint8_t value) {
    uint32_t next = (ring->head + 1) & RING_MASK;

    if (next == ring->tail) {
        return false; // Ring is full.
    }

    ring->data[ring->head] = value;
    ring->head = next;

    return true;
}

static bool ring_pop(byte_ring_t *ring, uint8_t *value) {
    if (ring->tail == ring->head) {
        return false; // Ring is empty.
    }

    *value = ring->data[ring->tail];
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
        uint8_t byte = uart_getc(CY8C_UART);

        // Later you can add a dropped-byte counter here if this returns false.
        ring_push(&uart_to_usb, byte);
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
        uart_to_usb.tail = (uart_to_usb.tail + written) & RING_MASK;
    }

    tud_cdc_write_flush();
}

void uart_bridge_task(void) {
    receive_usb_bytes();
    transmit_uart_bytes();
    transmit_usb_bytes();
}

/*
 * slidershim's chosen COM-port settings become the CY8C UART settings.
 */
void tud_cdc_line_coding_cb(uint8_t instance,
                            cdc_line_coding_t const *line_coding) {
    if (instance != 0 || line_coding == NULL) {
        return;
    }

    if (line_coding->bit_rate < 9600 ||
        line_coding->bit_rate > 1000000) {
        return;
    }

    uint data_bits = line_coding->data_bits;
    if (data_bits < 5 || data_bits > 8) {
        data_bits = 8;
    }

    uint stop_bits = line_coding->stop_bits == 2 ? 2 : 1;

    uart_parity_t parity = UART_PARITY_NONE;

    if (line_coding->parity == 1) {
        parity = UART_PARITY_ODD;
    } else if (line_coding->parity == 2) {
        parity = UART_PARITY_EVEN;
    }

    if (line_coding->bit_rate != active_baud_rate) {
        uart_set_baudrate(CY8C_UART, line_coding->bit_rate);
        active_baud_rate = line_coding->bit_rate;
    }

    if (data_bits != active_data_bits ||
        stop_bits != active_stop_bits ||
        parity != active_parity) {
        uart_set_format(CY8C_UART, data_bits, stop_bits, parity);

        active_data_bits = data_bits;
        active_stop_bits = stop_bits;
        active_parity = parity;
    }
}