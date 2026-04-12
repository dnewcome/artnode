# artnode-rpi — Installation Guide

## Requirements

**Hardware**
- Raspberry Pi 2B / 3B / 3B+ / 4B / 5 / Zero 2 W
- WS2812B or APA102 LED strips wired to hardware PWM GPIO pins (see [Wiring](#wiring))
- HUB75 RGB matrix panel (optional, see [HUB75](#hub75-panels))

**Software**
- Raspberry Pi OS Bookworm (64-bit recommended) or any Debian 12+ derivative
- CMake 3.18+
- GCC or Clang with C++17 support

---

## System dependencies

```bash
sudo apt update
sudo apt install -y cmake build-essential git libssl-dev
```

---

## Build

Clone the repo (if you haven't already) and enter the subproject:

```bash
git clone https://github.com/dnewcome/artnode.git
cd artnode/artnode-rpi
```

Configure and build:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

The first configure step fetches dependencies automatically via CMake FetchContent:
- [rpi_ws281x](https://github.com/jgarff/rpi_ws281x) — WS2812B PWM/DMA driver
- [cpp-httplib](https://github.com/yhirose/cpp-httplib) — embedded HTTP server
- [nlohmann/json](https://github.com/nlohmann/json) — config file serialization

A working internet connection is required on the first build.

### HUB75 panels

To enable HUB75 RGB matrix panel support:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_HUB75=ON
cmake --build build -j$(nproc)
```

This additionally fetches [rpi-rgb-led-matrix](https://github.com/hzeller/rpi-rgb-led-matrix).

---

## Install

```bash
sudo cmake --install build
```

This copies:
- `/usr/local/bin/artnode-rpi` — the binary
- `/etc/systemd/system/artnode-rpi.service` — the systemd unit

---

## Wiring

### LED strips (WS2812B)

rpi-ws281x drives strips via hardware PWM, which requires specific GPIO pins
(BCM numbering):

| Strip | GPIO | Board pin | PWM channel |
|-------|------|-----------|-------------|
| 0     | 18   | 12        | PWM0        |
| 1     | 13   | 33        | PWM1        |

These are the compile-time defaults in `src/config.h`. To use different pins,
edit `STRIPS[]` in `config.h` and rebuild. Valid alternatives:
- PWM0: GPIO 12 (board pin 32) or GPIO 18 (board pin 12)
- PWM1: GPIO 13 (board pin 33) or GPIO 19 (board pin 35)

> **Power:** Do not power more than a handful of LEDs from the Pi's 5 V header.
> Use a dedicated 5 V supply for the strips and share ground with the Pi.

### HUB75 panels

The default pin mapping (`"regular"`) matches the standard 40-pin header wiring
used by most HUB75 breakout boards. If you are using an Adafruit RGB Matrix HAT
set `HUB75_MAPPING` in `src/config.h` to `"adafruit-hat"` or
`"adafruit-hat-pwm"` before building.

Set `HUB75_GPIO_SLOWDOWN` in `src/config.h` to match your Pi model:

| Pi model          | Suggested slowdown |
|-------------------|--------------------|
| Pi 1 / Zero       | 1                  |
| Pi 2 / 3 / Zero 2 | 2                  |
| Pi 4              | 4                  |
| Pi 5              | 4                  |

---

## Run

The binary requires root (or raw I/O capability) to access the PWM/DMA hardware.

**Run directly:**
```bash
sudo /usr/local/bin/artnode-rpi
```

**Enable and start via systemd:**
```bash
sudo systemctl enable artnode-rpi
sudo systemctl start artnode-rpi
sudo journalctl -u artnode-rpi -f    # follow logs
```

The service starts automatically on boot after the network is online.

---

## Configuration

On first run a default config is used (from `src/config.h`). Open the web UI
in a browser to adjust settings at runtime:

```
http://artnode.local     (mDNS — requires avahi-daemon on the network)
http://<ip-address>
```

The REST API endpoints are:

| Endpoint | Method | Description |
|---|---|---|
| `/` | GET | Web configuration UI |
| `/api/config` | GET | Read current config as JSON |
| `/api/config` | POST | Write and persist config |
| `/api/status` | GET | IP address, uptime, frame count |

Config is persisted to `/etc/artnode/config.json` and survives reboots.

### Key settings

- **Brightness** — global LED output scale (0–255)
- **Node mode** — `DIRECT` (Art-Net + idle patterns) or `STANDALONE` (patterns only)
- **Strips** — LED count, Art-Net universe, and channel offset per strip
- **Spatial mapping** — virtual canvas origin and step for multi-node installations

---

## mDNS (hostname.local)

mDNS is handled by `avahi-daemon`, which is installed by default on Raspberry Pi OS.
If it is not running:

```bash
sudo apt install -y avahi-daemon
sudo systemctl enable --now avahi-daemon
```

The hostname defaults to `artnode`. Change it in the web UI; it takes effect after
avahi picks up the new hostname (usually within a few seconds of saving).

---

## Local / macOS builds (no hardware)

The project builds on macOS automatically in `MOCK_HARDWARE` mode, which stubs
out the LED and panel hardware layers. Everything else — Art-Net receiver,
pattern engine, web config, config persistence — runs normally. Useful for
development and testing without a Pi.

```bash
cmake -B build        # MOCK_HARDWARE=ON detected automatically on macOS
cmake --build build -j$(nproc)
./build/artnode-rpi
```

Config is written to `/tmp/artnode/config.json` in mock mode.

To force mock mode on Linux:

```bash
cmake -B build -DMOCK_HARDWARE=ON
```

---

## Updating

```bash
cd artnode/artnode-rpi
git pull
cmake --build build -j$(nproc)
sudo cmake --install build
sudo systemctl restart artnode-rpi
```
