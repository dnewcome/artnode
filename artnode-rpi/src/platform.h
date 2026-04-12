#pragma once
#include <cstdint>
#include <ctime>

// millis() shim — matches Arduino semantics: monotonic ms since program start.
inline uint32_t millis() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}
