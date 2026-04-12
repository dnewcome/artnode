#include "hub75_controller.h"

#if ENABLE_HUB75

#include <cstring>
#include <algorithm>
#include <cstdio>

void Hub75Controller::begin(const RuntimeConfig& cfg) {
    rgb_matrix::RGBMatrix::Options options;
    options.hardware_mapping = HUB75_MAPPING;
    options.rows             = HUB75_H;
    options.cols             = HUB75_W;
    options.chain_length     = HUB75_CHAIN;
    options.parallel         = 1;
    // brightness is 0-100 in rpi-rgb-led-matrix; scale from 0-255
    options.brightness       = (cfg.brightness * 100 + 127) / 255;

    rgb_matrix::RuntimeOptions runtime_opt;
    runtime_opt.gpio_slowdown  = HUB75_GPIO_SLOWDOWN;
    runtime_opt.drop_privileges = 0;  // keep root for hardware access

    _matrix = rgb_matrix::RGBMatrix::CreateFromOptions(options, runtime_opt);
    if (!_matrix) {
        fprintf(stderr, "[hub75] RGBMatrix::CreateFromOptions failed\n");
        return;
    }
    _canvas = _matrix->CreateFrameCanvas();

    memset(_buf, 0, sizeof(_buf));
}

void Hub75Controller::handleUniverse(uint8_t universe, uint8_t* data, uint16_t length) {
    // Pixel data is laid out row-major across universes starting at HUB75_START_UNIVERSE.
    if (universe < HUB75_START_UNIVERSE) return;

    uint32_t byte_offset = static_cast<uint32_t>(universe - HUB75_START_UNIVERSE) * 512;
    uint32_t total_bytes = static_cast<uint32_t>(HUB75_TOTAL_LEDS) * 3;
    if (byte_offset >= total_bytes) return;

    uint16_t usable = static_cast<uint16_t>(
        std::min(static_cast<uint32_t>(length), total_bytes - byte_offset));
    writeBytes(byte_offset, data, usable);
}

void Hub75Controller::writeBytes(uint32_t byte_offset, uint8_t* data, uint16_t len) {
    uint32_t led_start = byte_offset / 3;
    uint32_t carry     = byte_offset % 3;  // partial pixel carry (uncommon)

    // Fast path: byte_offset is always 3-aligned in normal Art-Net usage
    if (carry == 0) {
        uint32_t n = std::min(static_cast<uint32_t>(len / 3),
                              static_cast<uint32_t>(HUB75_TOTAL_LEDS) - led_start);
        for (uint32_t i = 0; i < n; i++) {
            _buf[led_start + i].r = data[i * 3];
            _buf[led_start + i].g = data[i * 3 + 1];
            _buf[led_start + i].b = data[i * 3 + 2];
        }
    }
}

void Hub75Controller::show() {
    if (!_canvas) return;

    int total_w = HUB75_W * HUB75_CHAIN;
    for (int i = 0; i < HUB75_TOTAL_LEDS; i++) {
        int x = i % total_w;
        int y = i / total_w;
        _canvas->SetPixel(x, y, _buf[i].r, _buf[i].g, _buf[i].b);
    }
    // SwapOnVSync returns the old (now free) canvas for reuse next frame
    _canvas = _matrix->SwapOnVSync(_canvas);
}

#endif // ENABLE_HUB75
