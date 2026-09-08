#include <string.h>
#include <stdio.h>
#include "bsp/board.h"
#include "hardware/gpio.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "tusb.h"
#include "air.h"
#include "calibration.h"
#include "io4.h"
#include "led_driver.h"
#include "uart_bridge.h"
#include "usb_descriptors.h"

#define TEST_PIN 4
#define IO4_REPORT_INTERVAL_MS 4
#define AIR_DIAGNOSTIC_CDC 1
#define AIR_DIAGNOSTIC_INTERVAL_MS 100

static absolute_time_t next_io4_report;
static absolute_time_t next_air_diagnostic;

static io4_state_t io4_state;
static io4_input_report_t io4_report;

static void air_diagnostic_task(void) {
    if (!time_reached(next_air_diagnostic)) {
        return;
    }

    char line[64];
    const int length = snprintf(
        line,
        sizeof(line),
        "%u,%u,%u,%u,%u,%u\r\n",
        (unsigned)air_sensor_raw_value(IO4_AIR_LEFT_A),
        (unsigned)air_sensor_raw_value(IO4_AIR_RIGHT_A),
        (unsigned)air_sensor_raw_value(IO4_AIR_LEFT_B),
        (unsigned)air_sensor_raw_value(IO4_AIR_RIGHT_B),
        (unsigned)air_sensor_raw_value(IO4_AIR_LEFT_C),
        (unsigned)air_sensor_raw_value(IO4_AIR_RIGHT_C)
    );

    if (length <= 0 || (uint32_t)length > sizeof(line) ||
        tud_cdc_n_write_available(AIR_DIAGNOSTIC_CDC) < (uint32_t)length) {
        return;
    }

    (void)tud_cdc_n_write(AIR_DIAGNOSTIC_CDC, line, (uint32_t)length);
    (void)tud_cdc_n_write_flush(AIR_DIAGNOSTIC_CDC);
    next_air_diagnostic =
        make_timeout_time_ms(AIR_DIAGNOSTIC_INTERVAL_MS);
}

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

static void show_colour_for(uint8_t red,
    uint8_t green,
    uint8_t blue,
    uint32_t duration_ms) {
    led_driver_set_solid(red, green, blue);

    absolute_time_t deadline =
        make_timeout_time_ms(duration_ms);

    while (!time_reached(deadline)) {
        led_driver_task();
        tight_loop_contents();
    }
}

int main(void) {
    uint16_t baselines[IO4_AIR_CHANNEL_COUNT];

    board_init();
    air_sensor_init();
    led_driver_init();

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
    
        if (air_sensor_capture_baselines(baselines) &&
            calibration_save(baselines)) {
            air_sensor_set_baselines(baselines);
    
            /* Green remains until game LED traffic replaces it. */
            led_driver_set_solid(0, 255, 0);
        }
    
        /*
         * Leave the display red when capture or flash saving fails.
         */
    } else if (calibration_load(baselines)) {
        air_sensor_set_baselines(baselines);
    }

    uart_bridge_init();
    io4_init( & io4_state);
    tusb_init();
    next_io4_report = get_absolute_time();
    next_air_diagnostic = get_absolute_time();

    // Flash operations are finished before this starts.
    multicore_launch_core1(air_sensor_core1_entry);

    while (true) {
        tud_task();

        uart_bridge_task();
        led_driver_task();
        air_diagnostic_task();

        io4_set_test_pressed( & io4_state, !gpio_get(TEST_PIN));

        io4_set_air_blocked( &
            io4_state,
            air_sensor_blocked_bitmap()
        );

        if (tud_hid_n_ready(0) && time_reached(next_io4_report)) {
            io4_build_input_report( & io4_state, & io4_report);

            tud_hid_n_report(
                0,
                IO4_REPORT_ID_INPUT, &
                io4_report,
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
    uint8_t * buffer,
    uint16_t requested_length) {
    (void) instance;

    if (report_id != IO4_REPORT_ID_INPUT ||
        report_type != HID_REPORT_TYPE_INPUT ||
        buffer == NULL) {
        return 0;
    }

    io4_build_input_report( & io4_state, & io4_report);

    uint16_t length = requested_length < sizeof(io4_report) ?
        requested_length :
        sizeof(io4_report);

    memcpy(buffer, & io4_report, length);
    return length;
}

void tud_hid_set_report_cb(uint8_t instance,
    uint8_t report_id,
    hid_report_type_t report_type,
    const uint8_t * buffer,
        uint16_t buffer_size) {
    (void) instance;

    if (report_type != HID_REPORT_TYPE_OUTPUT || buffer == NULL) {
        return;
    }

    const uint8_t * payload = buffer;
    uint16_t payload_size = buffer_size;

    if (report_id == 0) {
        if (buffer_size < 2 ||
            buffer[0] != IO4_REPORT_ID_OUTPUT) {
            return;
        }

        payload = & buffer[1];
        payload_size = buffer_size - 1;
    } else if (report_id != IO4_REPORT_ID_OUTPUT) {
        return;
    }

    io4_process_command( &
        io4_state,
        payload,
        payload_size
    );
}
