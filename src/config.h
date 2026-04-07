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

// ---- Spatial mapping ----
//
// When virt_w > 0 the pattern engine uses virtual canvas coordinates for each
// LED instead of a linear strip index. Multiple nodes spread in physical space
// each know their position within a shared virtual canvas, so patterns appear
// as a single coherent image across the installation.
//
// Virtual canvas origin is (0, 0) top-left. Coordinates range 0..virt_w-1 and
// 0..virt_h-1. LED[0] is placed at (origin_x, origin_y); each successive LED
// steps by (step_x, step_y).
//
// Example — four nodes each with 60 LEDs, arranged horizontally across a
// 256-wide × 256-tall virtual canvas:
//   node 0:  origin_x=0,   origin_y=128, step_x=1.07, step_y=0
//   node 1:  origin_x=64,  origin_y=128, step_x=1.07, step_y=0
//   node 2:  origin_x=128, origin_y=128, step_x=1.07, step_y=0
//   node 3:  origin_x=192, origin_y=128, step_x=1.07, step_y=0
//
// Set virt_w = 0 to disable spatial mapping (linear strip index used instead).

struct SpatialConfig {
    uint16_t virt_w;    // virtual canvas width  (0 = spatial disabled)
    uint16_t virt_h;    // virtual canvas height
    float    origin_x;  // LED[0] X position in virtual canvas
    float    origin_y;  // LED[0] Y position in virtual canvas
    float    step_x;    // X advance per LED (can be negative)
    float    step_y;    // Y advance per LED (can be negative)
    // Panel mode: when panel_w > 0 the pixel buffer is treated as a 2D raster
    // (row-major, panel_w pixels wide). Pixel i maps to (i%panel_w, i/panel_w)
    // instead of using the origin+step formula. Set automatically for HUB75.
    uint16_t panel_w;
};

// Default spatial config — disabled. Set virt_w > 0 to enable.
static const SpatialConfig DEFAULT_SPATIAL = {
    .virt_w   = 0,
    .virt_h   = 256,
    .origin_x = 0.0f,
    .origin_y = 128.0f,
    .step_x   = 1.0f,
    .step_y   = 0.0f,
    .panel_w  = 0,
};

// ---- HUB75 RGB matrix panel ----
//
// Set ENABLE_HUB75 to 1 to drive an HUB75 panel via ESP32-HUB75-MatrixPanel-DMA.
// When enabled the hub75 controller handles Art-Net and pattern output;
// strip output (LedController) still works in parallel if NUM_STRIPS > 0.
//
// Note: default HUB75 pins conflict with default strip pins (16, 17).
// Either change the strip pins in STRIPS[] / led_controller.cpp, or set
// NUM_STRIPS to 0 when using a panel.
//
// Pin defaults (library defaults if unchanged):
//   R1=25 G1=26 B1=27   R2=14 G2=12 B2=13
//   A=23  B=22  C=5     D=17  E=-1 (set to 18 for 64-row panels)
//   CLK=16  LAT=4  OE=15

#define ENABLE_HUB75 0  // set to 1 to enable

#if ENABLE_HUB75
#define HUB75_W             64   // panel pixel width
#define HUB75_H             32   // panel pixel height
#define HUB75_CHAIN         1    // number of panels daisy-chained
#define HUB75_START_UNIVERSE 0   // first Art-Net universe for panel data
#endif
