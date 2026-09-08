#ifndef CALIBRATION_H
#define CALIBRATION_H

#include <stdbool.h>
#include <stdint.h>

#include "io4.h"

bool calibration_load(uint16_t baselines[IO4_AIR_CHANNEL_COUNT]);
bool calibration_save(const uint16_t baselines[IO4_AIR_CHANNEL_COUNT]);

#endif