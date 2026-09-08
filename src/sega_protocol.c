#include "sega_protocol.h"

static bool is_led_command(uint8_t command) {
    return command == 0x02 ||
           command == 0x05 ||
           command == 0x08 ||
           command == 0x0D;
}

static uint8_t apply_brightness(uint8_t value, uint8_t brightness) {
    if (brightness > 63) {
        brightness = 63;
    }

    return (uint8_t)(((uint16_t)value * brightness + 31) / 63);
}

void sega_protocol_reset(sega_protocol_t *parser) {
    parser->active = false;
    parser->escaped = false;
    parser->position = 0;
    parser->checksum = 0;
}

bool sega_protocol_feed(sega_protocol_t *parser,
                        uint8_t byte,
                        uint32_t output[LED_COUNT]) {
    // FF always starts a new packet.
    if (byte == 0xFF) {
        sega_protocol_reset(parser);
        parser->active = true;
        parser->checksum = 0xFF;
        return false;
    }

    if (!parser->active) {
        return false;
    }

    // FD FC represents a literal FD.
    // FD FE represents a literal FF.
    if (parser->escaped) {
        parser->escaped = false;

        if (byte != 0xFC && byte != 0xFE) {
            sega_protocol_reset(parser);
            return false;
        }

        byte++;
    } else if (byte == 0xFD) {
        parser->escaped = true;
        return false;
    }

    parser->checksum += byte;

    uint16_t position = parser->position++;

    if (position == 0) {
        parser->command = byte;
        return false;
    }

    if (position == 1) {
        parser->length = byte;

        return false;
    }

    if (position < 2 + parser->length) {
        uint16_t index = position - 2;
    
        // Keep only the bytes needed for supported LED packets.
        if (index < sizeof(parser->body)) {
            parser->body[index] = byte;
        }
    
        return false;
    }

    // This is the checksum byte.
    parser->active = false;

    if (parser->checksum != 0 ||
        !is_led_command(parser->command) ||
        (parser->length != LED_PACKET_BODY_SIZE &&
         parser->length != LED_PACKET_BODY_MAX)) {
        return false;
    }

    uint8_t brightness = parser->body[0];

    for (uint32_t packet_led = 0; packet_led < LED_COUNT; packet_led++) {
        uint32_t source = 1 + packet_led * 3;

        uint8_t blue = apply_brightness(parser->body[source], brightness);
        uint8_t red = apply_brightness(parser->body[source + 1], brightness);
        uint8_t green = apply_brightness(parser->body[source + 2], brightness);

        /*
         * Store as GRB, ready for SK6812 output.
         *
         * The protocol addresses LEDs from the right. Your logical array is
         * left-to-right, therefore D1/rightmost is logical index 30.
         */
        uint32_t logical_led = LED_COUNT - 1 - packet_led;

        output[logical_led] =
            ((uint32_t)green << 24) |
            ((uint32_t)red << 16) |
            ((uint32_t)blue << 8);
    }

    return true;
}