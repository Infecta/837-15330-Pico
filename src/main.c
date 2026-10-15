#include <string.h>
#include <stdio.h>
#include "bsp/board.h"
#include "hardware/gpio.h"
#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "tusb.h"
#include "air.h"
#include "calibration.h"
#include "config_serial.h"
#include "io4.h"
#include "led_driver.h"
#include "uart_bridge.h"
#include "usb_descriptors.h"

#define TEST_PIN 4
#define SERVICE_PIN 5
/* simpl-slidrr / arcade cadence ~83 Hz. 1 ms flooded joy.cpl. */
#define IO4_REPORT_INTERVAL_MS 12

static absolute_time_t next_io4_report;

static io4_state_t io4_state;
static io4_input_report_t io4_report;

static bool calibration_requested(void) {
    gpio_init(TEST_PIN);
    gpio_set_dir(TEST_PIN, GPIO_IN);
    gpio_pull_up(TEST_PIN);

    if (gpio_get(TEST_PIN)) {
        return false;
    }

    sleep_ms(25);
    return !gpio_get(TEST_PIN);
}

int main(void) {
    board_init();

    gpio_init(SERVICE_PIN);
    gpio_set_dir(SERVICE_PIN, GPIO_IN);
    gpio_pull_up(SERVICE_PIN);

    /* Allow the pull-up to settle and debounce the bootloader request. */
    sleep_ms(25);
    if (!gpio_get(SERVICE_PIN)) {
        sleep_ms(25);
        if (!gpio_get(SERVICE_PIN)) {
            reset_usb_boot(0, 0);
        }
    }

    air_sensor_init();
    led_driver_init();

    controller_settings_t settings = {
        .baselines = {3800, 3800, 3800, 3800, 3800, 3800},
        .trigger_percent = {12, 12, 12, 12, 12, 12},
        .brightness_percent = 100,
    };
    bool saved_settings = calibration_load_settings(&settings);
    if (saved_settings) {
        air_sensor_set_baselines(settings.baselines);
        for (uint8_t i = 0; i < IO4_AIR_CHANNEL_COUNT; ++i) {
            (void)air_sensor_set_trigger(i, settings.trigger_percent[i]);
        }
        (void)led_driver_set_brightness(settings.brightness_percent);
    }

    if (calibration_requested()) {
        /*
         * Continuously scan for two seconds while the towers are clear.
         * This gives every phase repeated opportunities to stabilize.
         */
        led_driver_set_solid(255, 0, 0);

        absolute_time_t calibration_end =
            make_timeout_time_ms(2000);

        while (!time_reached(calibration_end)) {
            air_sensor_update();
            led_driver_task();
            tight_loop_contents();
        }

        if (air_sensor_capture_baselines(settings.baselines) &&
            calibration_save_settings(&settings)) {
            air_sensor_set_baselines(settings.baselines);
            for (uint8_t i = 0; i < IO4_AIR_CHANNEL_COUNT; ++i) {
                (void)air_sensor_set_trigger(i, settings.trigger_percent[i]);
            }
            (void)led_driver_set_brightness(settings.brightness_percent);
            saved_settings = true;

            /* Green remains until game LED traffic replaces it. */
            led_driver_set_solid(0, 255, 0);
        }

        /*
         * Leave the display red when capture or flash saving fails.
         */
    }

    config_serial_init(&settings, saved_settings);

    uart_bridge_init();
    io4_init(&io4_state);
    tusb_init();
    next_io4_report = get_absolute_time();

    // Flash operations are finished before this starts.
    multicore_launch_core1(air_sensor_core1_entry);
    calibration_enable_runtime_flash();

    while (true) {
        tud_task();

        uart_bridge_task();
        led_driver_task();
        config_serial_task();

        io4_set_test_pressed(&io4_state, !gpio_get(TEST_PIN));
        io4_set_service_pressed(&io4_state, !gpio_get(SERVICE_PIN));

        io4_set_air_blocked(&io4_state, air_sensor_blocked_bitmap());

        if (tud_hid_n_ready(0) && time_reached(next_io4_report)) {
            io4_build_input_report(&io4_state, &io4_report);

            tud_hid_n_report(
                0,
                IO4_REPORT_ID_INPUT,
                &io4_report,
                sizeof(io4_report)
            );

            next_io4_report =
                delayed_by_ms(next_io4_report, IO4_REPORT_INTERVAL_MS);
        }

        tight_loop_contents();
    }
}

uint16_t tud_hid_get_report_cb(uint8_t instance,
    uint8_t report_id,
    hid_report_type_t report_type,
    uint8_t *buffer,
    uint16_t requested_length) {
    /* Match simpl-slidrr: IO4 traffic is interrupt IN only. */
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)requested_length;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance,
    uint8_t report_id,
    hid_report_type_t report_type,
    const uint8_t *buffer,
    uint16_t buffer_size) {
    if (instance != 0) return;

    if (report_type != HID_REPORT_TYPE_OUTPUT || buffer == NULL) {
        return;
    }

    const uint8_t *payload = buffer;
    uint16_t payload_size = buffer_size;

    if (report_id == 0) {
        if (buffer_size < 2 ||
            buffer[0] != IO4_REPORT_ID_OUTPUT) {
            return;
        }

        payload = &buffer[1];
        payload_size = buffer_size - 1;
    } else if (report_id != IO4_REPORT_ID_OUTPUT) {
        return;
    }

    io4_process_command(&io4_state, payload, payload_size);
}
