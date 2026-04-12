#pragma once
// FastLED-compatible color types and math utilities for the Linux/RPi build.
// CRGB, CHSV, fill_solid, sin8, random8/16 — same API as FastLED so
// pattern_engine.cpp compiles unmodified.

#include <cstdint>
#include <cmath>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
struct CHSV;

// ---------------------------------------------------------------------------
// CRGB
// ---------------------------------------------------------------------------
struct CRGB {
    uint8_t r, g, b;

    CRGB() = default;
    constexpr CRGB(uint8_t r, uint8_t g, uint8_t b) : r(r), g(g), b(b) {}

    // Construct / assign from HSV (defined inline after CHSV below)
    CRGB(const CHSV& hsv);
    CRGB& operator=(const CHSV& hsv);

    // Scale each channel by s/256 (FastLED nscale8 semantics)
    void nscale8(uint8_t s) {
        r = static_cast<uint8_t>((static_cast<uint16_t>(r) * s) >> 8);
        g = static_cast<uint8_t>((static_cast<uint16_t>(g) * s) >> 8);
        b = static_cast<uint8_t>((static_cast<uint16_t>(b) * s) >> 8);
    }

    static const CRGB Black;
};

// ---------------------------------------------------------------------------
// CHSV
// ---------------------------------------------------------------------------
struct CHSV {
    uint8_t h, s, v;
    constexpr CHSV(uint8_t h, uint8_t s, uint8_t v) : h(h), s(s), v(v) {}
};

// ---------------------------------------------------------------------------
// HSV → RGB  (FastLED "rainbow" colorspace — 6-segment linear ramp)
// H=0..255 maps to 0°..360° in equal 43-unit steps; S,V are 0..255.
// ---------------------------------------------------------------------------
inline void hsv2rgb(const CHSV& c, CRGB& out) {
    if (c.s == 0) { out.r = out.g = out.b = c.v; return; }

    uint8_t  region    = c.h / 43;
    uint8_t  remainder = static_cast<uint8_t>((c.h - region * 43u) * 6u);

    uint8_t p = static_cast<uint8_t>(
        (static_cast<uint16_t>(c.v) * (255u - c.s)) >> 8);
    uint8_t q = static_cast<uint8_t>(
        (static_cast<uint16_t>(c.v) *
         (255u - (static_cast<uint16_t>(c.s) * remainder >> 8))) >> 8);
    uint8_t t = static_cast<uint8_t>(
        (static_cast<uint16_t>(c.v) *
         (255u - (static_cast<uint16_t>(c.s) * (255u - remainder) >> 8))) >> 8);

    switch (region) {
        case 0: out.r = c.v; out.g = t;   out.b = p;   break;
        case 1: out.r = q;   out.g = c.v; out.b = p;   break;
        case 2: out.r = p;   out.g = c.v; out.b = t;   break;
        case 3: out.r = p;   out.g = q;   out.b = c.v; break;
        case 4: out.r = t;   out.g = p;   out.b = c.v; break;
        default: out.r = c.v; out.g = p;  out.b = q;   break;
    }
}

// ---------------------------------------------------------------------------
// CRGB ← CHSV (defined after hsv2rgb so it can call it)
// ---------------------------------------------------------------------------
inline CRGB::CRGB(const CHSV& hsv) { hsv2rgb(hsv, *this); }
inline CRGB& CRGB::operator=(const CHSV& hsv) { hsv2rgb(hsv, *this); return *this; }
inline const CRGB CRGB::Black{0, 0, 0};

// ---------------------------------------------------------------------------
// fill_solid
// ---------------------------------------------------------------------------
inline void fill_solid(CRGB* leds, uint16_t n, CRGB c) {
    for (uint16_t i = 0; i < n; i++) leds[i] = c;
}

// ---------------------------------------------------------------------------
// sin8 — sine mapped to [0, 255]; sin8(0)=128, sin8(64)=255, sin8(128)=128
// Matches FastLED's visible output closely enough for pattern purposes.
// ---------------------------------------------------------------------------
inline uint8_t sin8(uint8_t theta) {
    static constexpr float K = 6.28318530718f / 256.0f;
    float s = sinf(static_cast<float>(theta) * K);
    return static_cast<uint8_t>(128.0f + 127.5f * s);
}

// ---------------------------------------------------------------------------
// random8 / random16
// ---------------------------------------------------------------------------
inline uint8_t  random8()            { return static_cast<uint8_t>(rand() & 0xFF); }
inline uint16_t random16(uint16_t n) { return n > 0 ? static_cast<uint16_t>(rand() % n) : 0; }
