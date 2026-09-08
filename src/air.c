#include "air.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#define AIR_PHASE_COUNT 3u

/*
 * Phase order:
 *   A = index 0
 *   B = index 1
 *   C = index 2
 */
static const uint8_t IR_LEFT_ABC[AIR_PHASE_COUNT] = {
    6u,  /* Left A */
    7u,  /* Left B */
    8u,  /* Left C */
};

static const uint8_t IR_RIGHT_ABC[AIR_PHASE_COUNT] = {
    27u, /* Right A */
    26u, /* Right B */
    15u, /* Right C */
};

/*
 * GP29 = ADC3 = left signal
 * GP28 = ADC2 = right signal
 */
static const uint8_t IR_SIGNAL_ADC[2] = {
    3u,
    2u,
};

static volatile uint16_t ir_raw[IO4_AIR_CHANNEL_COUNT];
static bool ir_blocked[IO4_AIR_CHANNEL_COUNT];

static uint16_t ir_base[IO4_AIR_CHANNEL_COUNT] = {
    3800u, 3800u,
    3800u, 3800u,
    3800u, 3800u,
};

static const uint8_t IR_TRIGGER_PERCENT = 17u;

#define IR_DEBOUNCE_PERCENT 90u

static uint8_t current_phase;
static volatile uint8_t published_bitmap;

static void emitters_off(void)
{
    for (uint8_t phase = 0u; phase < AIR_PHASE_COUNT; ++phase) {
        gpio_put(IR_LEFT_ABC[phase], false);
        gpio_put(IR_RIGHT_ABC[phase], false);
    }
}

/*
 * Read one A/B/C phase.
 */
static void ir_read(void)
{
    const uint8_t phase = current_phase;

    /*
     * Activate the same phase on both air towers.
     *
     * This is the only intentional scanning difference from the
     * unified ABC wiring in simpl-slidrr-firmware.
     */
    gpio_put(IR_LEFT_ABC[phase], true);
    gpio_put(IR_RIGHT_ABC[phase], true);

    sleep_us(10);

    /*
     * Array order remains:
     *
     *   0 = Left A
     *   1 = Right A
     *   2 = Left B
     *   3 = Right B
     *   4 = Left C
     *   5 = Right C
     */
    for (uint8_t side = 0u; side < 2u; ++side) {
        adc_select_input(IR_SIGNAL_ADC[side]);
        sleep_us(2);

        ir_raw[(phase * 2u) + side] = adc_read();
    }

    gpio_put(IR_LEFT_ABC[phase], false);
    gpio_put(IR_RIGHT_ABC[phase], false);

    current_phase = (uint8_t)((phase + 1u) % AIR_PHASE_COUNT);
}

/*
 * Apply the same threshold and hysteresis calculation used by
 * simpl-slidrr-firmware.
 */
static void ir_judge(void)
{
    uint8_t bitmap = 0u;

    for (uint8_t channel = 0u;
         channel < IO4_AIR_CHANNEL_COUNT;
         ++channel) {
        const int offset =
            (int)ir_base[channel] - (int)ir_raw[channel];

        int threshold =
            ((int)ir_base[channel] * IR_TRIGGER_PERCENT) / 100;

        if (ir_blocked[channel]) {
            threshold =
                (threshold * IR_DEBOUNCE_PERCENT) / 100;
        }

        ir_blocked[channel] = offset >= threshold;

        if (ir_blocked[channel]) {
            bitmap |= (uint8_t)(1u << channel);
        }
    }

    published_bitmap = bitmap;
}

void air_sensor_init(void)
{
    for (uint8_t phase = 0u; phase < AIR_PHASE_COUNT; ++phase) {
        gpio_init(IR_LEFT_ABC[phase]);
        gpio_set_dir(IR_LEFT_ABC[phase], GPIO_OUT);
        gpio_put(IR_LEFT_ABC[phase], false);
        gpio_set_drive_strength(
            IR_LEFT_ABC[phase],
            GPIO_DRIVE_STRENGTH_12MA
        );

        gpio_init(IR_RIGHT_ABC[phase]);
        gpio_set_dir(IR_RIGHT_ABC[phase], GPIO_OUT);
        gpio_put(IR_RIGHT_ABC[phase], false);
        gpio_set_drive_strength(
            IR_RIGHT_ABC[phase],
            GPIO_DRIVE_STRENGTH_12MA
        );
    }

    adc_init();

    adc_gpio_init(29u);
    adc_gpio_init(28u);

    emitters_off();

    memset((void *)ir_raw, 0, sizeof(ir_raw));
    memset(ir_blocked, 0, sizeof(ir_blocked));

    current_phase = 0u;
    published_bitmap = 0u;
}

bool air_sensor_capture_baselines(
    uint16_t baselines[IO4_AIR_CHANNEL_COUNT])
{
    if (baselines == NULL) {
        return false;
    }

    /*
     * main.c performs calibration before core 1 starts, so obtain one
     * complete A/B/C refresh here before capturing the values.
     */
    for (uint8_t phase = 0u; phase < AIR_PHASE_COUNT; ++phase) {
        ir_read();
    }

    for (uint8_t channel = 0u;
         channel < IO4_AIR_CHANNEL_COUNT;
         ++channel) {
        baselines[channel] = ir_raw[channel];
    }

    emitters_off();
    return true;
}

void air_sensor_set_baselines(
    const uint16_t baselines[IO4_AIR_CHANNEL_COUNT])
{
    if (baselines == NULL) {
        return;
    }

    for (uint8_t channel = 0u;
         channel < IO4_AIR_CHANNEL_COUNT;
         ++channel) {
        ir_base[channel] = baselines[channel];
        ir_blocked[channel] = false;
    }

    published_bitmap = 0u;
}

uint8_t air_sensor_blocked_bitmap(void)
{
    return published_bitmap;
}

uint16_t air_sensor_raw_value(uint8_t channel)
{
    if (channel >= IO4_AIR_CHANNEL_COUNT) {
        return 0u;
    }

    return ir_raw[channel];
}

void air_sensor_update(void)
{
    ir_read();
    ir_judge();
}

void air_sensor_core1_entry(void)
{
    while (true) {
        air_sensor_update();
    }
}