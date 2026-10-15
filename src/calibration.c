#include "calibration.h"

#include <stddef.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#define CALIBRATION_MAGIC 0x41495243u /* "AIRC" */
#define CALIBRATION_VERSION_BASELINES 1
#define CALIBRATION_VERSION_SETTINGS 2

#define CALIBRATION_FLASH_OFFSET \
    (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint16_t baselines[IO4_AIR_CHANNEL_COUNT];
    uint32_t crc;
} calibration_baselines_record_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint16_t baselines[IO4_AIR_CHANNEL_COUNT];
    uint8_t trigger_percent[IO4_AIR_CHANNEL_COUNT];
    uint8_t brightness_percent;
    uint8_t reserved;
    uint32_t crc;
} calibration_settings_record_t;

static bool runtime_flash;

void calibration_enable_runtime_flash(void) {
    runtime_flash = true;
}

static uint32_t crc32(const void *data, uint32_t length) {
    const uint8_t *bytes = data;
    uint32_t crc = UINT32_MAX;

    for (uint32_t i = 0; i < length; i++) {
        crc ^= bytes[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }

    return ~crc;
}

static void default_settings(controller_settings_t *settings) {
    for (unsigned i = 0; i < IO4_AIR_CHANNEL_COUNT; ++i) {
        settings->baselines[i] = 3800;
        settings->trigger_percent[i] = 12;
    }
    settings->brightness_percent = 100;
}

static bool flash_program_page(const uint8_t page[FLASH_PAGE_SIZE]) {
    if (runtime_flash) multicore_lockout_start_blocking();
    uint32_t interrupt_state = save_and_disable_interrupts();
    flash_range_erase(CALIBRATION_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(CALIBRATION_FLASH_OFFSET, page, FLASH_PAGE_SIZE);
    restore_interrupts(interrupt_state);
    if (runtime_flash) multicore_lockout_end_blocking();
    return true;
}

bool calibration_load_settings(controller_settings_t *settings) {
    if (settings == NULL) return false;
    default_settings(settings);

    const uint8_t *flash = (const uint8_t *)(XIP_BASE + CALIBRATION_FLASH_OFFSET);
    const calibration_settings_record_t *full =
        (const calibration_settings_record_t *)flash;

    if (full->magic == CALIBRATION_MAGIC &&
        full->version == CALIBRATION_VERSION_SETTINGS &&
        full->size == sizeof(calibration_settings_record_t)) {
        uint32_t expected = crc32(full, offsetof(calibration_settings_record_t, crc));
        if (full->crc != expected) return false;
        for (unsigned i = 0; i < IO4_AIR_CHANNEL_COUNT; ++i) {
            if (full->baselines[i] < 32 || full->baselines[i] > 4095) return false;
            if (full->trigger_percent[i] < 1 || full->trigger_percent[i] > 99) return false;
        }
        if (full->brightness_percent > 100) return false;
        memcpy(settings->baselines, full->baselines, sizeof(settings->baselines));
        memcpy(settings->trigger_percent, full->trigger_percent,
               sizeof(settings->trigger_percent));
        settings->brightness_percent = full->brightness_percent;
        return true;
    }

    const calibration_baselines_record_t *legacy =
        (const calibration_baselines_record_t *)flash;
    if (legacy->magic != CALIBRATION_MAGIC ||
        legacy->version != CALIBRATION_VERSION_BASELINES ||
        legacy->size != sizeof(calibration_baselines_record_t)) {
        return false;
    }

    uint32_t expected = crc32(legacy, offsetof(calibration_baselines_record_t, crc));
    if (legacy->crc != expected) return false;
    for (unsigned i = 0; i < IO4_AIR_CHANNEL_COUNT; ++i) {
        if (legacy->baselines[i] < 32 || legacy->baselines[i] > 4095) return false;
    }
    memcpy(settings->baselines, legacy->baselines, sizeof(settings->baselines));
    return true;
}

bool calibration_save_settings(const controller_settings_t *settings) {
    if (settings == NULL) return false;
    for (unsigned i = 0; i < IO4_AIR_CHANNEL_COUNT; ++i) {
        if (settings->baselines[i] < 32 || settings->baselines[i] > 4095) return false;
        if (settings->trigger_percent[i] < 1 || settings->trigger_percent[i] > 99) {
            return false;
        }
    }
    if (settings->brightness_percent > 100) return false;

    uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));
    calibration_settings_record_t *record = (calibration_settings_record_t *)page;
    record->magic = CALIBRATION_MAGIC;
    record->version = CALIBRATION_VERSION_SETTINGS;
    record->size = sizeof(calibration_settings_record_t);
    memcpy(record->baselines, settings->baselines, sizeof(record->baselines));
    memcpy(record->trigger_percent, settings->trigger_percent,
           sizeof(record->trigger_percent));
    record->brightness_percent = settings->brightness_percent;
    record->reserved = 0;
    record->crc = crc32(record, offsetof(calibration_settings_record_t, crc));

    flash_program_page(page);

    controller_settings_t verified;
    return calibration_load_settings(&verified) &&
        memcmp(verified.baselines, settings->baselines, sizeof(verified.baselines)) == 0 &&
        memcmp(verified.trigger_percent, settings->trigger_percent,
               sizeof(verified.trigger_percent)) == 0 &&
        verified.brightness_percent == settings->brightness_percent;
}

bool calibration_load(uint16_t baselines[IO4_AIR_CHANNEL_COUNT]) {
    controller_settings_t settings;
    if (!calibration_load_settings(&settings)) return false;
    memcpy(baselines, settings.baselines, sizeof(settings.baselines));
    return true;
}

bool calibration_save(const uint16_t baselines[IO4_AIR_CHANNEL_COUNT]) {
    controller_settings_t settings;
    default_settings(&settings);
    memcpy(settings.baselines, baselines, sizeof(settings.baselines));
    return calibration_save_settings(&settings);
}
