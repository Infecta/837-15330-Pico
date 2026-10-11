#ifndef AIR_H
#define AIR_H

#include <stdint.h>
#include <stdbool.h>
#include "io4.h"

void air_sensor_core1_entry(void);

uint8_t air_sensor_blocked_bitmap(void);
uint16_t air_sensor_raw_value(uint8_t channel);

void air_sensor_init(void);
void air_sensor_update(void);

bool air_sensor_capture_baselines(
    uint16_t baselines[IO4_AIR_CHANNEL_COUNT]
);

void air_sensor_set_baselines(
    const uint16_t baselines[IO4_AIR_CHANNEL_COUNT]
);

enum { AIR_CAL_IDLE, AIR_CAL_RUNNING, AIR_CAL_DONE, AIR_CAL_FAILED };
typedef struct {
    uint16_t raw[IO4_AIR_CHANNEL_COUNT];
    uint16_t baselines[IO4_AIR_CHANNEL_COUNT];
    uint16_t trigger[IO4_AIR_CHANNEL_COUNT];
    uint16_t release[IO4_AIR_CHANNEL_COUNT];
    uint16_t sample_id;
    uint8_t blocked, calibration_status, trigger_percent, hysteresis, absolute_mask;
} air_snapshot_t;
void air_sensor_get_snapshot(air_snapshot_t *snapshot);
bool air_sensor_request_calibration(void);
bool air_sensor_set_trigger_percent(uint8_t percent);
bool air_sensor_set_channel_trigger(uint8_t channel, uint16_t value, bool absolute);

#endif
