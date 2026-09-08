#ifndef SEGA_PROTOCOL_H
#define SEGA_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#define LED_COUNT 31
#define LED_PACKET_BODY_SIZE (1 + 3 * LED_COUNT)
#define LED_PACKET_BODY_MAX  (LED_PACKET_BODY_SIZE + 3)

typedef struct {
    bool active;
    bool escaped;

    uint8_t command;
    uint8_t length;
    uint8_t checksum;
    uint16_t position;

    uint8_t body[LED_PACKET_BODY_MAX];
} sega_protocol_t;

void sega_protocol_reset(sega_protocol_t *parser);

/*
 * Feed one raw host-to-CY8C byte.
 * Returns true only when a complete, valid LED frame is decoded.
 *
 * output is logical left-to-right pixel order.
 */
bool sega_protocol_feed(sega_protocol_t *parser,
                        uint8_t byte,
                        uint32_t output[LED_COUNT]);

#endif