#pragma once
#include <Arduino.h>
#include "config.h"

struct StripRuntime {
    uint16_t num_leds;
    uint8_t  start_universe;
    uint16_t channel_offset;
};

struct RuntimeConfig {
    char ssid[33];
    char password[65];
    char hostname[24];
    uint8_t brightness;
    StripRuntime strips[NUM_STRIPS];
};

void loadConfig(RuntimeConfig& cfg);   // NVS → cfg, falls back to config.h defaults
void saveConfig(const RuntimeConfig& cfg);
