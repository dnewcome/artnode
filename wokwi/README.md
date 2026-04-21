# Wokwi simulation

Runs the real firmware on an emulated ESP32 with a visual WS2812 strip.
Unlike the Python `sim/`, this exercises the actual code in `src/` —
including `espnow_mesh.cpp` and `FastLED.show()` timing.

## Important: wire `esp:TX`/`esp:RX` to `$serialMonitor` in `diagram.json`

Without these connections, Wokwi produces zero serial output and the sim
appears to hang. The included `diagram.json` already has them.

## One-time setup

1. Install the [Wokwi VSCode extension](https://docs.wokwi.com/vscode/getting-started),
   or install `wokwi-cli` from <https://docs.wokwi.com/wokwi-ci/getting-started>.
2. Edit `src/config.h` for the simulator:
   - `WIFI_SSID "Wokwi-GUEST"` and `WIFI_PASSWORD ""` (the Wokwi WiFi gateway)
   - Reduce `NUM_STRIPS` to `1` and drop the strip's `num_leds` to match the
     diagram (default diagram has 30 pixels on GPIO 16)
   - `ENABLE_HUB75 0` (already default — Wokwi has no HUB75 DMA)
3. Build: `pio run -e wokwi`

## Run

```
# Either: open wokwi/diagram.json in VSCode, hit the green play button
# Or: from the repo root (token loaded from .env — git-ignored)
set -a && . ./.env && set +a
wokwi-cli --timeout 15000 --serial-log-file /tmp/wokwi.log wokwi/
```

Expected boot sequence on serial:

```
[artnode] boot
Connecting to <SSID>...
[artnode] WiFi timeout — falling back to MESH mode
[mesh] ESP-NOW ready
```

## Why the strip stays dark by default

The firmware's default mode is `AUTO`, which becomes **MESH (slave)** on
WiFi timeout. Slaves wait for a bridge to broadcast; with only one chip in
the diagram, nothing drives the strip.

To see the strip light up in a single-chip run:

- Set `WIFI_SSID "Wokwi-GUEST"` / `WIFI_PASSWORD ""` in `src/config.h` —
  bridge mode runs local patterns when no Art-Net arrives, and
  `IDLE_TIMEOUT_MS` (5s) triggers the pattern engine.
- Or, for fully offline runs, change the `cfg.node_mode` default in
  `loadConfig()` (runtime_config.cpp) to `NodeMode::STANDALONE` — runs the
  pattern engine continuously, no network.

## Feeding Art-Net

Wokwi's WiFi gateway lets the simulated chip reach your host. Point any
Art-Net source (QLC+, jinx!, custom script) at the simulated chip's IP —
shown in the Wokwi serial monitor at boot — on UDP 6454.

## Extending to multiple chips (ESP-NOW mesh)

The current `diagram.json` has one ESP32. Wokwi supports multi-chip projects
and ESP-NOW between simulated chips, but the `wokwi.toml` layout for
multi-chip (and which Wokwi tier is required for it) is worth verifying in
the current Wokwi docs before scaffolding. The intended shape:

- Board A: built with `node_mode = BRIDGE`, joins Wokwi-GUEST, forwards
  Art-Net (or local patterns) to the mesh.
- Board B..N: built with `node_mode = MESH`, receive broadcasts and drive
  their own strips.

Two approaches to assigning roles:

1. Two PIO envs (`[env:wokwi-bridge]`, `[env:wokwi-slave]`) with different
   `-D` flags that override the `NodeMode` default in `runtime_config.cpp`.
2. Single build + pre-populate NVS for each chip via the Wokwi diagram
   (if the Wokwi docs still support this).

## Caveats

- Wokwi's ESP-NOW between chips is idealized — no packet loss, no
  contention. For loss/jitter stress use `sim/mesh_viz.py` instead.
- Emulation is slower than real hardware; long strips or high frame rates
  may run below 30 fps inside the sim. This doesn't affect correctness,
  just perceived smoothness.
- `ENABLE_HUB75 1` will not work — the HUB75 DMA peripheral is unemulated.
- `wokwi.toml` points at `firmware.bin`, not `firmware.factory.bin` — the
  app image is what Wokwi expects; the factory blob is for physical flash.
