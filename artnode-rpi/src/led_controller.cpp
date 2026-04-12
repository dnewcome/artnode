#include "led_controller.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

// ---------------------------------------------------------------------------
// Hardware layer — real (rpi-ws281x) or mock
// ---------------------------------------------------------------------------

#ifdef MOCK_HARDWARE

void LedController::begin(const RuntimeConfig& cfg) {
    _cfg = &cfg;
    memset(_virt, 0, sizeof(_virt));
    memset(_phys, 0, sizeof(_phys));
    printf("[led-stub] begin: %d strips\n", NUM_STRIPS);
    for (int i = 0; i < NUM_STRIPS; i++) {
        printf("[led-stub]   strip %d gpio=%d leds=%u uni=%u\n",
               i, STRIPS[i].gpio, cfg.strips[i].num_leds, cfg.strips[i].start_universe);
    }
}
void LedController::cleanup() {}

#else

#ifndef WS2811_STRIP_GRB
#define WS2811_STRIP_GRB 0x00081000
#endif
#define ARTNODE_DMA_CHANNEL 5

void LedController::begin(const RuntimeConfig& cfg) {
    _cfg = &cfg;

    _ledstring = {};
    _ledstring.freq   = WS2811_TARGET_FREQ;
    _ledstring.dmanum = ARTNODE_DMA_CHANNEL;

    for (int i = 0; i < NUM_STRIPS; i++) {
        _ledstring.channel[i].gpionum    = STRIPS[i].gpio;
        _ledstring.channel[i].count      = cfg.strips[i].num_leds;
        _ledstring.channel[i].invert     = 0;
        _ledstring.channel[i].brightness = cfg.brightness;
        _ledstring.channel[i].strip_type = WS2811_STRIP_GRB;
    }

    ws2811_return_t ret = ws2811_init(&_ledstring);
    if (ret != WS2811_SUCCESS) {
        fprintf(stderr, "[led] ws2811_init failed: %s\n", ws2811_get_return_t_str(ret));
    }
}
void LedController::cleanup() {
    ws2811_fini(&_ledstring);
}

#endif // MOCK_HARDWARE

// ---- Universe → virtual buffer ----------------------------------------

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
        if (STRIPS[i].res_mode == ResMode::SECONDARY) continue;

        const StripRuntime& s = cfg.strips[i];
        uint16_t virt_leds = virtualLeds(i);

        if (universe < s.start_universe) continue;

        uint32_t strip_byte_start = static_cast<uint32_t>(universe - s.start_universe) * 512;

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

        uint32_t strip_total_bytes = static_cast<uint32_t>(virt_leds) * 3;
        if (strip_byte_start >= strip_total_bytes) continue;

        uint16_t led_start = strip_byte_start / 3;
        uint16_t usable    = static_cast<uint16_t>(
            std::min(static_cast<uint32_t>(srclen), strip_total_bytes - strip_byte_start));

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

// ---- Resolution remap (identical logic to ESP32) -----------------------

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
                        case 0:                       _phys[i][slot] = _virt[i][v]; break;
                        case 1: if (j < NUM_STRIPS)   _phys[j][slot] = _virt[i][v]; break;
                        case 2: if (k < NUM_STRIPS)   _phys[k][slot] = _virt[i][v]; break;
                    }
                }
                break;
            }
            case ResMode::SECONDARY:
                break;
        }
    }
}

// ---- Push to hardware --------------------------------------------------

#ifdef MOCK_HARDWARE

void LedController::show() {
    applyResolution();
    // Pixels are in _phys buffers; hardware output is a no-op in stub mode.
}

#else

void LedController::show() {
    applyResolution();

    for (int i = 0; i < NUM_STRIPS; i++) {
        if (!_ledstring.channel[i].leds) continue;

        _ledstring.channel[i].brightness = _cfg->brightness;

        uint16_t n = _cfg->strips[i].num_leds;
        for (uint16_t j = 0; j < n; j++) {
            _ledstring.channel[i].leds[j] =
                (static_cast<uint32_t>(_phys[i][j].r) << 16) |
                (static_cast<uint32_t>(_phys[i][j].g) <<  8) |
                 static_cast<uint32_t>(_phys[i][j].b);
        }
    }

    ws2811_return_t ret = ws2811_render(&_ledstring);
    if (ret != WS2811_SUCCESS) {
        fprintf(stderr, "[led] ws2811_render: %s\n", ws2811_get_return_t_str(ret));
    }
}

#endif // MOCK_HARDWARE
