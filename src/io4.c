#include "io4.h"

#include <string.h>

#define IO4_COMMAND_SET_COMM_TIMEOUT  0x01
#define IO4_COMMAND_SET_SAMPLING_COUNT 0x02
#define IO4_COMMAND_CLEAR_STATUS      0x03
#define IO4_COMMAND_SET_GENERAL_OUTPUT 0x04
#define IO4_COMMAND_SET_PWM_OUTPUT    0x05
#define IO4_COMMAND_SET_UNIQUE_OUTPUT 0x41

#define IO4_STATUS_TIMEOUT_SET  0x10
#define IO4_STATUS_SAMPLING_SET 0x20

#define IO4_BUTTON_TEST    (1 << 9)
#define IO4_BUTTON_SERVICE (1 << 6)

_Static_assert(sizeof(io4_input_report_t) == 63,
               "IO4 input report must be exactly 63 bytes");

void io4_init(io4_state_t *state) {
    if (state == NULL) {
        return;
    }

    memset(state, 0, sizeof(*state));
}

void io4_set_air_blocked(io4_state_t *state, uint8_t blocked_bitmap) {
    if (state == NULL) {
        return;
    }

    state->air_blocked_bitmap =
        blocked_bitmap & ((1 << IO4_AIR_CHANNEL_COUNT) - 1);
}

void io4_set_test_pressed(io4_state_t *state, bool pressed) {
    if (state != NULL) {
        state->test_pressed = pressed;
    }
}

void io4_set_service_pressed(io4_state_t *state, bool pressed) {
    if (state != NULL) {
        state->service_pressed = pressed;
    }
}

bool io4_process_command(io4_state_t *state,
                         const uint8_t *payload,
                         uint16_t payload_size) {
    if (state == NULL || payload == NULL || payload_size == 0) {
        return false;
    }

    switch (payload[0]) {
    case IO4_COMMAND_SET_COMM_TIMEOUT:
        if (payload_size < 2) {
            return false;
        }

        state->communication_timeout = payload[1];
        state->system_status |=
            IO4_STATUS_TIMEOUT_SET | IO4_STATUS_SAMPLING_SET;
        return true;

    case IO4_COMMAND_SET_SAMPLING_COUNT:
        if (payload_size < 2) {
            return false;
        }

        state->sampling_count = payload[1];
        state->system_status |=
            IO4_STATUS_TIMEOUT_SET | IO4_STATUS_SAMPLING_SET;
        return true;

    case IO4_COMMAND_CLEAR_STATUS:
        state->coins[0] = 0;
        state->coins[1] = 0;
        state->system_status = 0;
        return true;

    case IO4_COMMAND_SET_GENERAL_OUTPUT:
        if (payload_size < 4) {
            return false;
        }

        state->general_output[0] = payload[1];
        state->general_output[1] = payload[2];
        state->general_output[2] = payload[3] & 0x0F;
        return true;

    case IO4_COMMAND_SET_PWM_OUTPUT:
    case IO4_COMMAND_SET_UNIQUE_OUTPUT:
        // Required for compatibility, but does not control hardware yet.
        return true;

    case 0x84:
    case 0x85:
    case 0x88:
        return true;

    default:
        return false;
    }
}

void io4_build_input_report(const io4_state_t *state,
                            io4_input_report_t *report) {
    if (state == NULL || report == NULL) {
        return;
    }

    memset(report, 0, sizeof(*report));

    report->system_status = state->system_status;
    report->usb_status = state->usb_status;
    report->coin[0] = state->coins[0];
    report->coin[1] = state->coins[1];

    // Air inputs are active-low
    // Don't ask me what they do because I don't know either lol
    static const uint16_t air_button_map[IO4_AIR_CHANNEL_COUNT][2] = {
        {0, 1 << 11}, // Left A
        {1 << 11, 0}, // Right A
        {0, 1 << 12}, // Left B
        {1 << 12, 0}, // Right B
        {0, 1 << 13}, // Left C
        {1 << 13, 0}, // Right C
    };

    for (uint8_t channel = 0; channel < IO4_AIR_CHANNEL_COUNT; channel++) {
        bool blocked =
            (state->air_blocked_bitmap & (1u << channel)) != 0;

        if (!blocked) {
            report->buttons[0] |= air_button_map[channel][0];
            report->buttons[1] |= air_button_map[channel][1];
        }
    }

    if (state->test_pressed) {
        report->buttons[0] |= IO4_BUTTON_TEST;
    }

    if (state->service_pressed) {
        report->buttons[0] |= IO4_BUTTON_SERVICE;
    }
}