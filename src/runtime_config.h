#pragma once
#include <Arduino.h>
#include "config.h"

enum class NodeMode : uint8_t {
    AUTO         = 0,  // try WiFi; if connected → BRIDGE, else → MESH
    BRIDGE       = 1,  // WiFi + forward Art-Net to mesh via ESP-NOW
    DIRECT       = 2,  // WiFi only, no ESP-NOW (original behavior)
    MESH         = 3,  // ESP-NOW only, no WiFi (slave node)
    STANDALONE   = 4,  // local patterns only, no network
};

struct StripRuntime {
    uint16_t num_leds;
    uint8_t  start_universe;
    uint16_t channel_offset;
};

struct RuntimeConfig {
    char ssid[33];
    char password[65];
    char hostname[24];
    uint8_t  brightness;
    NodeMode node_mode;
    StripRuntime strips[NUM_STRIPS];
};

void loadConfig(RuntimeConfig& cfg);   // NVS → cfg, falls back to config.h defaults
void saveConfig(const RuntimeConfig& cfg);
