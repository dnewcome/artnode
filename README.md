# artnode

ESP32 firmware that receives [Art-Net](https://art-net.org.uk/) (DMX over UDP) and drives WS2812B/APA102 LED strips via FastLED. Designed as a general-purpose wireless lighting node for art sculptures and installations.

Nodes can operate standalone, as a mesh bridge, or as a pure mesh slave — no controller required, and no need to maintain an IP connection to every node.

## Features

- Receives Art-Net DMX frames over WiFi (UDP port 6454)
- Drives multiple LED strips via FastLED
- Maps Art-Net universes to strips, including strips that span multiple universes
- **ESP-NOW mesh** — bridge nodes rebroadcast universe data to slave nodes with no router or IP management
- **Five operating modes** — AUTO, BRIDGE, DIRECT, MESH, STANDALONE (see below)
- **Pattern engine** — built-in animations run automatically when no DMX signal is present
- **Spatial mapping** — multiple nodes spread in physical space share a virtual canvas; patterns render as a single coherent image across the installation
- **HUB75 matrix panels** — drive 64×32 / 64×64 RGB matrix panels via DMA alongside (or instead of) LED strips
- Runtime configuration via a built-in web UI — no reflash needed for most changes
- OTA firmware updates over the network
- mDNS hostname (`artnode.local` by default)
- Status LED with blink patterns for WiFi state and DMX activity

## Hardware

- ESP32 development board (tested on standard 38-pin devkit)
- WS2812B (or compatible) LED strips on GPIO 16 and GPIO 17 by default
- Status LED on GPIO 2 (built-in LED on most devkits)

Any ESP32 board works. Pin assignments for LED strips are set at compile time due to a FastLED limitation (see [Configuration](#configuration)).

## Project Structure

```
src/
  config.h                Compile-time defaults: pins, strip layout, LED type, mesh channel, HUB75, spatial
  runtime_config.h/cpp    NVS-backed runtime config: WiFi, brightness, node mode, strip layout, spatial position
  led_controller.h/cpp    FastLED output, Art-Net universe → strip mapping, resolution multiplying
  hub75_controller.h/cpp  HUB75 matrix panel output via DMA (compiled in when ENABLE_HUB75=1)
  espnow_mesh.h/cpp       ESP-NOW transport: DMX broadcast/reassembly, pattern sync packets
  pattern_engine.h/cpp    Local animations: off, solid, rainbow, chase, pulse, twinkle, plasma
  status_led.h/cpp        Non-blocking status LED blink state machine
  web_config.h/cpp        HTTP server: config UI and REST API
  main.cpp                Mode-based init, Art-Net callback, main loop
platformio.ini            Build environments (USB and OTA)
```

## Operating Modes

The node mode is set in the web UI or via the REST API and persists across reboots.

| Mode | WiFi | ESP-NOW | Art-Net | Description |
|---|---|---|---|---|
| **AUTO** | tries | yes | yes | Default. Connects to WiFi if available → BRIDGE. On timeout → MESH slave. |
| **BRIDGE** | required | sends | yes | Receives Art-Net and rebroadcasts to mesh via ESP-NOW. |
| **DIRECT** | required | no | yes | WiFi only. Original behavior, no mesh involvement. |
| **MESH** | no | receives | no | ESP-NOW slave. No WiFi; listens for broadcasts from a bridge node. |
| **STANDALONE** | no | no | no | Runs local pattern engine continuously. No network. |

### Typical deployment

```
[Art-Net controller] ──WiFi──▶ [Bridge node] ──ESP-NOW broadcast──▶ [Mesh node]
                                     │                               [Mesh node]
                                     └── also drives its own strips  [Mesh node]
```

Any node can be a bridge. Multiple bridges are fine. Slave nodes do not need to know bridge MAC addresses — the bridge broadcasts, everyone in range listens.

### Idle fallback

In all WiFi-connected modes (AUTO/BRIDGE/DIRECT), if no DMX frames arrive for 5 seconds the pattern engine activates automatically. When frames resume, it stops. Nodes are never dark.

## ESP-NOW Mesh

Art-Net universes are 512 bytes; ESP-NOW packets are capped at 250 bytes. The bridge fragments each universe into up to three 240-byte packets and slaves reassemble them before passing to the LED controller. Single-fragment sends (strips ≤ 80 LEDs per universe) take the fast path with no reassembly overhead.

In addition to DMX frames, the bridge broadcasts a small **pattern sync packet** every 500 ms when the pattern engine is active. This packet carries the current pattern, parameters, and a frame counter so all nodes stay temporally aligned and spatial patterns remain coherent across the installation.

**Channel configuration:** ESP-NOW must operate on the same WiFi channel as the AP when in BRIDGE mode. Set `MESH_CHANNEL` in `config.h` to match your router's channel:

```bash
# Find your AP's channel (on the machine connected to the router):
iw dev wlan0 info | grep channel
```

MESH-only nodes (no WiFi) set their radio channel to `MESH_CHANNEL` at boot.

## Pattern Engine

When no DMX signal is present, the pattern engine runs locally at ~30 fps. The default pattern is **RAINBOW**. In BRIDGE mode, the bridge also broadcasts pattern state over ESP-NOW so all mesh slaves run the same animation in sync.

When [spatial mapping](#spatial-mapping) is configured, patterns address each LED by its position in the shared virtual canvas rather than by strip index, producing a single coherent image across physically distributed nodes.

| Pattern | Description | param1 | param2 |
|---|---|---|---|
| `OFF` | All LEDs off | — | — |
| `SOLID` | Single color | hue (0–255) | saturation (0–255) |
| `RAINBOW` | Moving rainbow sweeping across virtual X axis | speed | — |
| `CHASE` | Dot with tail moving across virtual X axis | hue | speed |
| `PULSE` | Breathing color (spatial-independent) | hue | speed |
| `TWINKLE` | Random sparkle (spatial-independent) | hue (0=random) | density |
| `PLASMA` | 2D sine interference field; best with spatial mapping | hue offset | speed |

## Spatial Mapping

Multiple nodes spread in physical space can be configured to share a virtual canvas so that patterns render as a single image across the installation. Each node knows its position in the canvas and independently samples the correct slice of the pattern — no per-pixel data is transmitted.

### How it works

The virtual canvas has configurable dimensions (`virt_w × virt_h`, in arbitrary units). Each node's strip is placed in the canvas by an origin point and a per-LED step vector:

```
virtual LED position for LED i:
    vx = origin_x + i × step_x
    vy = origin_y + i × step_y
```

Pattern functions use `vx` (and `vy` for 2D patterns like PLASMA) instead of the raw strip index. All nodes run the same pattern with the same time counter (kept in sync by the bridge), so the result is a spatially and temporally coherent display.

When `virt_w = 0` spatial mapping is disabled and patterns fall back to linear strip-index addressing.

### Configuration

Spatial settings are persisted to NVS and configurable from the web UI under **Spatial mapping**.

| Field | Description |
|---|---|
| **Canvas W** | Virtual canvas width (`virt_w`). Set to `0` to disable. |
| **Canvas H** | Virtual canvas height (`virt_h`). |
| **Origin X / Y** | Position of LED[0] in the virtual canvas. |
| **Step X / Y** | Virtual coordinate advance per LED. Negative values run the strip backwards. |

### Example — four nodes across a horizontal installation

Four nodes, each with 60 LEDs, evenly distributed across a 256-unit-wide canvas at mid-height:

| Node | origin_x | origin_y | step_x | step_y |
|---|---|---|---|---|
| 0 | 0 | 128 | 1.07 | 0 |
| 1 | 64 | 128 | 1.07 | 0 |
| 2 | 128 | 128 | 1.07 | 0 |
| 3 | 192 | 128 | 1.07 | 0 |

`step_x = 256 / (4 × 60) = 1.07` — each LED advances by one sixty-fourth of its quarter of the canvas.

A RAINBOW or PLASMA pattern running on all four nodes with this config will appear as a single continuous animation across the physical space.

### Vertical and diagonal strips

A strip running vertically in the canvas:
```
origin_x=128, origin_y=0, step_x=0, step_y=4.27
```

A strip running at 45°:
```
origin_x=0, origin_y=0, step_x=2.0, step_y=2.0
```

## HUB75 RGB Matrix Panels

In addition to LED strips, artnode can drive HUB75 RGB matrix panels (e.g. 64×32 or 64×64 scoreboards) using the [ESP32-HUB75-MatrixPanel-DMA](https://github.com/mrfaptastic/ESP32-HUB75-MatrixPanel-DMA) library. HUB75 output works alongside strip output — both can be active simultaneously.

### Enabling HUB75

Set `ENABLE_HUB75` to `1` in `config.h` and configure the panel dimensions:

```c
#define ENABLE_HUB75        1
#define HUB75_W             64   // panel pixel width
#define HUB75_H             32   // panel pixel height
#define HUB75_CHAIN         1    // panels daisy-chained
#define HUB75_START_UNIVERSE 0   // first Art-Net universe for panel data
```

### Pin wiring

HUB75 uses a dedicated parallel interface. Default pin mapping (matches most HUB75 breakout boards and the ESP32 Trinity):

| Signal | GPIO |
|---|---|
| R1, G1, B1 | 25, 26, 27 |
| R2, G2, B2 | 14, 12, 13 |
| A, B, C, D | 23, 22, 5, 17 |
| E (64-row panels only) | 18 |
| CLK, LAT, OE | 16, 4, 15 |

> **Pin conflict:** default CLK (16) and D (17) overlap with the default strip pins. Either rewire your strips or set `NUM_STRIPS 0` when using a panel as the primary output. Override pins in `hub75_controller.cpp` inside `begin()` via the `mxconfig.gpio` struct.

### Art-Net mapping

Panel pixels are addressed in row-major order from `HUB75_START_UNIVERSE`. Each 512-byte universe carries 170 pixels (170 × 3 = 510 bytes). A 64×32 panel = 2048 pixels = 12 universes (0–11).

### Pattern engine

When HUB75 is enabled, the spatial config is automatically set to **panel mode**: pixel `i` maps to column `i % HUB75_W`, row `i / HUB75_W`. All patterns use true 2D coordinates, which makes PLASMA especially effective.

The bridge also broadcasts pattern sync packets so mesh slave nodes running panels stay temporally aligned.

## Building and Flashing

This project uses [PlatformIO](https://platformio.org/).

**First flash (USB):**
```bash
pio run -t upload -e esp32dev
```

**Subsequent updates over WiFi (OTA):**
```bash
pio run -t upload -e esp32dev-ota
```

The OTA environment targets `artnode.local` by default. Change `upload_port` in `platformio.ini` if you use a different hostname or prefer to target by IP address.

**Serial monitor:**
```bash
pio device monitor
```

## Configuration

### Compile-time settings (`src/config.h`)

These must be set before the first flash. Most can be overridden at runtime afterward via the web UI.

| Setting | Description |
|---|---|
| `WIFI_SSID` / `WIFI_PASSWORD` | WiFi credentials (fallback if NVS is empty) |
| `NUM_STRIPS` | Number of LED strips (also controls array sizes) |
| `STRIPS[]` | Per-strip: data pin, LED count, starting universe, channel offset |
| `LED_TYPE` | FastLED chip type, e.g. `WS2812B`, `APA102` |
| `COLOR_ORDER` | e.g. `GRB` for WS2812B, `BGR` for APA102 |
| `MESH_CHANNEL` | WiFi channel for ESP-NOW (must match AP channel in BRIDGE mode) |
| `WIFI_TIMEOUT_MS` | How long AUTO mode waits for WiFi before falling back (default 10s) |
| `IDLE_TIMEOUT_MS` | Seconds without DMX before pattern engine activates (default 5s) |
| `DEFAULT_SPATIAL` | Default spatial config (disabled by default; `virt_w=0`) |
| `ENABLE_HUB75` | Set to `1` to enable HUB75 panel output (default `0`) |
| `HUB75_W` / `HUB75_H` | Panel pixel dimensions (default 64×32) |
| `HUB75_CHAIN` | Number of panels daisy-chained (default 1) |
| `HUB75_START_UNIVERSE` | First Art-Net universe for panel pixel data (default 0) |

**Strip pin numbers are compile-time only** — this is a FastLED constraint where the data pin is a C++ template parameter. If you change strip pins, update both `config.h` and the `FastLED.addLeds<>` calls in `led_controller.cpp`, then reflash.

### Runtime settings (web UI / NVS)

Once the node is on the network, open `http://artnode.local` in a browser:

- Node mode (AUTO / BRIDGE / DIRECT / MESH / STANDALONE)
- WiFi SSID, password, hostname
- Brightness (0–255)
- Per-strip: LED count, starting universe, channel offset
- Spatial mapping: canvas dimensions, strip origin, and step vector

Settings are saved to ESP32 NVS (non-volatile storage) and survive reboots. Saving via the web UI triggers an automatic reboot to apply changes.

## Resolution Multiplying

Placing multiple strips (or a single folded strip) at sub-pitch offsets increases effective spatial density without changing the LED hardware. Three modes are supported, configured per-strip in `config.h`.

### FOLD_2X — single strip folded back on itself

The strip runs forward to the end, then folds back and runs alongside itself in the opposite direction. LEDs interleave: the first pass covers even physical positions, the return pass covers odd positions.

Art-Net addresses all `num_leds` virtual pixels linearly. The remap interleaves them across the fold:

```
virtual: 0  1  2  3  4  5  6  7
         ↓  ↓  ↓  ↓  ↓  ↓  ↓  ↓
physical:0  7  1  6  2  5  3  4   ← positions (2× density)
```

Config (one strip, 120 LEDs, folded to 60 physical positions):
```c
{ .pin=16, .num_leds=120, .start_universe=0, .channel_offset=0,
  .res_mode=ResMode::FOLD_2X }
```

### PARALLEL_2X — two strips at ½-pitch offset

Two physical strips are mounted side by side, the second offset by half the LED pitch. Art-Net sends `num_leds × 2` virtual pixels. Even pixels go to the primary strip, odd pixels go to the secondary.

```
virtual: 0  1  2  3  4  5  6  7
         ↓     ↓     ↓     ↓        → strip A (pin 16)
            ↓     ↓     ↓     ↓     → strip B (pin 17)
```

Config:
```c
// Primary — receives Art-Net, drives even virtual pixels to its own LEDs
{ .pin=16, .num_leds=60, .start_universe=0, .channel_offset=0,
  .res_mode=ResMode::PARALLEL_2X, .res_partner={1, 0xFF} },
// Secondary — populated by primary's remap; no universe assignment needed
{ .pin=17, .num_leds=60, .start_universe=0, .channel_offset=0,
  .res_mode=ResMode::SECONDARY },
```

### PARALLEL_3X — three strips at ⅓-pitch offset

Same idea with three strips. Art-Net sends `num_leds × 3` virtual pixels. Pixels are distributed by `v % 3`: primary / partner[0] / partner[1].

Config:
```c
{ .pin=16, .num_leds=40, .start_universe=0, .channel_offset=0,
  .res_mode=ResMode::PARALLEL_3X, .res_partner={1, 2} },
{ .pin=17, .num_leds=40, .start_universe=0, .channel_offset=0,
  .res_mode=ResMode::SECONDARY },
{ .pin=18, .num_leds=40, .start_universe=0, .channel_offset=0,
  .res_mode=ResMode::SECONDARY },
```

### Constraints

- `num_leds` is the **physical** count per strip.
- Art-Net addressing uses the **virtual** count (`num_leds × factor`).
- Virtual count must fit in `MAX_LEDS_PER_STRIP` (512): PARALLEL_2X → `num_leds ≤ 256`, PARALLEL_3X → `num_leds ≤ 170`.
- SECONDARY strips cannot be independently addressed or animated while a primary is active.

## Art-Net Mapping

Art-Net universes contain 512 DMX channels. Each RGB LED consumes 3 channels, so one universe holds up to 170 LEDs.

Each strip is defined by:
- **start_universe** — which Art-Net universe the strip begins in
- **channel_offset** — first channel byte within that universe (0-based)

**Example: two 60-LED strips, each on its own universe**
```c
{ .pin = 16, .num_leds = 60, .start_universe = 0, .channel_offset = 0 }
{ .pin = 17, .num_leds = 60, .start_universe = 1, .channel_offset = 0 }
```

**Example: sharing a universe between two strips**
```c
{ .pin = 16, .num_leds = 60, .start_universe = 0, .channel_offset = 0   }  // channels 0–179
{ .pin = 17, .num_leds = 60, .start_universe = 0, .channel_offset = 180 }  // channels 180–359
```

**Example: one long strip spanning two universes**
```c
{ .pin = 16, .num_leds = 300, .start_universe = 0, .channel_offset = 0 }
// Universe 0: LEDs 0–169 (channels 0–509)
// Universe 1: LEDs 170–299 (channels 0–389)
```

Multi-universe strips are handled automatically — the controller stitches incoming universe frames together into the correct LED positions.

## Status LED

The built-in LED on GPIO 2 indicates node state:

| Pattern | State |
|---|---|
| Fast blink (100ms) | Connecting to WiFi |
| Single flash every 3 seconds | Connected, idle |
| Brief 30ms flash | DMX frame received |

## Web UI

Served at `http://artnode.local` (or the node's IP address).

- Live status: IP address, WiFi signal strength, uptime, total DMX frames received
- Node mode selector
- Edit WiFi credentials and hostname
- Adjust brightness
- Edit per-strip LED count, universe, and channel offset
- Configure spatial mapping: virtual canvas size, strip origin, and step vector per LED
- Save triggers a reboot; changes take effect on next boot

## REST API

The web UI is backed by a small JSON API that can also be used directly:

**GET `/api/config`** — returns current configuration
```json
{
  "ssid": "mynetwork",
  "hostname": "artnode",
  "brightness": 200,
  "node_mode": 0,
  "strips": [
    { "pin": 16, "num_leds": 60, "start_universe": 0, "channel_offset": 0 },
    { "pin": 17, "num_leds": 60, "start_universe": 1, "channel_offset": 0 }
  ],
  "spatial": {
    "virt_w": 256,
    "virt_h": 256,
    "origin_x": 0.0,
    "origin_y": 128.0,
    "step_x": 1.07,
    "step_y": 0.0,
    "panel_w": 0
  }
}
```

`node_mode` values: `0`=AUTO, `1`=BRIDGE, `2`=DIRECT, `3`=MESH, `4`=STANDALONE

Set `spatial.virt_w = 0` to disable spatial mapping. `panel_w` is set automatically when `ENABLE_HUB75` is active and does not need to be configured manually.

**POST `/api/config`** — save config and reboot (password field is write-only; omit to keep existing)

**GET `/api/status`** — returns live node status
```json
{
  "ip": "192.168.1.42",
  "rssi": -58,
  "uptime_s": 3721,
  "frames": 108432
}
```

## Dependencies

| Library | Purpose |
|---|---|
| [FastLED](https://github.com/FastLED/FastLED) | LED strip output |
| [ArtnetWifi](https://github.com/rstephan/ArtnetWifi) | Art-Net UDP receiver |
| [ArduinoJson](https://arduinojson.org/) | JSON config serialization |
| [ESP32-HUB75-MatrixPanel-DMA](https://github.com/mrfaptastic/ESP32-HUB75-MatrixPanel-DMA) | HUB75 RGB matrix panel output (optional) |
| ESP32 Arduino core (built-in) | WiFi, WebServer, mDNS, OTA, NVS Preferences, ESP-NOW |

## Adding More Strips

1. Increment `NUM_STRIPS` in `config.h`
2. Add an entry to the `STRIPS[]` array in `config.h` with the new pin and defaults
3. Add a corresponding `FastLED.addLeds<>` call in `led_controller.cpp::begin()`
4. Reflash via USB

After the first flash, LED count, universe, and channel offset for the new strip can be adjusted from the web UI without reflashing.
