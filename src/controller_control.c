#include "controller_control.h"
#include <string.h>
#include "air.h"
#include "calibration.h"
#include "led_driver.h"
#include "usb_descriptors.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
#include "pico/stdlib.h"

#define SAVE_MAGIC 0x534c4944u
#define VERSION 1
// Commands: 1 subscribe/heartbeat, 2 brightness, 3 calibrate, 4 save/restart, 5 trigger percentage.
// Result: 0 OK, 1 malformed/unknown, 2 busy, 3 not calibrated, 4 saving failed.
static uint16_t ack_sequence;
static uint8_t ack_command, ack_result;
static bool saved, dirty, was_calibrating, restarting, save_failed;
static uint64_t next_calibration_check;
static uint64_t lease_until, next_report, reboot_at;
static uint16_t reboot_baselines[6];

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static uint16_t get16(const uint8_t *p) { return p[0] | ((uint16_t)p[1] << 8); }

bool controller_control_restore_after_reboot(uint16_t baselines[6]) {
    bool requested = watchdog_caused_reboot() && watchdog_hw->scratch[0] == SAVE_MAGIC;
    watchdog_hw->scratch[0] = 0; // Never retry a stale save on an unrelated reset.
    if (!requested) return false;
    for (unsigned i = 0; i < 3; ++i) {
        uint32_t packed = watchdog_hw->scratch[i + 1];
        baselines[2*i] = packed & 0xffff;
        baselines[2*i+1] = packed >> 16;
    }
    for (unsigned i = 0; i < 6; ++i) {
        if (baselines[i] < 32 || baselines[i] > 4095) { save_failed = true; return false; }
    }
    // This runs on core 0 BEFORE the UART, USB and core 1 are started.
    if (!calibration_save(baselines)) { save_failed = true; return false; }
    return true;
}

void controller_control_init(bool saved_calibration) { saved = saved_calibration; }

uint16_t controller_control_report(uint8_t *data, uint16_t size) {
    if (!data || size < CONTROL_REPORT_SIZE) return 0;
    air_snapshot_t a;
    air_sensor_get_snapshot(&a);
    memset(data, 0, CONTROL_REPORT_SIZE);
    data[0] = VERSION; data[1] = a.calibration_status;
    data[2] = a.blocked; data[3] = led_driver_brightness();
    data[4] = (saved ? 1 : 0) | (dirty ? 2 : 0) | (restarting ? 4 : 0) | (save_failed ? 8 : 0);
    data[5] = a.trigger_percent; data[6] = a.hysteresis; data[7] = ack_command;
    put16(data+8, ack_sequence); data[10] = ack_result;
    data[11] = 0x80 | a.absolute_mask; // Per-channel tuning capability + absolute modes.
    put16(data+12, a.sample_id);
    for (unsigned i = 0; i < 6; ++i) {
        put16(data+14+2*i, a.raw[i]); put16(data+26+2*i, a.baselines[i]);
        put16(data+38+2*i, a.trigger[i]); put16(data+50+2*i, a.release[i]);
    }
    data[62] = (uint8_t)led_driver_power_budget_ma();
    return CONTROL_REPORT_SIZE;
}

void controller_control_receive(uint8_t id, const uint8_t *data, uint16_t size) {
    if (!data) return;
    // TinyUSB strips the ID for control SET_REPORT, but not interrupt OUT.
    if (id == 0) {
        if (size != CONTROL_REPORT_SIZE + 1 || data[0] != CONTROL_REPORT_ID) return;
        id = *data++; size--;
    }
    if (id != CONTROL_REPORT_ID || size != CONTROL_REPORT_SIZE) return;
    ack_command = data[1]; ack_sequence = get16(data+2); ack_result = 0;
    lease_until = time_us_64() + 3000000;
    next_report = 0;
    if (data[0] != VERSION) { ack_result = 1; return; }
    if (restarting) { ack_result = 2; return; }
    switch (data[1]) {
    case 1: break;
    case 2:
        if (!led_driver_set_brightness(data[4])) ack_result = 1;
        break;
    case 6:
    case 7:
        if (!air_sensor_set_channel_trigger(data[4], get16(data+5), data[1]==7)) ack_result = 1;
        break;
    case 5:
        if (!air_sensor_set_trigger_percent(data[4])) ack_result = 1;
        break;
    case 3:
        if (!air_sensor_request_calibration()) ack_result = 2;
        else was_calibrating = true;
        break;
    case 4: {
        air_snapshot_t a; air_sensor_get_snapshot(&a);
        if (a.calibration_status == AIR_CAL_RUNNING) { ack_result = 2; break; }
        if (!dirty || a.calibration_status != AIR_CAL_DONE) { ack_result = 3; break; }
        memcpy(reboot_baselines, a.baselines, sizeof(reboot_baselines));
        restarting = true; reboot_at = time_us_64() + 250000;
        break;
    }
    default: ack_result = 1; break;
    }
}

void controller_control_task(void) {
    uint64_t now = time_us_64();
    if (was_calibrating && now >= next_calibration_check) {
        next_calibration_check = now + 1000;
        air_snapshot_t a; air_sensor_get_snapshot(&a);
        if (a.calibration_status != AIR_CAL_RUNNING) {
            if (a.calibration_status == AIR_CAL_DONE) dirty = true;
            was_calibrating = false;
        }
    }
    if (restarting && now >= reboot_at) {
        for (unsigned i = 0; i < 3; ++i)
            watchdog_hw->scratch[i+1] = reboot_baselines[2*i] | ((uint32_t)reboot_baselines[2*i+1] << 16);
        watchdog_hw->scratch[0] = SAVE_MAGIC;
        watchdog_reboot(0, 0, 10);
        while (true) tight_loop_contents();
    }
    // No browser lease: no diagnostic HID traffic. IO4 is always serviced first.
    if (!tud_mounted()) { lease_until = 0; return; }
    if (now >= lease_until || now < next_report || !tud_hid_n_ready(CONTROL_HID_INSTANCE)) return;
    uint8_t report[CONTROL_REPORT_SIZE];
    controller_control_report(report, sizeof(report));
    if (tud_hid_n_report(CONTROL_HID_INSTANCE, CONTROL_REPORT_ID, report, sizeof(report))) next_report = now + 50000;
}
