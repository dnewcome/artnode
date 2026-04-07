#include "hub75_controller.h"

#if ENABLE_HUB75

void Hub75Controller::begin(const RuntimeConfig& cfg) {
    HUB75_I2S_CFG mxconfig(HUB75_W, HUB75_H, HUB75_CHAIN);

    // The library ships with sane defaults matching common HUB75 wiring.
    // Override individual pins here if your board differs, e.g.:
    //   mxconfig.gpio.e = 18;   // for 64-row panels
    //   mxconfig.gpio.clk = 16;

    _panel = new MatrixPanel_I2S_DMA(mxconfig);
    _panel->begin();
    _panel->setBrightness8(cfg.brightness);
    _panel->clearScreen();

    memset(_buf, 0, sizeof(_buf));
    Serial.printf("[hub75] %dx%d panel ready\n", HUB75_W, HUB75_H);
}

// Art-Net packs RGB pixels linearly from HUB75_START_UNIVERSE onward.
// Universe u starts at byte offset (u - HUB75_START_UNIVERSE) * 512.
void Hub75Controller::handleUniverse(uint8_t universe, uint8_t* data, uint16_t length) {
    if (universe < HUB75_START_UNIVERSE) return;

    uint32_t byte_offset = (uint32_t)(universe - HUB75_START_UNIVERSE) * 512;
    uint32_t total_bytes = (uint32_t)HUB75_TOTAL_LEDS * 3;
    if (byte_offset >= total_bytes) return;

    uint16_t usable = (uint16_t)min((uint32_t)length, total_bytes - byte_offset);
    writeBytes(byte_offset, data, usable);
}

void Hub75Controller::writeBytes(uint32_t byte_offset, uint8_t* data, uint16_t len) {
    uint32_t pixel = byte_offset / 3;
    uint32_t frac  = byte_offset % 3;   // byte within the first partial pixel (rare)

    uint16_t i = 0;
    // Handle partial first pixel if offset is mid-pixel
    if (frac && pixel < HUB75_TOTAL_LEDS) {
        if (frac == 1 && i + 0 < len) { _buf[pixel].g = data[i++]; frac++; }
        if (frac == 2 && i + 0 < len) { _buf[pixel].b = data[i++]; }
        pixel++;
    }
    // Bulk copy full pixels
    while (i + 2 < len && pixel < HUB75_TOTAL_LEDS) {
        _buf[pixel].r = data[i];
        _buf[pixel].g = data[i + 1];
        _buf[pixel].b = data[i + 2];
        i += 3;
        pixel++;
    }
}

void Hub75Controller::show() {
    if (!_panel) return;
    for (int y = 0; y < HUB75_H; y++) {
        for (int x = 0; x < HUB75_W * HUB75_CHAIN; x++) {
            const CRGB& c = _buf[y * HUB75_W * HUB75_CHAIN + x];
            _panel->drawPixelXY(x, y, _panel->color565(c.r, c.g, c.b));
        }
    }
}

#endif // ENABLE_HUB75
