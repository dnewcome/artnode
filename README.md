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
  config.h               Compile-time defaults: pins, strip layout, LED type, mesh channel
  runtime_config.h/cpp   NVS-backed runtime config: WiFi, brightness, node mode, strip layout
  led_controller.h/cpp   FastLED output, Art-Net universe → strip mapping
  espnow_mesh.h/cpp      ESP-NOW transport: broadcast, fragment, reassemble
  pattern_engine.h/cpp   Local animations: rainbow, chase, pulse, twinkle, solid
  status_led.h/cpp       Non-blocking status LED blink state machine
  web_config.h/cpp       HTTP server: config UI and REST API
  main.cpp               Mode-based init, Art-Net callback, main loop
platformio.ini           Build environments (USB and OTA)
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

**Channel configuration:** ESP-NOW must operate on the same WiFi channel as the AP when in BRIDGE mode. Set `MESH_CHANNEL` in `config.h` to match your router's channel:

```bash
# Find your AP's channel (on the machine connected to the router):
iw dev wlan0 info | grep channel
```

MESH-only nodes (no WiFi) set their radio channel to `MESH_CHANNEL` at boot.

## Pattern Engine

When no DMX signal is present, the pattern engine runs locally. The default pattern is **RAINBOW**. Patterns are selected programmatically via `patterns.setPattern()` in `main.cpp` — configurable via the REST API in a future update.

| Pattern | Description | param1 | param2 |
|---|---|---|---|
| `OFF` | All LEDs off | — | — |
| `SOLID` | Single color | hue (0–255) | saturation (0–255) |
| `RAINBOW` | Moving rainbow | speed | — |
| `CHASE` | Dot with tail | hue | speed |
| `PULSE` | Breathing color | hue | speed |
| `TWINKLE` | Random sparkle | hue (0=random) | density |

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

**Strip pin numbers are compile-time only** — this is a FastLED constraint where the data pin is a C++ template parameter. If you change strip pins, update both `config.h` and the `FastLED.addLeds<>` calls in `led_controller.cpp`, then reflash.

### Runtime settings (web UI / NVS)

Once the node is on the network, open `http://artnode.local` in a browser:

- Node mode (AUTO / BRIDGE / DIRECT / MESH / STANDALONE)
- WiFi SSID, password, hostname
- Brightness (0–255)
- Per-strip: LED count, starting universe, channel offset

Settings are saved to ESP32 NVS (non-volatile storage) and survive reboots. Saving via the web UI triggers an automatic reboot to apply changes.

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
  ]
}
```

`node_mode` values: `0`=AUTO, `1`=BRIDGE, `2`=DIRECT, `3`=MESH, `4`=STANDALONE

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
| ESP32 Arduino core (built-in) | WiFi, WebServer, mDNS, OTA, NVS Preferences, ESP-NOW |

## Adding More Strips

1. Increment `NUM_STRIPS` in `config.h`
2. Add an entry to the `STRIPS[]` array in `config.h` with the new pin and defaults
3. Add a corresponding `FastLED.addLeds<>` call in `led_controller.cpp::begin()`
4. Reflash via USB

After the first flash, LED count, universe, and channel offset for the new strip can be adjusted from the web UI without reflashing.
