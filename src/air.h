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

#endif
