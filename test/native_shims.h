// native_shims.h — minimal Arduino/FastLED stubs for host builds.
//
// Include this before any project header in every test file.
// These stubs let test files compile and run on your PC without any hardware.
// They satisfy the type system only — no hardware behaviour is emulated.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <algorithm>

// ── Arduino primitive type aliases ────────────────────────────────────────────
using byte = uint8_t;

// ── Arduino math helpers ──────────────────────────────────────────────────────
// constrain: clamp v to [lo, hi]
template<typename T>
static inline T constrain(T v, T lo, T hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

// min/max: forwarded to std:: so they work on mixed arithmetic types
using std::min;
using std::max;

// ── Seeded random helpers ─────────────────────────────────────────────────────
// random8(lo, hi): uniform in [lo, hi). Non-cryptographic; seed with srand()
// in setUp() for deterministic tests.
static inline uint8_t random8(uint8_t lo, uint8_t hi) {
    if (hi <= lo) return lo;
    return (uint8_t)(lo + (unsigned)rand() % (hi - lo));
}
static inline uint8_t random8(uint8_t n) { return n == 0 ? 0 : random8(0, n); }
static inline uint8_t random8()          { return random8(0, 255); }

// ── Minimal CRGB stub ─────────────────────────────────────────────────────────
struct CRGB {
    uint8_t r, g, b;
    CRGB() : r(0), g(0), b(0) {}
    CRGB(uint8_t r_, uint8_t g_, uint8_t b_) : r(r_), g(g_), b(b_) {}
    bool operator==(const CRGB& o) const { return r==o.r && g==o.g && b==o.b; }
    bool operator!=(const CRGB& o) const { return !(*this == o); }
    static const CRGB Black;
    static const CRGB White;
};
// Definitions placed inline so including this header in multiple test files is safe
inline const CRGB CRGB::Black = CRGB(0,   0,   0);
inline const CRGB CRGB::White = CRGB(255, 255, 255);

// ── FastLED CHSV stub ─────────────────────────────────────────────────────────
struct CHSV {
    uint8_t h, s, v;
    CHSV() : h(0), s(0), v(0) {}
    CHSV(uint8_t h_, uint8_t s_, uint8_t v_) : h(h_), s(s_), v(v_) {}
    // Approximate conversion — adequate for bounds tests (not colour accuracy)
    operator CRGB() const { return CRGB(v, v, v); }
};

// ── FastLED global object stub ────────────────────────────────────────────────
struct _FastLED_t {
    void clear() {}
    void show()  {}
    void setBrightness(uint8_t) {}
    uint8_t getBrightness() const { return 0; }
};
// Single global instance matching the FastLED library's API
inline _FastLED_t FastLED;

// ── Arduino.h guard ───────────────────────────────────────────────────────────
// Prevents "file not found" if a project header includes <Arduino.h>.
// The guard makes that include a no-op on the host build.
#ifndef ARDUINO_H
#define ARDUINO_H
#endif
