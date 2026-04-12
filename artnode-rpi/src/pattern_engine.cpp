// Pattern engine — ported from the ESP32 project.
// Only changes: FastLED.h → color_utils.h, millis() from platform.h.
// All pattern logic is identical.

#include "pattern_engine.h"
#include "platform.h"

#define FRAME_MS 33   // ~30 fps

void PatternEngine::setPattern(Pattern p, uint8_t param1, uint8_t param2) {
    _pattern = p;
    _p1      = param1;
    _p2      = param2;
    _t       = 0;
}

uint8_t PatternEngine::vx(uint16_t i, uint16_t n) const {
    if (_spatial.panel_w > 0) {
        uint16_t col = i % _spatial.panel_w;
        uint16_t W   = _spatial.virt_w > 0 ? _spatial.virt_w : _spatial.panel_w;
        return static_cast<uint8_t>(col * 255 / (W > 1 ? W - 1 : 1));
    }
    if (_spatial.virt_w == 0) {
        return static_cast<uint8_t>(i * 255 / (n > 1 ? n - 1 : 1));
    }
    float fx = _spatial.origin_x + i * _spatial.step_x;
    int   iv = static_cast<int>(fx * 255.0f / static_cast<float>(_spatial.virt_w));
    return static_cast<uint8_t>(iv < 0 ? 0 : iv > 255 ? 255 : iv);
}

uint8_t PatternEngine::vy(uint16_t i, uint16_t n) const {
    if (_spatial.panel_w > 0) {
        uint16_t row = i / _spatial.panel_w;
        uint16_t H   = _spatial.virt_h > 0 ? _spatial.virt_h : (n / _spatial.panel_w);
        return static_cast<uint8_t>(row * 255 / (H > 1 ? H - 1 : 1));
    }
    if (_spatial.virt_h == 0) return 128;
    float fy = _spatial.origin_y + i * _spatial.step_y;
    int   iv = static_cast<int>(fy * 255.0f / static_cast<float>(_spatial.virt_h));
    return static_cast<uint8_t>(iv < 0 ? 0 : iv > 255 ? 255 : iv);
}

bool PatternEngine::tick(CRGB* leds, uint16_t n) {
    uint32_t now = millis();
    bool newFrame = (now - _last_ms >= FRAME_MS);
    if (newFrame) {
        _last_ms = now;
        _t++;
    }
    // Always render to the caller's buffer — multiple outputs (strips + hub75)
    // call tick() each loop; only the first returns true (rate gate), but all
    // need fresh pixel data regardless of which fired the gate.

    switch (_pattern) {
        case Pattern::OFF: {
            fill_solid(leds, n, CRGB::Black);
            break;
        }
        case Pattern::SOLID: {
            fill_solid(leds, n, CHSV(_p1, _p2, 255));
            break;
        }
        case Pattern::RAINBOW: {
            uint8_t base_hue = static_cast<uint8_t>(_t * (_p1 >> 2));
            for (uint16_t i = 0; i < n; i++) {
                leds[i] = CHSV(base_hue + vx(i, n), 240, 255);
            }
            break;
        }
        case Pattern::CHASE: {
            fill_solid(leds, n, CRGB::Black);
            uint8_t chase_pos = static_cast<uint8_t>((_t * (_p2 >> 3)) & 0xFF);
            for (uint16_t i = 0; i < n; i++) {
                uint8_t x  = vx(i, n);
                int8_t  d  = static_cast<int8_t>(x - chase_pos);
                if (d >= 0 && d < 5) {
                    leds[i] = CHSV(_p1, 240, static_cast<uint8_t>(255 - d * 48));
                }
            }
            break;
        }
        case Pattern::PULSE: {
            uint8_t val = sin8(static_cast<uint8_t>(_t * (_p2 >> 3)));
            fill_solid(leds, n, CHSV(_p1, 240, val));
            break;
        }
        case Pattern::TWINKLE: {
            for (uint16_t i = 0; i < n; i++) leds[i].nscale8(220);
            uint16_t count = (_p2 * n) / 512 + 1;
            for (uint16_t k = 0; k < count; k++) {
                uint16_t idx = random16(n);
                uint8_t  hue = (_p1 == 0) ? random8() : _p1;
                leds[idx] = CHSV(hue, (_p1 == 0) ? 200 : 240, 255);
            }
            break;
        }
        case Pattern::PLASMA: {
            uint8_t spd = _p2 >> 4;
            for (uint16_t i = 0; i < n; i++) {
                uint8_t x = vx(i, n);
                uint8_t y = vy(i, n);
                uint8_t v = sin8(static_cast<uint8_t>(x * 3 + _t * spd))
                          + sin8(static_cast<uint8_t>(y * 3 + _t * (spd >> 1)))
                          + sin8(static_cast<uint8_t>((x + y) + _t * (spd + 1)));
                leds[i] = CHSV(static_cast<uint8_t>(_p1 + (v >> 1)), 240,
                               static_cast<uint8_t>(200 + (v >> 2)));
            }
            break;
        }
    }
    return newFrame;
}
