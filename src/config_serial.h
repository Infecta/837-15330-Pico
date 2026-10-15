#ifndef CONFIG_SERIAL_H
#define CONFIG_SERIAL_H

#include "calibration.h"

void config_serial_init(const controller_settings_t *settings, bool saved);
void config_serial_task(void);

#endif
