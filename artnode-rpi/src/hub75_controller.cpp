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
    fprintf(stderr, "[hub75] matrix OK  brightness=%d\n", options.brightness);

    _canvas = _matrix->CreateFrameCanvas();
    if (!_canvas) {
        fprintf(stderr, "[hub75] CreateFrameCanvas failed\n");
        return;
    }
    fprintf(stderr, "[hub75] canvas OK\n");

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
    // 512 % 3 == 2, so Art-Net universe boundaries almost never land on a pixel
    // boundary.  Walk byte-by-byte, advancing led_idx and channel together so
    // every incoming byte lands in the right colour channel regardless of alignment.
    uint32_t led_idx = byte_offset / 3;
    uint32_t channel = byte_offset % 3;

    if (led_idx >= static_cast<uint32_t>(HUB75_TOTAL_LEDS)) return;

    for (uint16_t i = 0; i < len; i++) {
        switch (channel) {
            case 0: _buf[led_idx].r = data[i]; break;
            case 1: _buf[led_idx].g = data[i]; break;
            case 2: _buf[led_idx].b = data[i]; break;
        }
        if (++channel == 3) {
            channel = 0;
            if (++led_idx >= static_cast<uint32_t>(HUB75_TOTAL_LEDS)) return;
        }
    }
}

void Hub75Controller::show() {
    if (!_canvas) return;

    // Debug: log first call and sample pixel values
    static int showCount = 0;
    if (showCount < 3) {
        fprintf(stderr, "[hub75] show() #%d  buf[0]=(%d,%d,%d)  buf[512]=(%d,%d,%d)\n",
                showCount,
                _buf[0].r, _buf[0].g, _buf[0].b,
                _buf[512].r, _buf[512].g, _buf[512].b);
        showCount++;
    }

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
