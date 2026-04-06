#pragma once
#include <FastLED.h>

enum class Pattern : uint8_t {
    OFF      = 0,
    SOLID    = 1,   // param1=hue, param2=saturation
    RAINBOW  = 2,   // param1=speed (higher=faster)
    CHASE    = 3,   // param1=hue, param2=speed
    PULSE    = 4,   // param1=hue, param2=speed
    TWINKLE  = 5,   // param1=hue (0=white/random), param2=density
};

class PatternEngine {
public:
    void setPattern(Pattern p, uint8_t param1 = 128, uint8_t param2 = 128);

    // Fill `leds` with the current frame. Returns true if a new frame was rendered.
    // Call once per strip per loop() — only renders at ~30fps regardless of call rate.
    bool tick(CRGB* leds, uint16_t num_leds);

private:
    Pattern  _pattern  = Pattern::RAINBOW;
    uint8_t  _p1       = 128;
    uint8_t  _p2       = 128;
    uint32_t _last_ms  = 0;
    uint32_t _t        = 0;   // animation time counter (increments each frame)
};
