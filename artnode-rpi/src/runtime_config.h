#pragma once
#include <cstdint>
#include <cstring>
#include "config.h"

// Simplified node modes — no ESP-NOW mesh on RPi.
// Values are kept numerically compatible with the ESP32 project so
// JSON config files are interchangeable.
enum class NodeMode : uint8_t {
    DIRECT     = 2,   // listen for Art-Net; local patterns on idle
    STANDALONE = 4,   // local patterns only, no network
};

struct StripRuntime {
    uint16_t num_leds;
    uint8_t  start_universe;
    uint16_t channel_offset;
};

struct RuntimeConfig {
    char         hostname[24];
    uint8_t      brightness;
    NodeMode     node_mode;
    StripRuntime strips[NUM_STRIPS];
    SpatialConfig spatial;
};

// Load config from /etc/artnode/config.json (falls back to config.h defaults).
void loadConfig(RuntimeConfig& cfg);

// Persist config to /etc/artnode/config.json.
// Creates /etc/artnode/ if it doesn't exist (requires write permission).
void saveConfig(const RuntimeConfig& cfg);
