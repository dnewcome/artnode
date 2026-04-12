#pragma once
#include <cstdint>

#ifdef MOCK_HARDWARE
// Stub types so the class definition compiles without rpi-ws281x.
#define RPI_PWM_CHANNELS 2
typedef uint32_t ws2811_led_t;
struct ws2811_channel_t { ws2811_led_t* leds; uint8_t brightness; unsigned count; };
struct ws2811_t { ws2811_channel_t channel[RPI_PWM_CHANNELS]; };
#else
#include <ws2811.h>
#endif

#include "color_utils.h"
#include "config.h"
#include "runtime_config.h"

static_assert(NUM_STRIPS <= RPI_PWM_CHANNELS,
    "rpi-ws281x supports at most 2 hardware PWM channels. "
    "Reduce NUM_STRIPS or use SPI-based APA102 strips.");

// Physical LED limit per strip.  Same as the ESP32 project.
// PARALLEL_2X constraint: num_leds ≤ 256.  PARALLEL_3X: num_leds ≤ 170.
#define MAX_LEDS_PER_STRIP 512

class LedController {
public:
    void begin(const RuntimeConfig& cfg);

    // Release ws2811 hardware resources.  Call before process exit.
    void cleanup();

    // Write incoming Art-Net universe data into the virtual buffer.
    void handleUniverse(uint8_t universe, uint8_t* data, uint16_t length,
                        const RuntimeConfig& cfg);

    // Remap virtual → physical buffers and push to hardware.
    void show();

    // Direct access to a strip's virtual buffer (for the pattern engine).
    CRGB* getLeds(int strip_idx) { return _virt[strip_idx]; }

private:
    CRGB _virt[NUM_STRIPS][MAX_LEDS_PER_STRIP];
    CRGB _phys[NUM_STRIPS][MAX_LEDS_PER_STRIP];

    const RuntimeConfig* _cfg = nullptr;
#ifndef MOCK_HARDWARE
    ws2811_t             _ledstring{};
#endif

    uint16_t virtualLeds(int i) const;
    void applyResolution();
    void writeBytes(int strip_idx, uint16_t led_start,
                    uint8_t* data, uint16_t byte_count, uint16_t max_leds);
};
