#include "led_controller.h"

// FastLED requires compile-time pin numbers.
// Pins come from STRIPS[] in config.h. If you add strips, mirror addLeds here.
// FastLED is registered against _phys — the post-remap physical buffer.
void LedController::begin(const RuntimeConfig& cfg) {
    _cfg = &cfg;

    FastLED.addLeds<LED_TYPE, 16, COLOR_ORDER>(_phys[0], STRIPS[0].num_leds)
           .setCorrection(TypicalLEDStrip);
#if NUM_STRIPS > 1
    FastLED.addLeds<LED_TYPE, 17, COLOR_ORDER>(_phys[1], STRIPS[1].num_leds)
           .setCorrection(TypicalLEDStrip);
#endif
    FastLED.setBrightness(cfg.brightness);
    FastLED.clear(true);
}

// Virtual LED count: for PARALLEL modes, Art-Net addresses num_leds × factor
// across one logical strip; the remap splits them across physical strips.
uint16_t LedController::virtualLeds(int i) const {
    switch (STRIPS[i].res_mode) {
        case ResMode::PARALLEL_2X: return _cfg->strips[i].num_leds * 2;
        case ResMode::PARALLEL_3X: return _cfg->strips[i].num_leds * 3;
        default:                   return _cfg->strips[i].num_leds;
    }
}

void LedController::handleUniverse(uint8_t universe, uint8_t* data, uint16_t length,
                                   const RuntimeConfig& cfg) {
    for (int i = 0; i < NUM_STRIPS; i++) {
        // Secondaries are driven by their primary's remap — skip
        if (STRIPS[i].res_mode == ResMode::SECONDARY) continue;

        const StripRuntime& s = cfg.strips[i];
        uint16_t virt_leds = virtualLeds(i);

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

        uint32_t strip_total_bytes = (uint32_t)virt_leds * 3;
        if (strip_byte_start >= strip_total_bytes) continue;

        uint16_t led_start = strip_byte_start / 3;
        uint16_t usable    = (uint16_t)min((uint32_t)srclen, strip_total_bytes - strip_byte_start);

        writeBytes(i, led_start, src, usable, virt_leds);
    }
}

void LedController::writeBytes(int strip_idx, uint16_t led_start,
                               uint8_t* data, uint16_t byte_count, uint16_t max_leds) {
    uint16_t i = 0, led = led_start;
    while (i + 2 < byte_count && led < max_leds) {
        _virt[strip_idx][led].r = data[i];
        _virt[strip_idx][led].g = data[i + 1];
        _virt[strip_idx][led].b = data[i + 2];
        i += 3;
        led++;
    }
}

// Remap virtual pixel buffer → physical LED buffer for each strip.
// NONE:        direct copy
// FOLD_2X:     interleave across the fold — even virtual → first half forward,
//              odd virtual → second half backward
// PARALLEL_2X: even virtual pixels → primary physical, odd → secondary physical
// PARALLEL_3X: virtual % 3 routes to primary / partner[0] / partner[1]
// SECONDARY:   physical buffer is written by the primary's case above; skip
void LedController::applyResolution() {
    for (int i = 0; i < NUM_STRIPS; i++) {
        const StripConfig&  sc    = STRIPS[i];
        const StripRuntime& sr    = _cfg->strips[i];
        uint16_t            phys_n = sr.num_leds;

        switch (sc.res_mode) {

            case ResMode::NONE: {
                memcpy(_phys[i], _virt[i], phys_n * sizeof(CRGB));
                break;
            }

            case ResMode::FOLD_2X: {
                // virtual[v] interleaves across the physical strip:
                //   v even → phys[v/2]          (first half, forward)
                //   v odd  → phys[N-1 - v/2]    (second half, backward)
                uint16_t n = phys_n;
                for (uint16_t v = 0; v < n; v++) {
                    uint16_t p = (v % 2 == 0) ? (v / 2) : (n - 1 - v / 2);
                    _phys[i][p] = _virt[i][v];
                }
                break;
            }

            case ResMode::PARALLEL_2X: {
                uint8_t  j      = sc.res_partner[0];
                uint16_t virt_n = phys_n * 2;
                for (uint16_t v = 0; v < virt_n; v++) {
                    if (v % 2 == 0) {
                        _phys[i][v / 2] = _virt[i][v];
                    } else if (j < NUM_STRIPS) {
                        _phys[j][v / 2] = _virt[i][v];
                    }
                }
                break;
            }

            case ResMode::PARALLEL_3X: {
                uint8_t  j      = sc.res_partner[0];
                uint8_t  k      = sc.res_partner[1];
                uint16_t virt_n = phys_n * 3;
                for (uint16_t v = 0; v < virt_n; v++) {
                    uint16_t slot = v / 3;
                    switch (v % 3) {
                        case 0:                          _phys[i][slot] = _virt[i][v]; break;
                        case 1: if (j < NUM_STRIPS)      _phys[j][slot] = _virt[i][v]; break;
                        case 2: if (k < NUM_STRIPS)      _phys[k][slot] = _virt[i][v]; break;
                    }
                }
                break;
            }

            case ResMode::SECONDARY:
                // Written by primary — nothing to do here
                break;
        }
    }
}

void LedController::show() {
    applyResolution();
    FastLED.show();
}
