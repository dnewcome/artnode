#pragma once
#include <cstdint>

// ---------------------------------------------------------------------------
// Strip count and configuration
// ---------------------------------------------------------------------------
// rpi-ws281x drives WS2812B via hardware PWM/DMA.  The RPi has two hardware
// PWM channels so NUM_STRIPS is capped at 2.  GPIOs must be BCM-numbered
// PWM-capable pins: PWM0 → GPIO 12 or 18; PWM1 → GPIO 13 or 19.
// APA102 strips can be driven over SPI without this limit; see led_controller.

#define NUM_STRIPS 2

// ---- Resolution multiplying ----
// Same semantics as the ESP32 project — see that config.h for full docs.

enum class ResMode : uint8_t {
    NONE        = 0,
    FOLD_2X     = 1,
    PARALLEL_2X = 2,
    PARALLEL_3X = 3,
    SECONDARY   = 4,
};

struct StripConfig {
    uint8_t  gpio;            // BCM GPIO pin number (PWM0=18, PWM1=13 default)
    uint16_t num_leds;
    uint8_t  start_universe;
    uint16_t channel_offset;
    ResMode  res_mode;
    uint8_t  res_partner[2]; // strip indices of secondary strips (0xFF = unused)
};

static const StripConfig STRIPS[NUM_STRIPS] = {
    { .gpio = 18, .num_leds = 60, .start_universe = 0, .channel_offset = 0,
      .res_mode = ResMode::NONE, .res_partner = {0xFF, 0xFF} },
    { .gpio = 13, .num_leds = 60, .start_universe = 1, .channel_offset = 0,
      .res_mode = ResMode::NONE, .res_partner = {0xFF, 0xFF} },
};

// LED wire protocol for rpi-ws281x strip_type constant
// WS2811_STRIP_GRB = 0x00081000   (GRB byte order, 800 kHz)
// WS2811_STRIP_RGB = 0x00100800   (RGB byte order)
// This must match the physical strip's protocol; see ws2811.h for all options.
#define STRIP_TYPE_WS2812B  // strips use GRB — mapped to WS2811_STRIP_GRB in led_controller

// ---------------------------------------------------------------------------
// Idle timeout before pattern engine takes over (ms)
// ---------------------------------------------------------------------------
#define IDLE_TIMEOUT_MS 5000

// ---------------------------------------------------------------------------
// Spatial mapping
// ---------------------------------------------------------------------------
struct SpatialConfig {
    uint16_t virt_w;    // virtual canvas width  (0 = spatial disabled)
    uint16_t virt_h;    // virtual canvas height
    float    origin_x;  // LED[0] X position in virtual canvas
    float    origin_y;  // LED[0] Y position in virtual canvas
    float    step_x;    // X advance per LED (can be negative)
    float    step_y;    // Y advance per LED (can be negative)
    uint16_t panel_w;   // >0 → panel/2D raster mode (set automatically for HUB75)
};

static const SpatialConfig DEFAULT_SPATIAL = {
    .virt_w   = 0,
    .virt_h   = 256,
    .origin_x = 0.0f,
    .origin_y = 128.0f,
    .step_x   = 1.0f,
    .step_y   = 0.0f,
    .panel_w  = 0,
};

// ---------------------------------------------------------------------------
// HUB75 RGB matrix panel (rpi-rgb-led-matrix)
// Build with -DENABLE_HUB75=ON to activate.  When enabled the hub75
// controller handles its own Art-Net universes and pattern rendering in
// parallel with strip output (if NUM_STRIPS > 0).
//
// Hardware mapping options: "regular", "adafruit-hat", "adafruit-hat-pwm"
// Set HUB75_MAPPING to match your wiring.
// ---------------------------------------------------------------------------
#ifndef ENABLE_HUB75
#define ENABLE_HUB75 0
#endif

#if ENABLE_HUB75
#define HUB75_W              64     // panel pixel width
#define HUB75_H              64     // panel pixel height
#define HUB75_CHAIN          1      // panels daisy-chained
#define HUB75_START_UNIVERSE 0      // first Art-Net universe for panel
// Hardware mapping: "regular" matches the Seengreat RGB Matrix HAT wiring
//   (R1=11 G1=27 B1=7 R2=8 G2=9 B2=10 A=22 B=23 C=24 D=25 E=15 CLK=17 LAT=4 OE=18)
// Use "adafruit-hat" or "adafruit-hat-pwm" for Adafruit RGB Matrix HAT/Bonnet.
#define HUB75_MAPPING        "regular"  // hardware pin mapping
#define HUB75_GPIO_SLOWDOWN  2      // 1=RPi1, 2=RPi2/3/Zero2, 4=RPi4/5

#define HUB75_TOTAL_LEDS (HUB75_W * HUB75_H * HUB75_CHAIN)
#endif
