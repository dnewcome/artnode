# artnode-rpi

Raspberry Pi port of [artnode](../README.md) — receives [Art-Net](https://art-net.org.uk/) (DMX over UDP) and drives WS2812B LED strips and HUB75 RGB matrix panels. Designed as a full-featured lighting node for art sculptures and installations where more compute, wired Ethernet, or a larger panel display is needed.

This is a self-contained CMake subproject that lives alongside the ESP32 firmware in the same repository. The two projects share no build system but are kept feature-compatible: the same Art-Net universe layout, pattern engine, spatial mapping configuration, and REST API work on both platforms.

> **No mesh:** this port does not implement the ESP-NOW mesh. The RPi listens for Art-Net directly on its own IP address. If you need a mesh, use an ESP32 node as the bridge and point it at your Art-Net controller.

---

## Features

- Receives Art-Net DMX frames over UDP (port 6454, wired or wireless)
- Drives up to 2 WS2812B / WS2811 LED strips via hardware PWM/DMA ([rpi-ws281x](https://github.com/jgarff/rpi_ws281x))
- Maps Art-Net universes to strips, including strips that span multiple universes
- **HUB75 matrix panels** — drive 64×32 / 64×64 and larger panels via [rpi-rgb-led-matrix](https://github.com/hzeller/rpi-rgb-led-matrix) (optional, `-DENABLE_HUB75=ON`)
- **Resolution multiplying** — FOLD_2X, PARALLEL_2X, PARALLEL_3X (same as ESP32)
- **Pattern engine** — seven built-in animations run automatically when no DMX signal is present
- **Spatial mapping** — multiple nodes share a virtual canvas; patterns render as a coherent image across physical space
- Runtime configuration via a built-in web UI — no recompile needed for most settings
- Config persisted to `/etc/artnode/config.json`
- mDNS hostname (`artnode.local`) via system avahi-daemon
- systemd service unit included
- **Builds on macOS** with stub hardware layers for development and testing

---

## Hardware

- Raspberry Pi 2B / 3B / 3B+ / 4B / 5 / Zero 2 W
- WS2812B strips wired to GPIO 18 (strip 0) and GPIO 13 (strip 1) — see [Wiring](#wiring)
- HUB75 RGB matrix panel wired to the standard 40-pin header (optional)

---

## Quick start

```bash
# 1. Install build dependencies
sudo apt update && sudo apt install -y cmake build-essential git

# 2. Clone and build
git clone https://github.com/dnewcome/artnode.git
cd artnode/artnode-rpi
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# 3. Install and start
sudo cmake --install build
sudo systemctl enable --now artnode-rpi

# 4. Open the web UI
xdg-open http://artnode.local
```

See [INSTALLING.md](INSTALLING.md) for full build options, wiring, and systemd details.

---

## Project structure

```
artnode-rpi/
  CMakeLists.txt            CMake build (fetches dependencies automatically)
  INSTALLING.md             Build, wiring, and deployment guide
  src/
    config.h                Compile-time defaults: GPIO pins, LED counts, HUB75 settings
    runtime_config.h/cpp    JSON-backed runtime config (/etc/artnode/config.json)
    color_utils.h           FastLED-compatible color types (CRGB, CHSV, sin8, …)
    platform.h              millis() shim (clock_gettime CLOCK_MONOTONIC)
    led_controller.h/cpp    rpi-ws281x output, Art-Net → strip mapping, resolution multiplying
    hub75_controller.h/cpp  HUB75 panel output via rpi-rgb-led-matrix (ENABLE_HUB75=ON)
    pattern_engine.h/cpp    Built-in animations — identical logic to ESP32 version
    artnet.h/cpp            Art-Net UDP receiver (POSIX sockets, no library needed)
    web_config.h/cpp        HTTP server (cpp-httplib): config UI and REST API
    main.cpp                Event loop, Art-Net callback, pattern engine, signal handling
  install/
    artnode-rpi.service     systemd unit file
```

---

## Operating modes

The RPi version supports two modes, selectable from the web UI:

| Mode | Art-Net | Patterns | Description |
|---|---|---|---|
| **DIRECT** (default) | yes | idle fallback | Listens for Art-Net. Activates the pattern engine after 5 s of silence. |
| **STANDALONE** | no | always | Runs the pattern engine continuously. No network I/O. |

In DIRECT mode the pattern engine activates automatically whenever the Art-Net signal is absent for more than `IDLE_TIMEOUT_MS` (default 5 s) and stops as soon as frames resume. Nodes are never dark.

---

## Wiring

### LED strips (WS2812B)

rpi-ws281x drives WS2812B via hardware PWM/DMA. Only specific GPIO pins support PWM output:

| Strip | Default GPIO | Board pin | PWM channel |
|-------|-------------|-----------|-------------|
| 0 | 18 | 12 | PWM0 |
| 1 | 13 | 33 | PWM1 |

Alternative PWM pins: GPIO 12 (PWM0, board pin 32), GPIO 19 (PWM1, board pin 35).

To change the default GPIOs, edit the `STRIPS[]` array in `src/config.h` and rebuild.

> **Power:** supply the strips from a dedicated 5 V rail, not from the Pi's 5 V header. Share a ground wire between the Pi and the strip power supply.

### HUB75 panels

The default pin mapping (`"regular"`) targets the standard 40-pin header directly:

| Signal | GPIO (BCM) | Board pin |
|--------|-----------|-----------|
| R1 | 5 | 29 |
| G1 | 13 | 33 |
| B1 | 6 | 31 |
| R2 | 12 | 32 |
| G2 | 16 | 36 |
| B2 | 23 | 16 |
| A | 21 | 40 |
| B | 26 | 37 |
| C | 27 | 13 |
| D | 20 | 38 |
| E | 24 | 18 (64-row panels) |
| CLK | 17 | 11 |
| LAT/STB | 4 | 7 |
| OE | 18 | 12 |

> If you are using an Adafruit RGB Matrix HAT, set `HUB75_MAPPING` to `"adafruit-hat"` in `src/config.h`.

---

## Pattern engine

When no Art-Net signal is present, the pattern engine runs at ~30 fps. The default pattern is **RAINBOW**. Pattern and parameters are configurable from the web UI.

When [spatial mapping](#spatial-mapping) is enabled, patterns address each LED by its position in the shared virtual canvas rather than by strip index — producing a coherent image across physically distributed nodes.

| Pattern | Description | param1 | param2 |
|---|---|---|---|
| `OFF` | All LEDs off | — | — |
| `SOLID` | Single color | hue (0–255) | saturation (0–255) |
| `RAINBOW` | Rainbow sweeping across virtual X axis | speed | — |
| `CHASE` | Dot with tail moving across virtual X | hue | speed |
| `PULSE` | Breathing color (spatial-independent) | hue | speed |
| `TWINKLE` | Random sparkle (spatial-independent) | hue (0=random) | density |
| `PLASMA` | 2D sine interference field; best with spatial mapping | hue offset | speed |

---

## Spatial mapping

Multiple nodes distributed in physical space can share a virtual canvas so patterns appear as a single coherent image across the installation. Each node independently samples the correct slice of the pattern based on its configured position — no per-pixel data is transmitted between nodes.

### How it works

The virtual canvas has configurable pixel dimensions (`virt_w × virt_h`). Each node's strip is placed in the canvas with an origin point and a per-LED step vector:

```
virtual position of LED i:
    vx = origin_x + i × step_x
    vy = origin_y + i × step_y
```

Pattern functions sample the animation at `(vx, vy)` instead of using the raw strip index. All nodes must run the same pattern with the same time counter. On the RPi, time is derived from the system monotonic clock — to keep spatially distributed RPi nodes in sync, ensure the system clocks are aligned (NTP is sufficient for pattern continuity at 30 fps).

Set `virt_w = 0` to disable spatial mapping; patterns fall back to linear strip-index addressing.

### Configuration

Spatial settings are configurable from the web UI under **Spatial mapping** and are persisted to `config.json`.

| Field | Description |
|---|---|
| **Canvas W** | Virtual canvas width. Set to `0` to disable spatial mapping. |
| **Canvas H** | Virtual canvas height. |
| **Origin X / Y** | Position of LED[0] in the virtual canvas. |
| **Step X / Y** | Coordinate advance per LED. Negative values run the strip in reverse. |

### Example — four nodes across a horizontal span

Four RPi nodes, each with 60 LEDs, spread across a 256-unit-wide canvas at mid-height:

| Node | origin_x | origin_y | step_x | step_y |
|------|---------|---------|--------|--------|
| 0 | 0 | 128 | 1.07 | 0 |
| 1 | 64 | 128 | 1.07 | 0 |
| 2 | 128 | 128 | 1.07 | 0 |
| 3 | 192 | 128 | 1.07 | 0 |

`step_x = 256 / (4 × 60) ≈ 1.07` — each LED advances by one sixty-fourth of the node's quarter of the canvas. A RAINBOW or PLASMA pattern running on all four nodes will appear as a single continuous animation.

### Mixing RPi and ESP32 nodes

RPi and ESP32 nodes are spatially compatible. Configure each with the same canvas dimensions and appropriate origin/step values. The only difference is time synchronization: ESP32 bridge nodes propagate a frame counter over ESP-NOW; RPi nodes use NTP. Both produce smooth 30 fps output; slight temporal drift between platforms is invisible in practice.

---

## HUB75 matrix panels

Enable with `-DENABLE_HUB75=ON` at configure time. Panels are driven by [rpi-rgb-led-matrix](https://github.com/hzeller/rpi-rgb-led-matrix) via DMA and run alongside strip output simultaneously.

### Enabling

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_HUB75=ON
```

### Panel dimensions

Set in `src/config.h` before building:

```c
#define HUB75_W              64   // panel pixel width
#define HUB75_H              32   // panel pixel height
#define HUB75_CHAIN          1    // panels daisy-chained
#define HUB75_START_UNIVERSE 0    // first Art-Net universe for panel data
#define HUB75_MAPPING        "regular"   // pin mapping; "adafruit-hat" for HATs
#define HUB75_GPIO_SLOWDOWN  2    // 1=Pi1, 2=Pi2/3/Zero2, 4=Pi4/5
```

### Art-Net mapping

Panel pixels are addressed in row-major order starting at `HUB75_START_UNIVERSE`. Each 512-byte Art-Net universe carries 170 RGB pixels (510 bytes used, 2 padding). A 64×32 panel requires 12 universes (universes 0–11); a 64×64 panel requires 24.

### Audio conflict and flicker

The Pi's built-in audio driver (`snd_bcm2835`) uses the same hardware PWM peripheral as rpi-rgb-led-matrix. If the audio module is loaded, the library falls back to software PWM timing, which introduces visible flicker. The audio module is loaded by default on Raspberry Pi OS.

**Quick fix — disable hardware pulsing in code (already the default in this project):**

`src/config.h` has `options.disable_hardware_pulsing = true` set in `hub75_controller.cpp`, which allows the library to start without the audio conflict. Flicker will be present but the panel will work.

**Full fix — disable the Pi audio module:**

```bash
# Add to /boot/config.txt (or /boot/firmware/config.txt on Pi 5 / Bookworm):
echo "dtparam=audio=off" | sudo tee -a /boot/config.txt
sudo reboot
```

After rebooting, set `options.disable_hardware_pulsing = false` in `src/hub75_controller.cpp` and rebuild. Hardware PWM will be used and the flicker will be gone. You can still use USB audio adapters — only the built-in 3.5 mm jack is affected.

### Pattern engine on panels

When HUB75 is enabled, the spatial config is automatically set to **panel mode**: pixel `i` maps to column `i % panel_w`, row `i / panel_w`. All patterns use true 2D coordinates — PLASMA is particularly effective on a full panel.

---

## Resolution multiplying

Mounting multiple strips at sub-pitch offsets increases effective spatial density without changing the LED hardware. Configured per-strip in `src/config.h`.

### FOLD_2X — single strip folded back on itself

The strip physically runs forward then back alongside itself. Virtual pixels interleave across the fold:

```
virtual:  0  1  2  3  4  5  6  7
          ↓  ↓  ↓  ↓  ↓  ↓  ↓  ↓
physical: 0  7  1  6  2  5  3  4   (2× density)
```

```c
{ .gpio=18, .num_leds=120, .start_universe=0, .channel_offset=0,
  .res_mode=ResMode::FOLD_2X }
```

### PARALLEL_2X — two strips at ½-pitch offset

Two strips side by side, the second offset by half the LED pitch. Art-Net sends `num_leds × 2` virtual pixels: even → primary strip, odd → secondary.

```c
{ .gpio=18, .num_leds=60, .start_universe=0, .channel_offset=0,
  .res_mode=ResMode::PARALLEL_2X, .res_partner={1, 0xFF} },  // primary
{ .gpio=13, .num_leds=60, .start_universe=0, .channel_offset=0,
  .res_mode=ResMode::SECONDARY },                             // secondary
```

### PARALLEL_3X — three strips at ⅓-pitch offset

Three strips. Art-Net sends `num_leds × 3` virtual pixels; distributed by `v % 3`.

> **Note:** the rpi-ws281x library supports at most 2 hardware PWM channels. PARALLEL_3X requires a third strip, which would need an SPI-based APA102 strip on the secondary channels. This mode is included for config compatibility with the ESP32 project but requires custom `led_controller` adaptations to add APA102 support.

### Constraints

- `num_leds` is the **physical** LED count per strip.
- Art-Net uses the **virtual** count: `num_leds × factor`.
- Virtual count must fit in `MAX_LEDS_PER_STRIP` (512): PARALLEL_2X → `num_leds ≤ 256`.
- SECONDARY strips cannot be independently addressed.

---

## Art-Net mapping

One Art-Net universe carries 512 DMX channels. Each RGB LED consumes 3 channels — one universe holds up to 170 LEDs.

Each strip is defined by **start_universe** (which universe it begins in) and **channel_offset** (the first byte within that universe).

**Two strips, one universe each:**
```c
{ .gpio=18, .num_leds=60, .start_universe=0, .channel_offset=0 }
{ .gpio=13, .num_leds=60, .start_universe=1, .channel_offset=0 }
```

**Two strips sharing a single universe:**
```c
{ .gpio=18, .num_leds=60, .start_universe=0, .channel_offset=0   }  // ch 0–179
{ .gpio=13, .num_leds=60, .start_universe=0, .channel_offset=180 }  // ch 180–359
```

**One long strip spanning two universes:**
```c
{ .gpio=18, .num_leds=300, .start_universe=0, .channel_offset=0 }
// Universe 0: LEDs 0–169
// Universe 1: LEDs 170–299
```

---

## Web UI

Open `http://artnode.local` or `http://<ip>` in a browser.

- **Status bar** — IP address, uptime, Art-Net frame count
- **Hostname** — change the mDNS name (takes effect after save + avahi refresh)
- **Node mode** — DIRECT or STANDALONE
- **Brightness** — global output scale (0–255)
- **Strips** — per-strip LED count, Art-Net universe, channel offset; GPIO shown read-only
- **Spatial mapping** — virtual canvas dimensions, strip origin, step vector
- **Save** — persists to `/etc/artnode/config.json` and applies immediately (no reboot)

---

## REST API

The same JSON API used by the web UI, accessible directly:

**`GET /api/config`**
```json
{
  "hostname": "artnode",
  "brightness": 200,
  "node_mode": 2,
  "strips": [
    { "gpio": 18, "num_leds": 60, "start_universe": 0, "channel_offset": 0 },
    { "gpio": 13, "num_leds": 60, "start_universe": 1, "channel_offset": 0 }
  ],
  "spatial": {
    "virt_w": 0,
    "virt_h": 256,
    "origin_x": 0.0,
    "origin_y": 128.0,
    "step_x": 1.0,
    "step_y": 0.0
  }
}
```

`node_mode`: `2` = DIRECT, `4` = STANDALONE. (Values are numerically compatible with the ESP32 project.)

**`POST /api/config`** — write and persist config; responds `ok` on success.

**`GET /api/status`**
```json
{
  "ip": "192.168.1.55",
  "uptime_s": 3600,
  "frames": 108000
}
```

---

## Configuration reference

### Compile-time (`src/config.h`)

| Setting | Description |
|---|---|
| `NUM_STRIPS` | Number of LED strips (max 2 with rpi-ws281x PWM) |
| `STRIPS[]` | Per-strip: GPIO pin, LED count, starting universe, channel offset, resolution mode |
| `IDLE_TIMEOUT_MS` | Seconds without DMX before pattern engine activates (default 5000) |
| `DEFAULT_SPATIAL` | Default spatial config (disabled by default; `virt_w=0`) |
| `ENABLE_HUB75` | Set to `1` to compile in HUB75 support (or pass `-DENABLE_HUB75=ON`) |
| `HUB75_W / H / CHAIN` | Panel pixel dimensions and chain length |
| `HUB75_START_UNIVERSE` | First Art-Net universe for panel pixel data |
| `HUB75_MAPPING` | rpi-rgb-led-matrix hardware mapping (`"regular"`, `"adafruit-hat"`, …) |
| `HUB75_GPIO_SLOWDOWN` | GPIO timing slowdown for your Pi model (1–4) |

### Runtime (`/etc/artnode/config.json` / web UI)

| Setting | Description |
|---|---|
| `hostname` | mDNS hostname (accessible as `hostname.local`) |
| `brightness` | Global LED brightness (0–255) |
| `node_mode` | `2`=DIRECT, `4`=STANDALONE |
| `strips[i].num_leds` | Physical LED count for strip i |
| `strips[i].start_universe` | First Art-Net universe for strip i |
| `strips[i].channel_offset` | Byte offset within the starting universe |
| `spatial.virt_w/h` | Virtual canvas dimensions (set `virt_w=0` to disable) |
| `spatial.origin_x/y` | Position of LED[0] in the virtual canvas |
| `spatial.step_x/y` | Coordinate advance per LED |

---

## Dependencies

All fetched automatically by CMake on first build.

| Library | Purpose |
|---|---|
| [rpi_ws281x](https://github.com/jgarff/rpi_ws281x) | WS2812B LED strip output via PWM/DMA |
| [rpi-rgb-led-matrix](https://github.com/hzeller/rpi-rgb-led-matrix) | HUB75 RGB matrix panel output (optional) |
| [cpp-httplib](https://github.com/yhirose/cpp-httplib) | Embedded HTTP server |
| [nlohmann/json](https://github.com/nlohmann/json) | Config file serialization |

System: `cmake`, `build-essential`, `libssl-dev` (for cpp-httplib TLS headers, even though TLS is not used).

---

## Differences from the ESP32 version

| Feature | ESP32 | RPi |
|---|---|---|
| LED strip library | FastLED | rpi-ws281x |
| HUB75 library | ESP32-HUB75-MatrixPanel-DMA | rpi-rgb-led-matrix |
| Max LED strips | Compile-time, any pin | 2 (hardware PWM) |
| Mesh | ESP-NOW | Not supported |
| Node modes | AUTO / BRIDGE / DIRECT / MESH / STANDALONE | DIRECT / STANDALONE |
| Config storage | NVS (non-volatile storage) | `/etc/artnode/config.json` |
| Build system | PlatformIO | CMake |
| Updates | OTA over WiFi | `cmake --install` + systemd restart |
| Boot time | ~2 s | ~15–30 s (Linux boot) |
