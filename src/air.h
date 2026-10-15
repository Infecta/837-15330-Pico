#ifndef AIR_H
#define AIR_H

#include <stdint.h>
#include <stdbool.h>
#include "io4.h"

typedef struct {
    uint16_t raw[IO4_AIR_CHANNEL_COUNT];
    uint16_t baselines[IO4_AIR_CHANNEL_COUNT];
    uint8_t trigger_percent[IO4_AIR_CHANNEL_COUNT];
    uint8_t blocked;
    uint8_t calibration_result; /* 0 idle, 1 running, 2 success, 3 failure */
    bool calibrating;
} air_status_t;

void air_sensor_core1_entry(void);

uint8_t air_sensor_blocked_bitmap(void);
uint16_t air_sensor_raw_value(uint8_t channel);
void air_sensor_get_status(air_status_t *status);

void air_sensor_init(void);
void air_sensor_update(void);

bool air_sensor_capture_baselines(
    uint16_t baselines[IO4_AIR_CHANNEL_COUNT]
);

void air_sensor_set_baselines(
    const uint16_t baselines[IO4_AIR_CHANNEL_COUNT]
);

bool air_sensor_set_baseline(uint8_t channel, uint16_t baseline);
bool air_sensor_set_trigger(uint8_t channel, uint8_t percent);
bool air_sensor_start_calibration(void);

#endif
