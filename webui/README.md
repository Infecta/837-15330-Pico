# SLIDRR Control Deck

A local WebHID dashboard with six live air traces, actual calibrated thresholds, LED brightness and calibration controls.

## Start

1. Install the matching `chunithm_pico_bridge-webhid.uf2` on your RP2040 Zero using your normal BOOTSEL procedure. This build has not been flashed automatically.
2. Install Node.js if needed, then run `node webui/serve.mjs` from the project root (or `node serve.mjs` inside this folder).
3. Open http://127.0.0.1:8371 in desktop Chrome or Edge. Keep the server terminal open.
4. Click **Connect controller** and select the controller. Only its separate vendor control interface is requested.

The local app has no npm dependencies. The UI uses optional Google Fonts and falls back to local fonts when offline. WebHID needs a supported desktop browser and localhost or HTTPS; opening index.html directly is not the supported launch method.

## Controls

- Air readings are the latest unfiltered ADC sample for each channel, sent at 20 Hz. They are not every ADC conversion. Channel order is Left A, Right A, Left B, Right B, Left C, Right C, preserving the existing physical and IO4 mapping.
- Trigger drop is adjustable from 1% to 99%, with 12% as the default. Lower values are more sensitive. Click Apply trigger to change all six channels without recalibrating. This setting resets to 12% on restart, including Save calibration & restart. Release uses the existing 90% hysteresis calculation. The displayed integer boundaries come from firmware: blocked at or below trigger, clear strictly above release.
- Brightness ranges from 0% (blackout) to 100% of the existing allowed output. It scales LED colours after the 200 mA estimated RGB power limiter. This estimate is not measured USB current. Brightness resets to 100% after restart and is not saved.
- **Calibrate airs** averages two seconds of clear-beam readings without disconnecting USB. Keep all six beams clear. The new thresholds apply immediately and last until restart/power-off. A very low signal rejects the entire capture and retains the previous baselines.
- **Save calibration & restart** persists a successful new capture. Stop gameplay first. The controller briefly disconnects; click Connect controller after it returns. Flash is written during boot, before UART/USB and the second core start. This avoids a flash-write pause during normal operation.
- Existing saved calibration is retained by the normal firmware update. No recalibration is forced solely by this WebHID addition. Recalibrate if tower position, lighting, emitter resistor or other optical conditions changed, or if the UI reports default baselines.

## Preserved behaviour

Raw CDC 0, diagnostics CDC 1, fixed UART operation, existing IO4 report, all physical pins and IO4 air bits, core 0 USB/UART, core 1 air scanning, GP4 calibration and GP5 SERVICE/startup bootloader are preserved. The removed UART monitor stays removed. No runtime flash write is introduced.

## Validation and limits

Firmware build, native firmware regression tests, UART tests, compiled USB descriptor checks, protocol tests, and isolated browser interaction tests passed. Browser tests use a simulated HID device, including calibration, brightness, save/restart, stale data, reconnect and responsive layout. Actual Windows enumeration, simultaneous game operation, physical LED output and ADC calibration still require testing on the controller. This does not claim to resolve the earlier joy.cpl freeze.

Run protocol tests: `node --test webui/tests/protocol.test.mjs` from the project root. Firmware tests and descriptor checks are under `tests/webhid`.

## Individual receiver tuning

Each receiver now has a sensitivity slider and an exact ADC trigger field. Move right for more sensitivity (a smaller drop from baseline). The linked ADC field previews the same boundary. Press the receiver's Apply button to send it; edits survive live updates and heartbeats. Moving the slider chooses a percentage that follows future calibration. Typing an ADC threshold chooses a fixed boundary below that channel's baseline. Release remains the existing 90% hysteresis calculation. If recalibration lowers the baseline below a fixed boundary, the effective trigger is clamped to one ADC count below baseline; inspect the live reported thresholds after calibration.

The all-channel control replaces individual settings. All tuning resets to 12% after restart, including Save calibration & restart. This tuning changes detection only, not amplifier gain, emitter power, channel mapping or optical paths. Install the matching new UF2 and refresh the page; older firmware has individual controls disabled.
