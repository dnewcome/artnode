#pragma once
#include <FastLED.h>
#include "config.h"
#include "runtime_config.h"

#define MAX_LEDS_PER_STRIP 512

class LedController {
public:
    void begin(const RuntimeConfig& cfg);
    void handleUniverse(uint8_t universe, uint8_t* data, uint16_t length, const RuntimeConfig& cfg);
    void show();
    CRGB* getLeds(int strip_idx) { return _leds[strip_idx]; }

private:
    CRGB _leds[NUM_STRIPS][MAX_LEDS_PER_STRIP];
    void writeBytes(int strip_idx, uint16_t led_start, uint8_t* data, uint16_t byte_count, uint16_t max_leds);
};
