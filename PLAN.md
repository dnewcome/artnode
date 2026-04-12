# Raspberry Pi Target: Exploration Plan

## Background

This project currently targets the **ESP32** microcontroller using the Arduino framework via PlatformIO. It implements a WiFi Art-Net receiver with WS2812B/APA102 LED strip control, an ESP-NOW mesh, HUB75 matrix panel support, a local pattern engine, and a web configuration UI.

The Raspberry Pi (RPi) is a full Linux SBC (single-board computer) — a fundamentally different class of hardware from the ESP32. This document explores the trade-offs, challenges, and strategic options for adding RPi support.

---

## Why Consider Raspberry Pi?

**Capabilities the ESP32 lacks:**

| Capability | ESP32 | Raspberry Pi |
|---|---|---|
| CPU cores | 2 (240 MHz, Xtensa LX7) | 4 (1.5–2.4 GHz, ARM Cortex-A) |
| RAM | 520 KB SRAM | 512 MB – 8 GB |
| Storage | 4–16 MB flash | microSD / SSD (GB–TB) |
| OS | FreeRTOS / bare Arduino | Full Linux (Raspberry Pi OS, etc.) |
| Networking | WiFi + BT (single radio) | WiFi + Ethernet (dual) |
| USB | Limited (host on some variants) | Full USB host |
| Display output | None (SPI/I2C only) | HDMI, DSI |
| Video/audio | None | H.265 decode, audio jack, HDMI audio |

**Use cases where RPi wins:**
- Very large LED installations (thousands of LEDs, many universes)
- Installations needing persistent logging, metrics, or dashboards
- Driving multiple HUB75 panels with complex video content
- Running a central Art-Net bridge/splitter that needs to survive complex network topologies
- Offline playback from SD card or USB (pre-recorded shows)
- Installations in venues with reliable power (no battery concern)

---

## Pros and Cons

### Pros of Adding Raspberry Pi Support

- **More computational headroom** — The pattern engine (currently ~30 FPS at 512 LEDs) could scale to thousands of LEDs without frame-rate degradation.
- **rpi-rgb-led-matrix** is the dominant, battle-tested HUB75 library on Linux with DMA support and many panel configurations. Much richer than the ESP32 DMA library.
- **Ethernet** — RPi has wired Ethernet; Art-Net deployments often prefer wired for reliability and to avoid WiFi contention with other radios on a show network.
- **Standard build tooling** — CMake, gcc/clang, apt packages. No PlatformIO or toolchain downloads.
- **Easier development** — Edit and run directly on the device via SSH; no flash cycle.
- **File system** — Config lives in normal JSON/INI files instead of NVS. Easier to inspect and edit.
- **Richer web UI** — A more capable HTTP server (nginx + a small app) is straightforward on Linux.
- **Debugging** — gdb, valgrind, perf, strace. Far easier to diagnose production issues.

### Cons of Adding Raspberry Pi Support

- **Power consumption** — RPi draws ~2–7 W idle vs ESP32's 0.25–0.5 W. Matters for battery-powered art pieces.
- **Boot time** — RPi Linux boots in 10–30+ seconds. ESP32 is ready in under 2 seconds. Installation startup UX degrades significantly.
- **Cost** — RPi Zero 2 W (~$15) is competitive, but RPi 4/5 ($35–$80) is 5–10× the ESP32 cost.
- **Form factor** — RPi is physically larger and requires a microSD card, proper PSU, and case.
- **No ESP-NOW** — The mesh protocol (`espnow_mesh.cpp`) is an Espressif-proprietary radio protocol. It does not exist on any other platform. A mesh on RPi would require a completely different transport (e.g., UDP multicast, Zigbee dongle, or a custom WiFi-based mesh).
- **WS2812B timing is hard on Linux** — The WS2812B protocol requires precise ≤100 ns pulse timing. Linux is not a real-time OS; the kernel can preempt userspace at any moment. This is solvable (rpi-ws281x uses PWM/DMA), but it is a real hardware/software constraint that doesn't exist on the ESP32 which has dedicated RMT peripheral support.
- **Increased complexity** — Two targets means two build systems, two CI paths, two sets of platform-specific code paths, and two devices to test every change on.

---

## Component-by-Component Portability Analysis

### `led_controller` (FastLED → rpi-ws281x / SPI)

**Difficulty: Medium**

FastLED does not support Linux/RPi natively. Options:

1. **rpi-ws281x** — C library using PWM or SPI DMA for WS2812B timing. Has a C API. FastLED logic (color math, virtual buffer, universe mapping) could be retained; only the output call changes.
2. **SPI-mode APA102** — APA102/SK9822 strips work over standard Linux SPI (`/dev/spidev0.0`), making them significantly easier to drive on RPi than WS2812B. No timing constraints.
3. **Adapter layer** — Introduce an abstract `LedOutput` interface that has an ESP32 implementation (FastLED) and an RPi implementation (rpi-ws281x or SPI), keeping `LedController` logic shared.

> **Recommendation:** Prefer APA102 strips on RPi targets for simplicity. For WS2812B, use rpi-ws281x via a thin adapter.

---

### `hub75_controller` (ESP32-HUB75-MatrixPanel-DMA → rpi-rgb-led-matrix)

**Difficulty: Medium-Low**

The HUB75 component would be replaced entirely by **rpi-rgb-led-matrix**, which is the canonical RPi HUB75 library. It supports a far wider range of panel sizes, chaining, and configurations. The public API is very different, but the conceptual mapping (Art-Net universe → pixel row → panel pixel) is the same. The controller class would be a clean rewrite for RPi using rpi-rgb-led-matrix's C++ API.

---

### `espnow_mesh` (ESP-NOW → ?)

**Difficulty: High — fundamental redesign required**

ESP-NOW is a proprietary Espressif radio protocol. There is no RPi equivalent. Options:

1. **UDP multicast** — Use `255.255.255.255` or a multicast group for DMX and pattern sync broadcast. High-bandwidth, LAN-only, but works anywhere.
2. **MQTT** — Broker-based publish/subscribe. Adds infrastructure dependency but enables cross-WAN deployments.
3. **Thread/OpenThread** — IEEE 802.15.4 mesh networking via a USB dongle. Maintains mesh topology but adds hardware.
4. **Hybrid mesh** — RPi acts as a BRIDGE node only (receives Art-Net, broadcasts via ESP-NOW to ESP32 leaves). This preserves the existing mesh for most nodes.

> **Recommendation for hybrid deployment:** An RPi can sit at the top of the existing mesh as a bridge node, translating Art-Net → ESP-NOW. The ESP32 nodes in the mesh remain unchanged. This avoids redesigning the mesh entirely.

---

### `pattern_engine`

**Difficulty: Low**

Pure C++ math with no platform dependencies. Portable as-is. The time sync mechanism (currently uses `esp_timer_get_time()`) would need a shim (e.g., `clock_gettime(CLOCK_MONOTONIC)` on Linux). A thin `platform_time.h` abstraction covers this cleanly.

---

### `runtime_config` (NVS → filesystem)

**Difficulty: Low**

NVS (ESP32 Non-Volatile Storage) has no Linux equivalent, but this is easy: replace with a JSON file in `/etc/artnode/config.json` (or `~/.artnode/config.json`). The `RuntimeConfig` class interface stays the same; only the backend changes.

---

### `web_config` (ESP32 WebServer → ASIO / cpp-httplib / external)

**Difficulty: Low-Medium**

The ESP32 Arduino `WebServer` class has no Linux equivalent. Options:
- **cpp-httplib** — Header-only C++ HTTP library. Nearly identical API, minimal dependencies.
- **Crow / Drogon** — More fully-featured C++ REST frameworks.
- Same HTML/JS frontend can be served without changes.

---

### `status_led`

**Difficulty: Low**

GPIO status LED on RPi can use `/sys/class/gpio` or the `gpiod` library (libgpiod). The state machine logic is fully portable; only the GPIO write call changes.

---

### `main.cpp` (WiFi, mDNS, OTA, Art-Net)

**Difficulty: Medium**

- **WiFi connection management** — On RPi, WiFi is managed by the OS (wpa_supplicant / NetworkManager). The firmware's WiFi connection loop in `setup()` is unnecessary; the application just assumes networking is available.
- **mDNS** — `avahi-daemon` handles this at the OS level. The app can advertise a service via the Avahi D-Bus API or a helper library (libavahi-client).
- **OTA** — Replaced by standard Linux update mechanisms (apt, rsync, systemd service restart).
- **Art-Net (UDP)** — The ArtnetWifi library uses standard Berkeley socket calls underneath. The library itself may port, or can be replaced with a thin UDP listener using POSIX sockets.

---

## Architecture Options for Multi-Platform Support

### Option A: Single Codebase, Compile-Time Platform Abstraction

Introduce a `platform/` directory with ESP32 and RPi implementations of a set of abstract interfaces:

```
src/
  platform/
    esp32/
      led_output_esp32.cpp    ← FastLED
      time_esp32.cpp          ← esp_timer_get_time
      storage_esp32.cpp       ← NVS Preferences
      mesh_esp32.cpp          ← ESP-NOW
    rpi/
      led_output_rpi.cpp      ← rpi-ws281x or SPI
      time_rpi.cpp            ← clock_gettime
      storage_rpi.cpp         ← JSON file
      mesh_rpi.cpp            ← UDP multicast
  led_controller.cpp          ← shared, uses platform/
  pattern_engine.cpp          ← shared, no changes
  hub75_controller.cpp        ← platform-specific (two versions)
  web_config.cpp              ← platform-specific (WebServer vs cpp-httplib)
```

**Build system:** CMake with platform selection (`-DPLATFORM=rpi` vs `-DPLATFORM=esp32`). PlatformIO stays for ESP32; CMake is used for RPi.

**Pros:** Single repo, shared business logic, clear seams.  
**Cons:** Two build systems to maintain; cross-platform CMake + PlatformIO configurations can drift. Every new feature must consider both targets.

---

### Option B: Separate RPi Port in a Subdirectory

Keep `src/` as the ESP32 firmware. Create `rpi/` as an independent CMake project that shares only the pure-logic files (pattern engine, config parsing).

```
artnode/
  src/          ← ESP32 Arduino/PlatformIO (unchanged)
  rpi/
    CMakeLists.txt
    main_rpi.cpp
    led_output_rpi.cpp
    hub75_rpi.cpp
    ...
```

**Pros:** ESP32 side is completely unaffected; RPi port evolves independently.  
**Cons:** Logic duplication risk grows over time; changes to the pattern engine need to be mirrored manually (or symlinked).

---

### Option C: RPi as Bridge Node Only (Hybrid Deployment)

Don't port the full firmware to RPi. Instead, build a small, focused **bridge application** for RPi that:
- Receives Art-Net from the network
- Forwards DMX universes to ESP32 leaf nodes via ESP-NOW (using an ESP32 connected to the RPi over USB/UART as a co-processor)
- Optionally drives HUB75 panels directly via rpi-rgb-led-matrix

This keeps the existing ESP32 mesh intact and adds RPi capability without a full platform port.

**Pros:** Minimal disruption; leverages existing ESP32 mesh nodes; RPi gets its main strengths (HUB75, compute) without needing to replicate the whole stack.  
**Cons:** Requires an ESP32 co-processor USB dongle for mesh forwarding; adds hardware complexity at the RPi end.

---

## Recommended Approach

**Start with Option C (Hybrid Bridge), then evolve toward Option A if warranted.**

Rationale:
1. The ESP-NOW mesh is the hardest component to port and is the most deployment-critical. Keeping it on ESP32 preserves proven reliability.
2. The highest-value RPi capability is HUB75 panel driving. rpi-rgb-led-matrix is definitively better than the ESP32 DMA library for complex/chained panels. This can be added as a focused, isolated effort.
3. A bridge-only RPi binary is small, testable, and deployable quickly — it proves the concept without committing to a full port.
4. Once the bridge is stable, the platform abstraction layer (Option A) can be introduced incrementally as shared logic.

---

## Key Technical Risks

| Risk | Severity | Mitigation |
|---|---|---|
| WS2812B timing jitter on Linux kernel | High | Use rpi-ws281x (DMA/PWM); prefer APA102 on RPi targets |
| ESP-NOW not portable | High | Co-processor approach; or accept UDP multicast as RPi-native mesh |
| Boot time for installations | Medium | Use RPi Zero 2 W with fast SD; `systemd` service with `After=network.target` |
| SD card corruption on power loss | Medium | Read-only root filesystem; config on separate partition with journaling |
| Increased maintenance surface | Medium | Keep shared logic minimal; invest in integration tests |
| FastLED not available on Linux | Low | rpi-ws281x is a clean replacement; APA102 via SPI is trivial |

---

## Suggested First Steps

1. **Proof of concept:** Build a minimal RPi CMake project that receives Art-Net UDP and drives a HUB75 panel via rpi-rgb-led-matrix. Validate timing and frame rates.
2. **Evaluate co-processor approach:** Connect an ESP32 via USB to an RPi and test whether the UART/USB serial bridge for ESP-NOW forwarding is viable at the required throughput.
3. **Abstract platform time:** Add a `platform_time.h` shim to the pattern engine — this is the smallest, lowest-risk first step toward shared code.
4. **Decide on strip type:** If RPi LED strips are needed, decide between WS2812B (rpi-ws281x) and APA102 (SPI) early to avoid duplicating work.
5. **Evaluate CI:** Add a GitHub Actions workflow that at minimum compiles the ESP32 firmware on push; add RPi cross-compilation later.
