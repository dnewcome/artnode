#pragma once
#include "config.h"

#if ENABLE_HUB75

#include <led-matrix.h>
#include "color_utils.h"
#include "runtime_config.h"

class Hub75Controller {
public:
    void begin(const RuntimeConfig& cfg);

    // Write incoming Art-Net universe data into the pixel buffer.
    // Pixels are laid out row-major starting from HUB75_START_UNIVERSE.
    void handleUniverse(uint8_t universe, uint8_t* data, uint16_t length);

    // Blit the CRGB pixel buffer to the panel (double-buffered via SwapOnVSync).
    void show();

    // Direct access to the pixel buffer (for the pattern engine).
    CRGB* getLeds() { return _buf; }

private:
    CRGB                         _buf[HUB75_TOTAL_LEDS]{};
    rgb_matrix::RGBMatrix*       _matrix = nullptr;

    void writeBytes(uint32_t byte_offset, uint8_t* data, uint16_t len);
};

#endif // ENABLE_HUB75
