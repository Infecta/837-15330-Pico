#include "air.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "pico/critical_section.h"
#include "pico/multicore.h"

#define IR_PHASES 3

// ADC GPIO Assignments
// If you're changing this, change IR_SIGNAL_ADC too.
#define ADC_LEFT 29
#define ADC_RIGHT 28

// IR Phase Pin Assignments for each side
// Goes like { A, B, C }
static const uint8_t IR_LEFT_CHANNEL[IR_PHASES] = { 8, 7, 6 };

static const uint8_t IR_RIGHT_CHANNEL[IR_PHASES] = { 15, 26, 27 };

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

static uint8_t trigger_percent[IO4_AIR_CHANNEL_COUNT] = {
    12, 12, 12, 12, 12, 12
};

#define IR_DEBOUNCE_PERCENT 90

static uint8_t current_phase;
static volatile uint8_t published_bitmap;
static critical_section_t air_lock;
static air_status_t published_status;

static bool calibration_pending;
static bool calibration_active;
static uint8_t calibration_result;
static uint32_t calibration_sums[IO4_AIR_CHANNEL_COUNT];
static uint32_t calibration_samples;
static uint64_t calibration_end_us;
static uint64_t next_status_us;

static uint16_t channel_drop(unsigned channel) {
    return (uint16_t)(ir_baselines[channel] * trigger_percent[channel] / 100u);
}

static void publish_status(void) {
    air_status_t status = {0};
    for (unsigned i = 0; i < IO4_AIR_CHANNEL_COUNT; ++i) {
        status.raw[i] = ir_raw[i];
        status.baselines[i] = ir_baselines[i];
        status.trigger_percent[i] = trigger_percent[i];
    }
    status.blocked = published_bitmap;
    status.calibration_result = calibration_result;
    status.calibrating = calibration_active;
    critical_section_enter_blocking(&air_lock);
    if (calibration_pending) {
        status.calibrating = true;
        status.calibration_result = 1;
    }
    published_status = status;
    critical_section_exit(&air_lock);
}

void air_sensor_get_status(air_status_t *status) {
    if (status == NULL) return;
    critical_section_enter_blocking(&air_lock);
    *status = published_status;
    critical_section_exit(&air_lock);
}

bool air_sensor_set_baseline(uint8_t channel, uint16_t baseline) {
    if (channel >= IO4_AIR_CHANNEL_COUNT || baseline < 32 || baseline > 4095) {
        return false;
    }
    critical_section_enter_blocking(&air_lock);
    if (calibration_active || calibration_pending) {
        critical_section_exit(&air_lock);
        return false;
    }
    ir_baselines[channel] = baseline;
    ir_blocked[channel] = false;
    published_bitmap &= (uint8_t)~(1u << channel);
    critical_section_exit(&air_lock);
    publish_status();
    return true;
}

bool air_sensor_set_trigger(uint8_t channel, uint8_t percent) {
    if (channel >= IO4_AIR_CHANNEL_COUNT || percent < 1 || percent > 99) {
        return false;
    }
    critical_section_enter_blocking(&air_lock);
    trigger_percent[channel] = percent;
    critical_section_exit(&air_lock);
    publish_status();
    return true;
}

bool air_sensor_start_calibration(void) {
    critical_section_enter_blocking(&air_lock);
    bool accepted = !calibration_active && !calibration_pending;
    if (accepted) {
        calibration_pending = true;
        calibration_active = true;
        published_status.calibrating = true;
        published_status.calibration_result = 1;
    }
    critical_section_exit(&air_lock);
    return accepted;
}

static void calibration_and_status_task(void) {
    uint64_t now = time_us_64();
    bool start = false;
    bool finished = false;
    critical_section_enter_blocking(&air_lock);
    if (calibration_pending) {
        calibration_pending = false;
        start = true;
    }
    critical_section_exit(&air_lock);

    if (start) {
        calibration_result = 1;
        memset(calibration_sums, 0, sizeof(calibration_sums));
        calibration_samples = 0;
        calibration_end_us = now + 2000000u;
    }

    if (calibration_result == 1) {
        for (unsigned i = 0; i < IO4_AIR_CHANNEL_COUNT; ++i) {
            calibration_sums[i] += ir_raw[i];
        }
        calibration_samples++;
        if (now >= calibration_end_us) {
            uint16_t captured[IO4_AIR_CHANNEL_COUNT];
            bool valid = true;
            for (unsigned i = 0; i < IO4_AIR_CHANNEL_COUNT; ++i) {
                captured[i] = (uint16_t)(calibration_sums[i] / calibration_samples);
                if (captured[i] < 32 || captured[i] > 4095) valid = false;
            }
            if (valid) {
                air_sensor_set_baselines(captured);
                calibration_result = 2;
            } else {
                calibration_result = 3;
            }
            finished = true;
            critical_section_enter_blocking(&air_lock);
            if (!calibration_pending) calibration_active = false;
            critical_section_exit(&air_lock);
        }
    }

    if (now >= next_status_us || start || finished) {
        publish_status();
        next_status_us = now + 10000u;
    }
}

static void emitters_off(void)
{
    for (uint8_t phase = 0; phase < IR_PHASES; ++phase) {
        gpio_put(IR_LEFT_CHANNEL[phase], false);
        gpio_put(IR_RIGHT_CHANNEL[phase], false);
    }
}

static void ir_read(void)
{
    const uint8_t phase = current_phase;

    gpio_put(IR_LEFT_CHANNEL[phase], true);
    gpio_put(IR_RIGHT_CHANNEL[phase], true);

    sleep_us(10);

    for (uint8_t side = 0; side < 2; ++side) {
        adc_select_input(IR_SIGNAL_ADC[side]);
        sleep_us(2);
        ir_raw[(phase * 2) + side] = adc_read();
    }

    gpio_put(IR_LEFT_CHANNEL[phase], false);
    gpio_put(IR_RIGHT_CHANNEL[phase], false);

    current_phase = (uint8_t)((phase + 1) % IR_PHASES);
}

static void ir_judge(void)
{
    uint8_t bitmap = 0;

    for (uint8_t channel = 0; channel < IO4_AIR_CHANNEL_COUNT; ++channel) {
        const int offset = (int)ir_baselines[channel] - (int)ir_raw[channel];
        int threshold = (int)channel_drop(channel);

        if (ir_blocked[channel]) {
            threshold = (threshold * IR_DEBOUNCE_PERCENT) / 100;
        }

        ir_blocked[channel] = offset >= threshold;
        if (ir_blocked[channel]) {
            bitmap |= (uint8_t)(1 << channel);
        }
    }

    published_bitmap = bitmap;
}

void air_sensor_init(void)
{
    critical_section_init(&air_lock);
    calibration_result = 0;
    calibration_pending = false;
    calibration_active = false;
    next_status_us = 0;

    for (uint8_t phase = 0; phase < IR_PHASES; ++phase) {
        gpio_init(IR_LEFT_CHANNEL[phase]);
        gpio_set_dir(IR_LEFT_CHANNEL[phase], GPIO_OUT);
        gpio_put(IR_LEFT_CHANNEL[phase], false);
        gpio_set_drive_strength(IR_LEFT_CHANNEL[phase], GPIO_DRIVE_STRENGTH_12MA);

        gpio_init(IR_RIGHT_CHANNEL[phase]);
        gpio_set_dir(IR_RIGHT_CHANNEL[phase], GPIO_OUT);
        gpio_put(IR_RIGHT_CHANNEL[phase], false);
        gpio_set_drive_strength(IR_RIGHT_CHANNEL[phase], GPIO_DRIVE_STRENGTH_12MA);
    }

    adc_init();
    adc_gpio_init(ADC_LEFT);
    adc_gpio_init(ADC_RIGHT);
    emitters_off();

    memset((void *)ir_raw, 0, sizeof(ir_raw));
    memset(ir_blocked, 0, sizeof(ir_blocked));
    current_phase = 0;
    published_bitmap = 0;
    publish_status();
}

bool air_sensor_capture_baselines(
    uint16_t baselines[IO4_AIR_CHANNEL_COUNT])
{
    if (baselines == NULL) return false;

    for (uint8_t phase = 0; phase < IR_PHASES; ++phase) {
        ir_read();
    }

    for (uint8_t channel = 0; channel < IO4_AIR_CHANNEL_COUNT; ++channel) {
        baselines[channel] = ir_raw[channel];
    }

    emitters_off();
    return true;
}

void air_sensor_set_baselines(
    const uint16_t baselines[IO4_AIR_CHANNEL_COUNT])
{
    if (baselines == NULL) return;

    critical_section_enter_blocking(&air_lock);
    for (uint8_t channel = 0; channel < IO4_AIR_CHANNEL_COUNT; ++channel) {
        ir_baselines[channel] = baselines[channel];
        ir_blocked[channel] = false;
    }
    published_bitmap = 0;
    critical_section_exit(&air_lock);
    publish_status();
}

uint8_t air_sensor_blocked_bitmap(void)
{
    return published_bitmap;
}

uint16_t air_sensor_raw_value(uint8_t channel)
{
    if (channel >= IO4_AIR_CHANNEL_COUNT) return 0;
    return ir_raw[channel];
}

void air_sensor_update(void)
{
    ir_read();
    ir_judge();
}

void air_sensor_core1_entry(void)
{
    multicore_lockout_victim_init();
    while (true) {
        air_sensor_update();
        if (current_phase == 0) calibration_and_status_task();
    }
}
