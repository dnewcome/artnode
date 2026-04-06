#include "pattern_engine.h"

#define FRAME_MS 33   // ~30fps

void PatternEngine::setPattern(Pattern p, uint8_t param1, uint8_t param2) {
    _pattern = p;
    _p1      = param1;
    _p2      = param2;
    _t       = 0;
}

bool PatternEngine::tick(CRGB* leds, uint16_t n) {
    uint32_t now = millis();
    if (now - _last_ms < FRAME_MS) return false;
    _last_ms = now;
    _t++;

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
            // _p1 = speed (1-255). hue shifts each frame, spread across strip.
            uint8_t hue = (uint8_t)(_t * (_p1 >> 2));
            for (uint16_t i = 0; i < n; i++) {
                leds[i] = CHSV(hue + (uint8_t)(i * 256 / n), 240, 255);
            }
            break;
        }
        case Pattern::CHASE: {
            // _p1 = hue, _p2 = speed
            fill_solid(leds, n, CRGB::Black);
            uint16_t pos = (_t * (_p2 >> 3)) % n;
            // Lit dot with a short tail
            for (int k = 0; k < 5; k++) {
                uint16_t idx = (pos + n - k) % n;
                uint8_t  val = 255 - k * 48;
                leds[idx] = CHSV(_p1, 240, val);
            }
            break;
        }
        case Pattern::PULSE: {
            // _p1 = hue, _p2 = speed
            uint8_t val = sin8((uint8_t)(_t * (_p2 >> 3)));
            fill_solid(leds, n, CHSV(_p1, 240, val));
            break;
        }
        case Pattern::TWINKLE: {
            // _p1 = hue (0 = random color), _p2 = density (higher = more lit)
            // Fade everything down slightly each frame
            for (uint16_t i = 0; i < n; i++) {
                leds[i].nscale8(220);
            }
            // Randomly light up some LEDs based on density
            uint16_t count = (_p2 * n) / 512 + 1;
            for (uint16_t k = 0; k < count; k++) {
                uint16_t idx = random16(n);
                uint8_t  hue = (_p1 == 0) ? random8() : _p1;
                leds[idx] = CHSV(hue, (_p1 == 0) ? 200 : 240, 255);
            }
            break;
        }
    }
    return true;
}
