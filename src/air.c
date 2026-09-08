#include "air.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#define IR_PHASES 3

// ADC GPIO Assignments
// If you're changing this, change IR_SIGNAL_ADC too.
#define ADC_LEFT 29
#define ADC_RIGHT 28

// IR Phase Pin Assignments for each side
// Goes like { A, B, C }
static const uint8_t IR_LEFT_CHANNEL[IR_PHASES] = { 6, 7, 8 };

static const uint8_t IR_RIGHT_CHANNEL[IR_PHASES] = { 27, 26, 15 };

// ADC Pin Assignments
// GP29 = ADC3 = left signal
// GP28 = ADC2 = right signal
static const uint8_t IR_SIGNAL_ADC[2] = { 3, 2 };

static volatile uint16_t ir_raw[IO4_AIR_CHANNEL_COUNT];
static bool ir_blocked[IO4_AIR_CHANNEL_COUNT];

static uint16_t ir_baselines[IO4_AIR_CHANNEL_COUNT] = {
    3800, 3800,
    3800, 3800,
    3800, 3800,
};

static const uint8_t IR_TRIGGER_PERCENT = 17;

#define IR_DEBOUNCE_PERCENT 90

static uint8_t current_phase;
static volatile uint8_t published_bitmap;

static void emitters_off(void)
{
    for (uint8_t phase = 0; phase < IR_PHASES; ++phase) {
        gpio_put(IR_LEFT_CHANNEL[phase], false);
        gpio_put(IR_RIGHT_CHANNEL[phase], false);
    }
}

// Read IR Phases
// Since we are using 2 seperate pins instead of a unified one,
// We need to iterate through 2 pins at a time
static void ir_read(void)
{
    const uint8_t phase = current_phase;

    // IR Channel ON
    gpio_put(IR_LEFT_CHANNEL[phase], true);
    gpio_put(IR_RIGHT_CHANNEL[phase], true);

    sleep_us(10);

    // Read raw ADC value.
    // Every even number is the left side
    // Every odd number is the right side
    // {0, 1, 2, 3, 4, 5}
    for (uint8_t side = 0; side < 2; ++side) {
        adc_select_input(IR_SIGNAL_ADC[side]);
        sleep_us(2);

        ir_raw[(phase * 2) + side] = adc_read();
    }

    // IR Channel OFF
    gpio_put(IR_LEFT_CHANNEL[phase], false);
    gpio_put(IR_RIGHT_CHANNEL[phase], false);

    current_phase = (uint8_t)((phase + 1) % IR_PHASES);
}

// Only the agent and god knows what's happening here
// Maybe even only god now.
// Taken from my other firmware.. which is adapted from whowe's firmware.
static void ir_judge(void)
{
    uint8_t bitmap = 0;

    for (uint8_t channel = 0;
         channel < IO4_AIR_CHANNEL_COUNT;
         ++channel) {
        const int offset =
            (int)ir_baselines
        [channel] - (int)ir_raw[channel];

        int threshold =
            ((int)ir_baselines
        [channel] * IR_TRIGGER_PERCENT) / 100;

        if (ir_blocked[channel]) {
            threshold =
                (threshold * IR_DEBOUNCE_PERCENT) / 100;
        }

        ir_blocked[channel] = offset >= threshold;

        if (ir_blocked[channel]) {
            bitmap |= (uint8_t)(1 << channel);
        }
    }

    published_bitmap = bitmap;
}

// IR Tower Initialization
void air_sensor_init(void)
{
    for (uint8_t phase = 0; phase < IR_PHASES; ++phase) {
        gpio_init(IR_LEFT_CHANNEL[phase]);
        gpio_set_dir(IR_LEFT_CHANNEL[phase], GPIO_OUT);
        gpio_put(IR_LEFT_CHANNEL[phase], false);
        gpio_set_drive_strength(
            IR_LEFT_CHANNEL[phase],
            GPIO_DRIVE_STRENGTH_12MA
        );

        gpio_init(IR_RIGHT_CHANNEL[phase]);
        gpio_set_dir(IR_RIGHT_CHANNEL[phase], GPIO_OUT);
        gpio_put(IR_RIGHT_CHANNEL[phase], false);
        gpio_set_drive_strength(
            IR_RIGHT_CHANNEL[phase],
            GPIO_DRIVE_STRENGTH_12MA
        );
    }

    adc_init();

    adc_gpio_init(ADC_LEFT);
    adc_gpio_init(ADC_RIGHT);

    emitters_off();

    memset((void *)ir_raw, 0, sizeof(ir_raw));
    memset(ir_blocked, 0, sizeof(ir_blocked));

    current_phase = 0;
    published_bitmap = 0;
}

bool air_sensor_capture_baselines(
    uint16_t baselines[IO4_AIR_CHANNEL_COUNT])
{
    if (baselines == NULL) {
        return false;
    }

    for (uint8_t phase = 0; phase < IR_PHASES; ++phase) {
        ir_read();
    }

    for (uint8_t channel = 0;
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

    for (uint8_t channel = 0;
         channel < IO4_AIR_CHANNEL_COUNT;
         ++channel) {
        ir_baselines
    [channel] = baselines[channel];
        ir_blocked[channel] = false;
    }

    published_bitmap = 0;
}

uint8_t air_sensor_blocked_bitmap(void)
{
    return published_bitmap;
}

uint16_t air_sensor_raw_value(uint8_t channel)
{
    if (channel >= IO4_AIR_CHANNEL_COUNT) {
        return 0;
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