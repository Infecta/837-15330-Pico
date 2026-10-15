#include "config_serial.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tusb.h"
#include "pico/stdlib.h"
#include "air.h"
#include "led_driver.h"

#define CONFIG_CDC 1
#define RX_LINE_SIZE 64
#define TELEMETRY_INTERVAL_MS 33

static controller_settings_t current;
static bool saved_settings;
static char rx_line[RX_LINE_SIZE];
static unsigned rx_length;
static bool rx_overflow;
static bool subscribed;
static absolute_time_t next_telemetry;
static uint8_t last_calibration_result;
static uint64_t connection_lease_until;

static void reply(const char *message) {
    if (!tud_mounted() || !tud_cdc_n_connected(CONFIG_CDC)) return;
    uint32_t size = (uint32_t)strlen(message);
    if (tud_cdc_n_write_available(CONFIG_CDC) >= size) {
        tud_cdc_n_write(CONFIG_CDC, message, size);
        tud_cdc_n_write_flush(CONFIG_CDC);
    }
}

static bool parse_number(const char *text, unsigned maximum, unsigned *value) {
    if (*text == '\0') return false;
    unsigned number = 0;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9') return false;
        unsigned digit = (unsigned)(*text - '0');
        if (digit > maximum || number > (maximum - digit) / 10u) return false;
        number = number * 10u + digit;
    }
    *value = number;
    return true;
}

static void status_line(void) {
    if (!tud_mounted() || !tud_cdc_n_connected(CONFIG_CDC)) return;
    air_status_t status;
    air_sensor_get_status(&status);
    char line[256];
    int n = snprintf(line, sizeof(line),
        "STATUS %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u\n",
        saved_settings ? 1u : 0u, status.calibration_result,
        (unsigned)led_driver_brightness(), (unsigned)status.blocked,
        (unsigned)status.raw[0], (unsigned)status.raw[1],
        (unsigned)status.raw[2], (unsigned)status.raw[3],
        (unsigned)status.raw[4], (unsigned)status.raw[5],
        (unsigned)status.baselines[0], (unsigned)status.baselines[1],
        (unsigned)status.baselines[2], (unsigned)status.baselines[3],
        (unsigned)status.baselines[4], (unsigned)status.baselines[5],
        (unsigned)status.trigger_percent[0], (unsigned)status.trigger_percent[1],
        (unsigned)status.trigger_percent[2], (unsigned)status.trigger_percent[3],
        (unsigned)status.trigger_percent[4], (unsigned)status.trigger_percent[5]);
    if (n > 0 && (size_t)n < sizeof(line)) reply(line);
}

static void handle_line(char *line) {
    char *args[4];
    unsigned count = 0;
    for (char *part = strtok(line, " \t"); part != NULL;
         part = strtok(NULL, " \t")) {
        if (count == 4) { reply("ERR command\n"); return; }
        args[count++] = part;
    }
    if (count == 0) return;
    connection_lease_until = time_us_64() + 3000000u;
    if (strcmp(args[0], "HELLO") == 0 && count == 1) {
        subscribed = true;
        connection_lease_until = time_us_64() + 3000000u;
        reply("OK HELLO 1\n");
        next_telemetry = get_absolute_time();
    } else if (strcmp(args[0], "STATUS") == 0 && count == 1) {
        status_line();
    } else if (strcmp(args[0], "CALIBRATE") == 0 && count == 1) {
        if (air_sensor_start_calibration()) {
            last_calibration_result = 1;
            reply("OK CALIBRATE\n");
            status_line();
        } else reply("ERR busy\n");
    } else if ((strcmp(args[0], "BASELINE") == 0 ||
                strcmp(args[0], "TRIGGER") == 0) && count == 3) {
        unsigned channel, value;
        bool baseline = strcmp(args[0], "BASELINE") == 0;
        air_status_t status;
        air_sensor_get_status(&status);
        if (baseline && status.calibrating) {
            reply("ERR busy\n");
        } else if (!parse_number(args[1], 5, &channel) ||
                   !parse_number(args[2], baseline ? 4095 : 99, &value) ||
                   value < (baseline ? 32u : 1u)) {
            reply("ERR range\n");
        } else if (baseline ? air_sensor_set_baseline(channel, value) :
                   air_sensor_set_trigger(channel, value)) {
            if (baseline) current.baselines[channel] = value;
            else current.trigger_percent[channel] = value;
            saved_settings = false;
            reply(baseline ? "OK BASELINE\n" : "OK TRIGGER\n");
            status_line();
        } else reply(baseline ? "ERR busy\n" : "ERR range\n");
    } else if (strcmp(args[0], "BRIGHTNESS") == 0 && count == 2) {
        unsigned value;
        if (!parse_number(args[1], 100, &value) || !led_driver_set_brightness(value))
            reply("ERR range\n");
        else {
            current.brightness_percent = value;
            saved_settings = false;
            reply("OK BRIGHTNESS\n");
            status_line();
        }
    } else if (strcmp(args[0], "SAVE") == 0 && count == 1) {
        air_status_t status;
        air_sensor_get_status(&status);
        if (status.calibrating) { reply("ERR busy\n"); return; }
        // Copy the last applied values; never persist a pending mailbox update.
        for (unsigned i = 0; i < IO4_AIR_CHANNEL_COUNT; ++i) {
            if (current.baselines[i] != status.baselines[i] ||
                current.trigger_percent[i] != status.trigger_percent[i]) {
                reply("ERR pending\n");
                return;
            }
        }
        memcpy(current.baselines, status.baselines, sizeof(current.baselines));
        memcpy(current.trigger_percent, status.trigger_percent,
               sizeof(current.trigger_percent));
        current.brightness_percent = led_driver_brightness();
        // This blocks both cores and USB for a short flash erase/program pause.
        bool ok = calibration_save_settings(&current);
        if (ok) saved_settings = true;
        reply(ok ? "OK SAVE\n" : "ERR flash\n");
        status_line();
    } else reply("ERR command\n");
}

void config_serial_init(const controller_settings_t *settings, bool saved) {
    current = *settings;
    saved_settings = saved;
    next_telemetry = get_absolute_time();
}

void config_serial_task(void) {
    if (!tud_mounted() || !tud_cdc_n_connected(CONFIG_CDC)) {
        rx_length = 0;
        rx_overflow = false;
        subscribed = false;
        return;
    }
    for (unsigned budget = 0; budget < 64 && tud_cdc_n_available(CONFIG_CDC); ++budget) {
        char c = (char)tud_cdc_n_read_char(CONFIG_CDC);
        if (c == '\n' || c == '\r') {
            if (rx_overflow) reply("ERR line-too-long\n");
            else if (rx_length) { rx_line[rx_length] = '\0'; handle_line(rx_line); }
            rx_length = 0;
            rx_overflow = false;
        } else if (!rx_overflow) {
            if (rx_length + 1 < sizeof(rx_line)) rx_line[rx_length++] = c;
            else rx_overflow = true;
        }
    }
    air_status_t status;
    air_sensor_get_status(&status);
    if (last_calibration_result == 1 && status.calibration_result >= 2) {
        last_calibration_result = status.calibration_result;
        if (status.calibration_result == 2) {
            memcpy(current.baselines, status.baselines, sizeof(current.baselines));
            saved_settings = false;
        }
        status_line();
    }
    if (subscribed && time_us_64() >= connection_lease_until) subscribed = false;
    if (subscribed && time_reached(next_telemetry)) {
        next_telemetry = make_timeout_time_ms(TELEMETRY_INTERVAL_MS);
        status_line();
    }
}
