# slidrr Web Serial setup

Build and flash the firmware first; the existing `chunithm_pico_bridge.uf2` must be rebuilt. Start the zero-dependency local server with `node webui/serve.mjs` from the repository root, then open http://127.0.0.1:8371 in desktop Chrome or Edge.

## Remote server (container)

The WebUI is static files. USB still happens in **your** Chrome/Edge on the PC with the Pico plugged in. The container only hosts the page. Web Serial refuses plain `http://` on a public host, so put TLS in front (Caddy, nginx, Traefik, Cloudflare).

From `webui/`:

```
docker compose up -d --build
```

That serves the UI on port 8371. Point a reverse proxy at it, for example Caddy:

```
config.example.com {
    reverse_proxy 127.0.0.1:8371
}
```

Then open `https://config.example.com` on the machine that has the controller. Select **slidrr Config Serial**, not **slidrr Port**.

Without HTTPS, Connect will fail with the Web Serial secure-context error. Do not expect the remote server to see the Pico; it never does. Select **slidrr Config Serial**, not **slidrr Port** (the raw slider UART). Both ports have the same USB VID/PID, so the Web Serial picker cannot distinguish them by its filter; select by name. A matching firmware replies to `HELLO` with `OK HELLO 1` and then `STATUS ...`. `ERR unknown command` indicates a different protocol or firmware on the selected port; raw ADC CSV readings indicate the old diagnostics firmware. Web Serial needs localhost/HTTPS and a browser that supports it. Keep the server running while configuring.

The screen shows six live ADC readings in Left B, Right A, Left A, Right B, Left C, Right C order. Left A and Left B have exchanged positions in both sections; each card still controls its original firmware channel and saved settings. Calibrate with all beams clear for two seconds, or enter each receiver's baseline (32–4095) and trigger drop percentage (1–99, default 12%; lower = more sensitive). Set maximum light brightness (0–100%; default 100%) separately. Type a receiver baseline or trigger drop, then click that receiver's Apply button. Live ADC updates no longer overwrite an unsubmitted edit. Changes apply in RAM after acknowledgement. **Save to Pico flash** explicitly persists the full current configuration. If you reboot without saving, edits are lost. GP4 startup calibration remains supported and saves the same settings record. Existing baseline-only saved calibration is read on upgrade.

Saving erases/programs the last flash sector while both cores are paused. This briefly interrupts USB, air scanning and UART; **stop gameplay before saving**. Flash has finite endurance; do not repeatedly click Save. A failed or interrupted erase may discard the previous saved settings. The last flash sector stores settings; ensure your firmware image does not overlap that sector. The 200 mA LED limiter is an estimated cap, not a measurement.

The port uses a newline-delimited ASCII protocol on CDC 1. `HELLO` enables 10 Hz status while the browser refreshes its three-second lease; `STATUS` requests one sample; `CALIBRATE` starts a two-second calibration; `BASELINE <channel 0–5> <ADC 32–4095>`, `TRIGGER <channel 0–5> <drop percent 1–99>` and `BRIGHTNESS <percent 0–100>` apply settings; `SAVE` persists the applied values. Responses are `OK <command>` or `ERR <reason>`. `STATUS` is followed by 22 unsigned integers: saved flag, calibration state (0 idle / 1 running / 2 success / 3 failure), brightness, blocked bitmap, six raw ADC values, six baselines, then six trigger percentages. The port's CDC baud-rate setting is cosmetic.
