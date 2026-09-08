#include "usb_descriptors.h"

#include <stddef.h>
#include <string.h>

static const tusb_desc_device_t device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,

    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,

    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor = 0x0ca3,
    .idProduct = 0x0021,
    .bcdDevice = 0x0101,

    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

static const uint8_t io4_report_descriptor[] = {
    IO4_REPORT_DESCRIPTOR,
};

enum {
    INTERFACE_IO4_HID = 0,
    INTERFACE_SLIDER_CDC_CONTROL,
    INTERFACE_SLIDER_CDC_DATA,
    INTERFACE_DIAGNOSTIC_CDC_CONTROL,
    INTERFACE_DIAGNOSTIC_CDC_DATA,
    INTERFACE_COUNT,
};

#define CONFIGURATION_TOTAL_LENGTH \
    (TUD_CONFIG_DESC_LEN + TUD_HID_INOUT_DESC_LEN + TUD_CDC_DESC_LEN + \
     TUD_CDC_DESC_LEN)

#define ENDPOINT_HID_OUT 0x01
#define ENDPOINT_HID_IN  0x81

#define ENDPOINT_CDC_NOTIFICATION 0x82
#define ENDPOINT_CDC_OUT          0x02
#define ENDPOINT_CDC_IN           0x83

#define ENDPOINT_DIAGNOSTIC_NOTIFICATION 0x84
#define ENDPOINT_DIAGNOSTIC_OUT          0x03
#define ENDPOINT_DIAGNOSTIC_IN           0x85

static const uint8_t configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(
        1,
        INTERFACE_COUNT,
        0,
        CONFIGURATION_TOTAL_LENGTH,
        TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP,
        200
    ),

    TUD_HID_INOUT_DESCRIPTOR(
        INTERFACE_IO4_HID,
        4,
        HID_ITF_PROTOCOL_NONE,
        sizeof(io4_report_descriptor),
        ENDPOINT_HID_OUT,
        ENDPOINT_HID_IN,
        CFG_TUD_HID_EP_BUFSIZE,
        1
    ),

    TUD_CDC_DESCRIPTOR(
        INTERFACE_SLIDER_CDC_CONTROL,
        5,
        ENDPOINT_CDC_NOTIFICATION,
        8,
        ENDPOINT_CDC_OUT,
        ENDPOINT_CDC_IN,
        64
    ),

    TUD_CDC_DESCRIPTOR(
        INTERFACE_DIAGNOSTIC_CDC_CONTROL,
        6,
        ENDPOINT_DIAGNOSTIC_NOTIFICATION,
        8,
        ENDPOINT_DIAGNOSTIC_OUT,
        ENDPOINT_DIAGNOSTIC_IN,
        64
    ),
};

static const char *const string_descriptors[] = {
    (const char[]){0x09, 0x04}, // English (United States)
    "SEGA",
    "slidrr-psoc",
    "000001",
    "I/O CONTROL BD;15257;01;90;1831;6679A;00;GOUT=14_ADIN=8,E_ROTIN=4_COININ=2_SWIN=2,E_UQ1=41,6;",
    "slidrr Port",
    "slidrr Air Diagnostics",
};

const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&device_descriptor;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return configuration_descriptor;
}

const uint8_t *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return io4_report_descriptor;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index,
                                         uint16_t language_id) {
    (void)language_id;

    static uint16_t descriptor[128];

    if (index == 0) {
        memcpy(&descriptor[1], string_descriptors[0], 2);
        descriptor[0] = (TUSB_DESC_STRING << 8) | 4;
        return descriptor;
    }

    if (index >=
        sizeof(string_descriptors) / sizeof(string_descriptors[0])) {
        return NULL;
    }

    const char *text = string_descriptors[index];
    size_t length = strlen(text);

    if (length > 127) {
        length = 127;
    }

    for (size_t i = 0; i < length; i++) {
        descriptor[i + 1] = (uint8_t)text[i];
    }

    descriptor[0] =
        (TUSB_DESC_STRING << 8) | (uint16_t)(length * 2 + 2);

    return descriptor;
}
