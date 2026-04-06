#pragma once

// WiFi credentials
#define WIFI_SSID "your_ssid"
#define WIFI_PASSWORD "your_password"

// Number of LED strips
#define NUM_STRIPS 2

// Strip definitions: {data_pin, num_leds, start_universe, channel_offset}
// channel_offset: first DMX channel within start_universe (0-based)
// LEDs are packed sequentially across universes if strip spans >170 LEDs
struct StripConfig {
    uint8_t  pin;
    uint16_t num_leds;
    uint8_t  start_universe;
    uint16_t channel_offset;  // byte offset within start_universe (0-based)
};

static const StripConfig STRIPS[NUM_STRIPS] = {
    { .pin = 16, .num_leds = 60,  .start_universe = 0, .channel_offset = 0 },
    { .pin = 17, .num_leds = 60,  .start_universe = 1, .channel_offset = 0 },
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
