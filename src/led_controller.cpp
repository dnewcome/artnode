#include "led_controller.h"

// FastLED requires compile-time pin numbers.
// Pin values come from STRIPS[] in config.h; only num_leds is runtime-variable.
// If you add strips, mirror the addLeds call here and add to config.h.
void LedController::begin(const RuntimeConfig& cfg) {
    FastLED.addLeds<LED_TYPE, 16, COLOR_ORDER>(_leds[0], cfg.strips[0].num_leds)
           .setCorrection(TypicalLEDStrip);
#if NUM_STRIPS > 1
    FastLED.addLeds<LED_TYPE, 17, COLOR_ORDER>(_leds[1], cfg.strips[1].num_leds)
           .setCorrection(TypicalLEDStrip);
#endif
    FastLED.setBrightness(cfg.brightness);
    FastLED.clear(true);
}

void LedController::handleUniverse(uint8_t universe, uint8_t* data, uint16_t length, const RuntimeConfig& cfg) {
    for (int i = 0; i < NUM_STRIPS; i++) {
        const StripRuntime& s = cfg.strips[i];

        if (universe < s.start_universe) continue;

        uint32_t strip_byte_start = (uint32_t)(universe - s.start_universe) * 512;

        uint8_t*  src    = data;
        uint16_t  srclen = length;

        if (universe == s.start_universe) {
            if (srclen <= s.channel_offset) continue;
            src    += s.channel_offset;
            srclen -= s.channel_offset;
            strip_byte_start = 0;
        } else {
            strip_byte_start -= s.channel_offset;
        }

        uint32_t strip_total_bytes = (uint32_t)s.num_leds * 3;
        if (strip_byte_start >= strip_total_bytes) continue;

        uint16_t led_start = strip_byte_start / 3;
        uint16_t usable    = (uint16_t)min((uint32_t)srclen, strip_total_bytes - strip_byte_start);

        writeBytes(i, led_start, src, usable, s.num_leds);
    }
}

void LedController::writeBytes(int strip_idx, uint16_t led_start, uint8_t* data, uint16_t byte_count, uint16_t max_leds) {
    uint16_t i = 0, led = led_start;
    while (i + 2 < byte_count && led < max_leds) {
        _leds[strip_idx][led].r = data[i];
        _leds[strip_idx][led].g = data[i + 1];
        _leds[strip_idx][led].b = data[i + 2];
        i += 3;
        led++;
    }
}

void LedController::show() {
    FastLED.show();
}
