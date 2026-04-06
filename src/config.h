#pragma once

// WiFi credentials
#define WIFI_SSID "your_ssid"
#define WIFI_PASSWORD "your_password"

// Number of LED strips (physical)
#define NUM_STRIPS 2

// ---- Resolution multiplying ----
//
// Placing multiple strips (or a folded strip) at sub-pitch offsets increases
// effective spatial resolution without changing the LED hardware.
//
// NONE        — direct 1:1 mapping (default)
// FOLD_2X     — single strip folded back on itself. num_leds is the full
//               physical count; virtual pixels interleave across the fold:
//               virtual 0 → LED 0, virtual 1 → LED N-1, virtual 2 → LED 1 …
// PARALLEL_2X — two physical strips offset by ½ pitch. Mark the primary strip
//               PARALLEL_2X and set res_partner[0] to the secondary strip index.
//               Mark the secondary strip SECONDARY. Art-Net addresses 2× num_leds
//               virtual pixels; even pixels → primary, odd → secondary.
// PARALLEL_3X — three physical strips offset by ⅓ pitch. Primary + two partners.
//               Pixels mod 3 route to primary / partner[0] / partner[1].
// SECONDARY   — driven by its primary's remap; do not assign a universe to it.
//
// Constraint: for PARALLEL modes, num_leds × factor must fit in MAX_LEDS_PER_STRIP.
//   PARALLEL_2X: num_leds ≤ 256   PARALLEL_3X: num_leds ≤ 170
//
// Examples
// --------
// Single strip, 2× density via fold (120 LEDs folded to 60 physical positions):
//   { .pin=16, .num_leds=120, .start_universe=0, .channel_offset=0,
//     .res_mode=ResMode::FOLD_2X }
//
// Two strips, 2× density via parallel interleave (each strip 60 LEDs):
//   { .pin=16, .num_leds=60, .start_universe=0, .channel_offset=0,
//     .res_mode=ResMode::PARALLEL_2X, .res_partner={1,0xFF} },   // primary
//   { .pin=17, .num_leds=60, .start_universe=0, .channel_offset=0,
//     .res_mode=ResMode::SECONDARY },                             // secondary
//
// Three strips, 3× density (each strip 40 LEDs):
//   { .pin=16, .num_leds=40, .start_universe=0, .channel_offset=0,
//     .res_mode=ResMode::PARALLEL_3X, .res_partner={1,2} },
//   { .pin=17, .num_leds=40, .start_universe=0, .channel_offset=0,
//     .res_mode=ResMode::SECONDARY },
//   { .pin=18, .num_leds=40, .start_universe=0, .channel_offset=0,
//     .res_mode=ResMode::SECONDARY },

enum class ResMode : uint8_t {
    NONE        = 0,
    FOLD_2X     = 1,
    PARALLEL_2X = 2,
    PARALLEL_3X = 3,
    SECONDARY   = 4,
};

// Strip definitions
// num_leds = physical LED count on this strip
struct StripConfig {
    uint8_t  pin;
    uint16_t num_leds;
    uint8_t  start_universe;
    uint16_t channel_offset;   // byte offset within start_universe (0-based)
    ResMode  res_mode;
    uint8_t  res_partner[2];   // strip indices of secondary strips (0xFF = unused)
};

static const StripConfig STRIPS[NUM_STRIPS] = {
    { .pin = 16, .num_leds = 60, .start_universe = 0, .channel_offset = 0,
      .res_mode = ResMode::NONE, .res_partner = {0xFF, 0xFF} },
    { .pin = 17, .num_leds = 60, .start_universe = 1, .channel_offset = 0,
      .res_mode = ResMode::NONE, .res_partner = {0xFF, 0xFF} },
};

// FastLED color order — change per strip type (WS2812B = GRB, APA102 = BGR, etc.)
#define COLOR_ORDER GRB
#define LED_TYPE    WS2812B

// ---- Mesh / mode settings ----

// WiFi channel used for ESP-NOW in MESH/BRIDGE modes.
// Must match your router's channel when using BRIDGE mode.
// Run: iw dev wlan0 info | grep channel   (on the machine connected to your AP)
#define MESH_CHANNEL 6

// How long to wait for WiFi before falling back to mesh-only mode (ms)
#define WIFI_TIMEOUT_MS 10000

// How long without a DMX/ESP-NOW frame before switching to local patterns (ms)
#define IDLE_TIMEOUT_MS 5000
