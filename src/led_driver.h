#ifndef LED_DRIVER_H
#define LED_DRIVER_H

#include <stdint.h>

#include "sega_protocol.h"

void led_driver_init(void);
void led_driver_submit(const uint32_t logical_frame[LED_COUNT]);
void led_driver_task(void);

void led_driver_set_solid(uint8_t red,
    uint8_t green,
    uint8_t blue);
#endif