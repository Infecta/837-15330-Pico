#include "led_driver.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"

#include "sk6812.pio.h"

#define LED_DATA_PIN 14
#define LED_PIO pio0
#define LED_PIO_CLOCK_HZ 8000000.0f

// Safe temporary limit while LEDs are powered from USB VBUS.
#define LED_POWER_BUDGET_MA 100u
#define LED_IDLE_CURRENT_MA 1u
#define LED_CHANNEL_CURRENT_MA 12u

// Allows the last LED bits to leave the PIO and latch.
#define LED_LATCH_TIME_US 400u

typedef enum {
    LED_WAITING_FOR_RESET,
    LED_SENDING,
    LED_LATCHING,
} led_state_t;

static uint32_t dma_pixels[LED_COUNT];
static uint32_t pending_pixels[LED_COUNT];

static bool pending_frame = false;
static led_state_t state;
static absolute_time_t deadline;

static uint led_sm;
static uint led_dma_channel;

/*
 * Limit all pixels together, keeping their colours intact.
 * This is an estimate, not a physical current measurement.
 */
static void limit_power(uint32_t pixels[LED_COUNT]) {
    uint32_t channel_sum = 0;

    for (uint32_t i = 0; i < LED_COUNT; i++) {
        channel_sum += (pixels[i] >> 24) & 0xFF; // Green
        channel_sum += (pixels[i] >> 16) & 0xFF; // Red
        channel_sum += (pixels[i] >> 8) & 0xFF;  // Blue
    }

    uint32_t available_ma =
        LED_POWER_BUDGET_MA - (LED_COUNT * LED_IDLE_CURRENT_MA);

    uint32_t maximum_sum =
        (available_ma * 255u) / LED_CHANNEL_CURRENT_MA;

    if (channel_sum <= maximum_sum || channel_sum == 0) {
        return;
    }

    for (uint32_t i = 0; i < LED_COUNT; i++) {
        uint32_t green = ((pixels[i] >> 24) & 0xFF) * maximum_sum / channel_sum;
        uint32_t red = ((pixels[i] >> 16) & 0xFF) * maximum_sum / channel_sum;
        uint32_t blue = ((pixels[i] >> 8) & 0xFF) * maximum_sum / channel_sum;

        pixels[i] = (green << 24) | (red << 16) | (blue << 8);
    }
}

void led_driver_set_solid(uint8_t red,
        uint8_t green,
        uint8_t blue) {
    uint32_t frame[LED_COUNT];

    uint32_t colour =
    ((uint32_t)green << 24) |
    ((uint32_t)red << 16) |
    ((uint32_t)blue << 8);

    for (uint32_t i = 0; i < LED_COUNT; i++) {
    frame[i] = colour;
}

led_driver_submit(frame);
}

void led_driver_init(void) {
    led_sm = pio_claim_unused_sm(LED_PIO, true);
    led_dma_channel = dma_claim_unused_channel(true);

    uint program_offset =
        pio_add_program(LED_PIO, &sk6812_program);

    pio_gpio_init(LED_PIO, LED_DATA_PIN);
    pio_sm_set_consecutive_pindirs(
        LED_PIO, led_sm, LED_DATA_PIN, 1, true
    );

    pio_sm_config config =
        sk6812_program_get_default_config(program_offset);

    sm_config_set_sideset_pins(&config, LED_DATA_PIN);
    sm_config_set_out_shift(&config, false, true, 24);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);

    sm_config_set_clkdiv(
        &config,
        (float)clock_get_hz(clk_sys) / LED_PIO_CLOCK_HZ
    );

    pio_sm_init(LED_PIO, led_sm, program_offset, &config);
    pio_sm_set_enabled(LED_PIO, led_sm, true);

    dma_channel_config dma_config =
        dma_channel_get_default_config(led_dma_channel);

    channel_config_set_transfer_data_size(
        &dma_config, DMA_SIZE_32
    );
    channel_config_set_read_increment(&dma_config, true);
    channel_config_set_write_increment(&dma_config, false);

    channel_config_set_dreq(
        &dma_config,
        pio_get_dreq(LED_PIO, led_sm, true)
    );

    dma_channel_configure(
        led_dma_channel,
        &dma_config,
        &LED_PIO->txf[led_sm],
        dma_pixels,
        LED_COUNT,
        false
    );

    // Ensure the data line has been low long enough before the first frame.
    deadline = make_timeout_time_us(LED_LATCH_TIME_US);
    state = LED_WAITING_FOR_RESET;
}

void led_driver_submit(const uint32_t logical_frame[LED_COUNT]) {
    /*
     * logical_frame[0] is the leftmost LED.
     * PIO transmits pixel 0 to D1, which is physically rightmost.
     */
    for (uint32_t wire_pixel = 0; wire_pixel < LED_COUNT; wire_pixel++) {
        pending_pixels[wire_pixel] =
            logical_frame[LED_COUNT - 1 - wire_pixel];
    }

    pending_frame = true;
}

void led_driver_task(void) {
    if (state == LED_WAITING_FOR_RESET &&
        time_reached(deadline)) {
        state = LED_LATCHING;
    }

    if (state == LED_SENDING &&
        !dma_channel_is_busy(led_dma_channel)) {
        deadline = make_timeout_time_us(LED_LATCH_TIME_US);
        state = LED_WAITING_FOR_RESET;
    }

    if (state != LED_LATCHING || !pending_frame) {
        return;
    }

    memcpy(dma_pixels, pending_pixels, sizeof(dma_pixels));
    pending_frame = false;

    limit_power(dma_pixels);

    dma_channel_set_read_addr(
        led_dma_channel,
        dma_pixels,
        false
    );

    dma_channel_set_trans_count(
        led_dma_channel,
        LED_COUNT,
        true
    );

    state = LED_SENDING;
    
}