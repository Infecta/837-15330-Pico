#ifndef USB_DESCRIPTORS_H
#define USB_DESCRIPTORS_H

#include "tusb.h"

enum {
    IO4_REPORT_ID_INPUT = 1,
    IO4_REPORT_ID_OUTPUT = 16,
};

#define IO4_REPORT_DESCRIPTOR                                      \
    HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),                        \
    HID_USAGE(HID_USAGE_DESKTOP_GAMEPAD),                          \
    HID_COLLECTION(HID_COLLECTION_APPLICATION),                    \
        HID_REPORT_ID(IO4_REPORT_ID_INPUT)                        \
        HID_LOGICAL_MIN(0), HID_LOGICAL_MAX_N(65535, 2),          \
        HID_REPORT_COUNT(1), HID_REPORT_SIZE(16),                 \
        HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),                    \
        HID_USAGE(HID_USAGE_DESKTOP_X),                            \
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),         \
        HID_REPORT_COUNT(1), HID_REPORT_SIZE(16),                 \
        HID_USAGE(HID_USAGE_DESKTOP_Y),                            \
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),         \
        HID_REPORT_COUNT(1), HID_REPORT_SIZE(16),                 \
        HID_USAGE(HID_USAGE_DESKTOP_X),                            \
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),         \
        HID_REPORT_COUNT(1), HID_REPORT_SIZE(16),                 \
        HID_USAGE(HID_USAGE_DESKTOP_Y),                            \
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),         \
        HID_REPORT_COUNT(1), HID_REPORT_SIZE(16),                 \
        HID_USAGE(HID_USAGE_DESKTOP_X),                            \
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),         \
        HID_REPORT_COUNT(1), HID_REPORT_SIZE(16),                 \
        HID_USAGE(HID_USAGE_DESKTOP_Y),                            \
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),         \
        HID_REPORT_COUNT(1), HID_REPORT_SIZE(16),                 \
        HID_USAGE(HID_USAGE_DESKTOP_X),                            \
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),         \
        HID_REPORT_COUNT(1), HID_REPORT_SIZE(16),                 \
        HID_USAGE(HID_USAGE_DESKTOP_Y),                            \
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),         \
        HID_REPORT_COUNT(1), HID_REPORT_SIZE(16),                 \
        HID_USAGE(HID_USAGE_DESKTOP_RX),                           \
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),         \
        HID_REPORT_COUNT(1), HID_REPORT_SIZE(16),                 \
        HID_USAGE(HID_USAGE_DESKTOP_RY),                           \
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),         \
        HID_REPORT_COUNT(1), HID_REPORT_SIZE(16),                 \
        HID_USAGE(HID_USAGE_DESKTOP_RX),                           \
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),         \
        HID_REPORT_COUNT(1), HID_REPORT_SIZE(16),                 \
        HID_USAGE(HID_USAGE_DESKTOP_RY),                           \
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),         \
        HID_REPORT_COUNT(1), HID_REPORT_SIZE(16),                 \
        HID_USAGE(HID_USAGE_DESKTOP_SLIDER),                       \
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),         \
        HID_REPORT_COUNT(1), HID_REPORT_SIZE(16),                 \
        HID_USAGE(HID_USAGE_DESKTOP_SLIDER),                       \
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),         \
        HID_USAGE_PAGE(HID_USAGE_PAGE_BUTTON),                    \
        HID_LOGICAL_MIN(0), HID_LOGICAL_MAX(1),                   \
        HID_REPORT_COUNT(48), HID_REPORT_SIZE(1),                 \
        HID_USAGE_MAX_N(48, 2), HID_USAGE_MIN_N(1, 2),             \
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),         \
        HID_REPORT_COUNT(1), HID_REPORT_SIZE(232),                \
        HID_INPUT(HID_CONSTANT | HID_ABSOLUTE),                    \
        HID_USAGE_PAGE_N(0xffa0, 2),                              \
        HID_USAGE(0x00),                                          \
        HID_REPORT_ID(IO4_REPORT_ID_OUTPUT)                       \
        HID_COLLECTION(HID_COLLECTION_APPLICATION),                \
            HID_USAGE(0x00),                                      \
            HID_LOGICAL_MIN(0), HID_LOGICAL_MAX(255),              \
            HID_REPORT_COUNT(63), HID_REPORT_SIZE(8),              \
            HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),    \
        HID_COLLECTION_END,                                        \
    HID_COLLECTION_END

// Separate vendor-defined interface: the IO4 gamepad stays byte-for-byte compatible.
#define CONTROL_HID_INSTANCE 1
#define CONTROL_REPORT_ID 1
#define CONTROL_REPORT_SIZE 63
#define CONTROL_REPORT_DESCRIPTOR \
    HID_USAGE_PAGE_N(0xff60, 2), HID_USAGE(1), \
    HID_COLLECTION(HID_COLLECTION_APPLICATION), \
        HID_REPORT_ID(CONTROL_REPORT_ID) \
        HID_LOGICAL_MIN(0), HID_LOGICAL_MAX_N(255, 2), \
        HID_REPORT_SIZE(8), HID_REPORT_COUNT(CONTROL_REPORT_SIZE), \
        HID_USAGE(2), HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        HID_USAGE(3), HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
    HID_COLLECTION_END
#endif