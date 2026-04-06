#pragma once
#include <FastLED.h>
#include "config.h"
#include "runtime_config.h"

// Physical LED limit per strip. Virtual buffer for PARALLEL modes is the same
// size, so: PARALLEL_2X → num_leds ≤ 256, PARALLEL_3X → num_leds ≤ 170.
#define MAX_LEDS_PER_STRIP 512

class LedController {
public:
    void begin(const RuntimeConfig& cfg);

    // Write incoming Art-Net universe data into the virtual buffer.
    void handleUniverse(uint8_t universe, uint8_t* data, uint16_t length,
                        const RuntimeConfig& cfg);

    // Remap virtual → physical buffers, then push to hardware.
    void show();

    // Direct access to a strip's virtual buffer (for pattern engine).
    // Note: for SECONDARY strips, writes here are overwritten by remap.
    CRGB* getLeds(int strip_idx) { return _virt[strip_idx]; }

private:
    CRGB _virt[NUM_STRIPS][MAX_LEDS_PER_STRIP];  // virtual (Art-Net / pattern input)
    CRGB _phys[NUM_STRIPS][MAX_LEDS_PER_STRIP];  // physical (FastLED output)

    const RuntimeConfig* _cfg = nullptr;

    // Virtual LED count for strip i (= num_leds × factor for PARALLEL modes)
    uint16_t virtualLeds(int i) const;

    void applyResolution();
    void writeBytes(int strip_idx, uint16_t led_start,
                    uint8_t* data, uint16_t byte_count, uint16_t max_leds);
};
