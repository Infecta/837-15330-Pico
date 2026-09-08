#ifndef IO4_H
#define IO4_H

#include <stdbool.h>
#include <stdint.h>

enum {
    IO4_AIR_LEFT_A = 0,
    IO4_AIR_RIGHT_A,
    IO4_AIR_LEFT_B,
    IO4_AIR_RIGHT_B,
    IO4_AIR_LEFT_C,
    IO4_AIR_RIGHT_C,
    IO4_AIR_CHANNEL_COUNT,
};

typedef struct __attribute__((packed)) {
    uint16_t analog[8];
    uint16_t rotary[4];
    uint16_t coin[2];
    uint16_t buttons[2];
    uint8_t system_status;
    uint8_t usb_status;
    uint8_t reserved[29];
} io4_input_report_t;

typedef struct {
    uint8_t system_status;
    uint8_t usb_status;

    uint8_t communication_timeout;
    uint8_t sampling_count;

    uint16_t coins[2];
    uint8_t general_output[3];

    uint8_t air_blocked_bitmap;
    bool test_pressed;
    bool service_pressed;
} io4_state_t;

void io4_init(io4_state_t *state);

bool io4_process_command(io4_state_t *state,
                         const uint8_t *payload,
                         uint16_t payload_size);

void io4_build_input_report(const io4_state_t *state,
                            io4_input_report_t *report);

void io4_set_air_blocked(io4_state_t *state, uint8_t blocked_bitmap);
void io4_set_test_pressed(io4_state_t *state, bool pressed);
void io4_set_service_pressed(io4_state_t *state, bool pressed);

#endif