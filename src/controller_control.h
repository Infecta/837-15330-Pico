#ifndef CONTROLLER_CONTROL_H
#define CONTROLLER_CONTROL_H
#include <stdbool.h>
#include <stdint.h>
void controller_control_init(bool saved_calibration);
void controller_control_task(void);
void controller_control_receive(uint8_t report_id, const uint8_t *data, uint16_t size);
uint16_t controller_control_report(uint8_t *data, uint16_t size);
bool controller_control_restore_after_reboot(uint16_t baselines[6]);
#endif
