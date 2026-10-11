#include "air.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "pico/critical_section.h"

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

static uint8_t ir_trigger_percent = 12;
static uint8_t channel_percent[6] = {12,12,12,12,12,12};
static uint16_t absolute_trigger[6] = {65535,65535,65535,65535,65535,65535};
static uint8_t pending_mode[6];
static uint16_t pending_value[6];
static uint16_t channel_drop(unsigned i) {
    if (absolute_trigger[i] != 65535)
        return ir_baselines[i] > absolute_trigger[i] ? ir_baselines[i] - absolute_trigger[i] : 1;
    return ir_baselines[i] * channel_percent[i] / 100;
}

#define IR_DEBOUNCE_PERCENT 90

static uint8_t current_phase;
static volatile uint8_t published_bitmap;
// Core 1 owns ADC/baselines. This lock only protects bounded mailbox copies.
static critical_section_t snapshot_lock;
static air_snapshot_t latest_snapshot;
static bool calibration_pending, calibration_active;
static uint8_t calibration_status;
static uint32_t calibration_sums[IO4_AIR_CHANNEL_COUNT];
static uint32_t calibration_samples;
static uint64_t calibration_deadline, next_snapshot;

static void publish_snapshot(void) {
    air_snapshot_t snapshot = {0};
    for (unsigned i = 0; i < IO4_AIR_CHANNEL_COUNT; ++i) {
        uint16_t drop = channel_drop(i);
        if (absolute_trigger[i] != 65535) snapshot.absolute_mask |= 1u << i;
        snapshot.raw[i] = ir_raw[i];
        snapshot.baselines[i] = ir_baselines[i];
        snapshot.trigger[i] = ir_baselines[i] - drop;
        // A blocked beam clears strictly ABOVE this ADC boundary.
        snapshot.release[i] = ir_baselines[i] - drop * IR_DEBOUNCE_PERCENT / 100;
    }
    snapshot.trigger_percent = ir_trigger_percent;
    snapshot.hysteresis = IR_DEBOUNCE_PERCENT;
    snapshot.blocked = published_bitmap;
    snapshot.calibration_status = calibration_status;
    critical_section_enter_blocking(&snapshot_lock);
    // Preserve a request arriving while this snapshot was being prepared.
    if (calibration_pending) snapshot.calibration_status = AIR_CAL_RUNNING;
    snapshot.sample_id = latest_snapshot.sample_id + 1;
    latest_snapshot = snapshot;
    critical_section_exit(&snapshot_lock);
}

void air_sensor_get_snapshot(air_snapshot_t *snapshot) {
    critical_section_enter_blocking(&snapshot_lock);
    *snapshot = latest_snapshot;
    critical_section_exit(&snapshot_lock);
}

bool air_sensor_set_trigger_percent(uint8_t percent) {
    if (percent < 1 || percent > 99) return false;
    critical_section_enter_blocking(&snapshot_lock);
    for (unsigned i=0;i<6;i++) { pending_mode[i]=1; pending_value[i]=percent; }
    critical_section_exit(&snapshot_lock);
    return true;
}

bool air_sensor_set_channel_trigger(uint8_t channel, uint16_t value, bool absolute) {
    if (channel >= 6 || (absolute ? value > 4094 : (value < 1 || value > 99))) return false;
    critical_section_enter_blocking(&snapshot_lock);
    bool valid = !absolute || value < latest_snapshot.baselines[channel];
    if (valid) { pending_mode[channel] = absolute ? 2 : 1; pending_value[channel] = value; }
    critical_section_exit(&snapshot_lock);
    return valid;
}

bool air_sensor_request_calibration(void) {
    critical_section_enter_blocking(&snapshot_lock);
    bool accepted = !calibration_active;
    if (accepted) {
        calibration_pending = true;
        calibration_active = true;
        latest_snapshot.calibration_status = AIR_CAL_RUNNING;
    }
    critical_section_exit(&snapshot_lock);
    return accepted;
}

static void calibration_and_snapshot_task(void) {
    // Only called at a complete A/B/C boundary, at most once per millisecond.
    uint64_t now = time_us_64();
    if (now < next_snapshot) return;
    next_snapshot = now + 1000;
    critical_section_enter_blocking(&snapshot_lock);
    for (unsigned i=0;i<6;i++) {
        if (pending_mode[i]==1) { channel_percent[i]=(uint8_t)pending_value[i]; absolute_trigger[i]=65535; }
        if (pending_mode[i]==2) absolute_trigger[i]=pending_value[i];
        pending_mode[i]=0;
    }
    ir_trigger_percent=channel_percent[0];
    for (unsigned i=0;i<6;i++) if (absolute_trigger[i]!=65535 || channel_percent[i]!=ir_trigger_percent) { ir_trigger_percent=0; break; }
    bool requested = calibration_pending;
    calibration_pending = false;
    critical_section_exit(&snapshot_lock);
    if (requested) {
        calibration_status = AIR_CAL_RUNNING;
        memset(calibration_sums, 0, sizeof(calibration_sums));
        calibration_samples = 0;
        calibration_deadline = now + 2000000;
    }
    if (calibration_status == AIR_CAL_RUNNING) {
        for (unsigned i = 0; i < IO4_AIR_CHANNEL_COUNT; ++i) calibration_sums[i] += ir_raw[i];
        calibration_samples++;
        if (now >= calibration_deadline) {
            uint16_t captured[IO4_AIR_CHANNEL_COUNT];
            bool valid = true;
            for (unsigned i = 0; i < IO4_AIR_CHANNEL_COUNT; ++i) {
                captured[i] = calibration_sums[i] / calibration_samples;
                // Reject effectively unpowered/missing towers; retain the prior calibration.
                if (captured[i] < 32 || captured[i] > 4095) valid = false;
            }
            if (valid) {
                air_sensor_set_baselines(captured);
                calibration_status = AIR_CAL_DONE;
            } else calibration_status = AIR_CAL_FAILED;
        }
    }
    publish_snapshot();
    if (calibration_status == AIR_CAL_DONE || calibration_status == AIR_CAL_FAILED) {
        critical_section_enter_blocking(&snapshot_lock);
        // A new request may have arrived after publishing completion.
        if (!calibration_pending) calibration_active = false;
        critical_section_exit(&snapshot_lock);
    }
}

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

        int threshold = channel_drop(channel);

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
    critical_section_init(&snapshot_lock);
    ir_trigger_percent = 12;
    for (unsigned i=0;i<6;i++) { channel_percent[i]=12; absolute_trigger[i]=65535; pending_mode[i]=0; }
    calibration_status = AIR_CAL_IDLE;
    calibration_pending = false;
    calibration_active = false;
    next_snapshot = 0;
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
    publish_snapshot();
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
    publish_snapshot();
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
        if (current_phase == 0) calibration_and_snapshot_task();
    }
}