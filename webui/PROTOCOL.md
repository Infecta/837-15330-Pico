# Control HID v1

VID 0x0CA3, PID 0x0021; vendor collection usage page 0xFF60, usage 1. This is HID instance 1, separate from IO4 (instance 0). USB interface 5, OUT endpoint 0x04, IN endpoint 0x86. Input and output report ID 1 each carry 63 payload bytes; WebHID strips the report ID from DataView. All multi-byte values are little-endian.

Output command payload: byte 0 version 1; byte 1 operation; bytes 2–3 nonzero sequence; byte 4 value; all remaining bytes zero. Operations: 1 subscribe/heartbeat, 2 set brightness (0–100), 3 start clear-beam calibration, 4 save completed calibration and restart, 5 set trigger drop percentage (1–99, default 12 on boot). Command 5 acknowledges the queued request; the next core 1 snapshot reports the applied percentage and thresholds. Refresh subscription each second; telemetry stops after a three-second lease. Normal telemetry interval is 50 ms; command acknowledgements may arrive sooner.

Input payload offsets:

- 0: version (1)
- 1: calibration status: idle 0, running 1, complete 2, failed 3
- 2: six blocked bits in Left A, Right A, Left B, Right B, Left C, Right C order
- 3: brightness percentage
- 4: flags: saved calibration loaded 1, unsaved live calibration 2, restarting 4, boot save failed 8
- 5: trigger drop percent; 6: hysteresis percent
- 7: last command; 8–9: sequence; 10: result (OK 0, invalid 1, busy 2, calibration required 3)
- 11: reserved; 12–13: wrapping snapshot counter
- 14–25: six raw ADC uint16 values
- 26–37: six baseline uint16 values
- 38–49: six trigger uint16 boundaries
- 50–61: six release uint16 boundaries
- 62: estimated LED power ceiling in mA (200)

Only one command should be outstanding per client. Match both sequence and command for acknowledgement. Reported readings are snapshots, not a lossless ADC stream. Calibration requests are handed to core 1 using a short protected mailbox. Save/restart stages values in watchdog scratch 0–3 and consumes the marker once on watchdog reboot, before normal operation.

## Individual tuning extension

Input byte 11 bit 7 advertises individual tuning. Bits 0–5 mark channels configured with absolute ADC thresholds. Input byte 5 is zero when thresholds are individually configured, otherwise the shared percentage. Existing report size, version and USB descriptors are unchanged.

Commands 6 (percentage drop, 1–99) and 7 (absolute trigger, 0–4094 and below baseline) use byte 4 for channel 0–5 and bytes 5–6 for the little-endian value. Commands queue changes to the core 1 mailbox. Telemetry reports effective ADC trigger and release thresholds. Command 5 resets all channels to percentage mode. Individual tuning is volatile. Absolute thresholds are clamped below the baseline if later recalibration reduces it.
