#pragma once
#include "color_utils.h"
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

    // Configure spatial mapping.  When spatial.virt_w > 0 each LED is addressed
    // by its virtual canvas (x, y) position rather than strip index.
    void setSpatial(const SpatialConfig& s) { _spatial = s; }

    // Fill `leds` with the current frame.  Returns true if a new frame was rendered.
    // Call once per strip per loop iteration — renders at ~30 fps regardless of call rate.
    bool tick(CRGB* leds, uint16_t num_leds);

    // Time sync — push a frame counter so spatially distributed nodes stay
    // temporally aligned when running patterns independently.
    void     setTime(uint32_t t) { _t = t; }
    uint32_t getTime()     const { return _t; }

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

    uint8_t vx(uint16_t i, uint16_t n) const;
    uint8_t vy(uint16_t i, uint16_t n) const;
};
