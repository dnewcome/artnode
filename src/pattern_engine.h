#pragma once
#include <FastLED.h>
#include "config.h"

enum class Pattern : uint8_t {
    OFF      = 0,
    SOLID    = 1,   // param1=hue, param2=saturation
    RAINBOW  = 2,   // param1=speed; spatial: sweeps across virtual X axis
    CHASE    = 3,   // param1=hue, param2=speed; spatial: moves across virtual X
    PULSE    = 4,   // param1=hue, param2=speed
    TWINKLE  = 5,   // param1=hue (0=white/random), param2=density
    PLASMA   = 6,   // param1=hue_offset, param2=speed; requires spatial config
};

class PatternEngine {
public:
    void setPattern(Pattern p, uint8_t param1 = 128, uint8_t param2 = 128);

    // Configure spatial mapping. When spatial.virt_w > 0 each LED is addressed
    // by its virtual canvas (x, y) position rather than strip index.
    void setSpatial(const SpatialConfig& s) { _spatial = s; }

    // Fill `leds` with the current frame. Returns true if a new frame was rendered.
    // Call once per strip per loop() — only renders at ~30fps regardless of call rate.
    bool tick(CRGB* leds, uint16_t num_leds);

    // Time sync — the bridge can push its frame counter so all mesh nodes stay
    // temporally aligned even when running patterns independently.
    void      setTime(uint32_t t) { _t = t; }
    uint32_t  getTime()     const { return _t; }

    // Accessors for broadcast
    Pattern  getPattern() const { return _pattern; }
    uint8_t  getParam1()  const { return _p1; }
    uint8_t  getParam2()  const { return _p2; }

private:
    Pattern       _pattern  = Pattern::RAINBOW;
    uint8_t       _p1       = 128;
    uint8_t       _p2       = 128;
    uint32_t      _last_ms  = 0;
    uint32_t      _t        = 0;
    SpatialConfig _spatial  = DEFAULT_SPATIAL;

    // Return virtual X coordinate (0-255) for LED i of n.
    // Falls back to linear (i/n) when spatial is disabled.
    uint8_t vx(uint16_t i, uint16_t n) const;
    uint8_t vy(uint16_t i, uint16_t n) const;
};
