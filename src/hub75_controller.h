#pragma once
#include "config.h"

#if ENABLE_HUB75

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <FastLED.h>
#include "runtime_config.h"

// Total pixel count for the panel (or chained panels).
#define HUB75_TOTAL_LEDS (HUB75_W * HUB75_H * HUB75_CHAIN)

class Hub75Controller {
public:
    void begin(const RuntimeConfig& cfg);

    // Write incoming Art-Net universe data into the pixel buffer.
    // Data is laid out row-major starting from HUB75_START_UNIVERSE.
    void handleUniverse(uint8_t universe, uint8_t* data, uint16_t length);

    // Blit the CRGB pixel buffer to the DMA display.
    void show();

    // Direct access to the pixel buffer (for the pattern engine).
    CRGB* getLeds() { return _buf; }

private:
    CRGB                 _buf[HUB75_TOTAL_LEDS];
    MatrixPanel_I2S_DMA* _panel = nullptr;

    // Write raw RGB bytes at a byte offset into the pixel buffer.
    void writeBytes(uint32_t byte_offset, uint8_t* data, uint16_t len);
};

#endif // ENABLE_HUB75
