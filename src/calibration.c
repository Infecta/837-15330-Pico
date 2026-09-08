#include "calibration.h"

#include <stddef.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#define CALIBRATION_MAGIC 0x41495243u // "AIRC"
#define CALIBRATION_VERSION 1u

#define CALIBRATION_FLASH_OFFSET \
    (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint16_t baselines[IO4_AIR_CHANNEL_COUNT];
    uint32_t crc;
} calibration_record_t;

static uint32_t crc32(const void *data, uint32_t length) {
    const uint8_t *bytes = data;
    uint32_t crc = UINT32_MAX;

    for (uint32_t i = 0; i < length; i++) {
        crc ^= bytes[i];

        for (uint8_t bit = 0; bit < 8; bit++) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }

    return ~crc;
}

bool calibration_load(uint16_t baselines[IO4_AIR_CHANNEL_COUNT]) {
    const calibration_record_t *record =
        (const calibration_record_t *)
        (XIP_BASE + CALIBRATION_FLASH_OFFSET);

    if (record->magic != CALIBRATION_MAGIC ||
        record->version != CALIBRATION_VERSION ||
        record->size != sizeof(calibration_record_t)) {
        return false;
    }

    uint32_t expected_crc = crc32(
        record,
        offsetof(calibration_record_t, crc)
    );

    if (record->crc != expected_crc) {
        return false;
    }

    memcpy(baselines, record->baselines, sizeof(record->baselines));
    return true;
}

bool calibration_save(const uint16_t baselines[IO4_AIR_CHANNEL_COUNT]) {
    uint8_t page[FLASH_PAGE_SIZE];

    memset(page, 0xFF, sizeof(page));

    calibration_record_t *record =
        (calibration_record_t *)page;

    record->magic = CALIBRATION_MAGIC;
    record->version = CALIBRATION_VERSION;
    record->size = sizeof(calibration_record_t);

    memcpy(record->baselines, baselines, sizeof(record->baselines));

    record->crc = crc32(
        record,
        offsetof(calibration_record_t, crc)
    );

    /*
     * This must run before multicore_launch_core1().
     * Interrupts are disabled because flash cannot be read while writing.
     */
    uint32_t interrupt_state = save_and_disable_interrupts();

    flash_range_erase(
        CALIBRATION_FLASH_OFFSET,
        FLASH_SECTOR_SIZE
    );

    flash_range_program(
        CALIBRATION_FLASH_OFFSET,
        page,
        FLASH_PAGE_SIZE
    );

    restore_interrupts(interrupt_state);

    uint16_t verified[IO4_AIR_CHANNEL_COUNT];

    return calibration_load(verified) &&
        memcmp(verified, baselines, sizeof(verified)) == 0;
}