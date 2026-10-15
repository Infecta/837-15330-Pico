#ifndef CALIBRATION_H
#define CALIBRATION_H

#include <stdbool.h>
#include <stdint.h>

#include "io4.h"

typedef struct {
    uint16_t baselines[IO4_AIR_CHANNEL_COUNT];
    uint8_t trigger_percent[IO4_AIR_CHANNEL_COUNT];
    uint8_t brightness_percent;
} controller_settings_t;

bool calibration_load(uint16_t baselines[IO4_AIR_CHANNEL_COUNT]);
bool calibration_save(const uint16_t baselines[IO4_AIR_CHANNEL_COUNT]);

bool calibration_load_settings(controller_settings_t *settings);
bool calibration_save_settings(const controller_settings_t *settings);

/* Call after multicore_launch_core1 so flash pauses core 1 safely. */
void calibration_enable_runtime_flash(void);

#endif
