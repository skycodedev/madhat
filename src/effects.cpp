#include <Arduino.h>
#include <FastLED.h>
#include "config.h"
#include "matrix.h"
#include "font5x7.h"
#include "effects.h"

extern CRGB leds[];

// ── Scroll text (cylindrical wrap) ───────────────────────────────────────────
// The display is a cylinder: column 0 and column 59 are physical neighbours.
// Text is tiled end-to-end and rendered modulo MATRIX_W so it wraps seamlessly.
// scrollOffset advances each frame; when it reaches textW it wraps back to 0.

static void drawColumn(uint8_t screenX, char c, uint8_t charCol, uint8_t hueOffset) {
    if (c < 0x20 || c > 0x7A) return;
    uint8_t charIndex = c - 0x20;
    uint8_t colBits   = pgm_read_byte(&font5x7[charIndex][charCol]);
    CRGB    color     = CHSV((uint8_t)(hueOffset + screenX * 4), 255, 255);
    for (uint8_t row = 0; row < 7; row++) {
        if (colBits & (1 << row)) {
            leds[translatePixel(screenX, row)] = color;
        }
    }
}

void effectScrollText(const char* text, bool resetScroll) {
    static int16_t scrollOffset = 0;
    static uint8_t hueOffset    = 0;

    if (resetScroll) {
        scrollOffset = 0;
        return;
    }

    uint8_t textLen = strlen(text);
    int16_t textW   = textLen * 6;

    FastLED.clear();

    for (uint8_t screenX = 0; screenX < MATRIX_W; screenX++) {
        int16_t tilePos = ((int16_t)screenX + scrollOffset) % textW;
        if (tilePos < 0) tilePos += textW;

        uint8_t charIdx = tilePos / 6;
        uint8_t charCol = tilePos % 6;

        if (charCol < 5) {
            drawColumn(screenX, text[charIdx], charCol, hueOffset);
        }
    }

    FastLED.show();

    hueOffset    += 2;
    scrollOffset += 1;
    if (scrollOffset >= textW) scrollOffset = 0;
}

// ── Plasma ────────────────────────────────────────────────────────────────────

static inline int8_t isin8(uint8_t theta) { return (int8_t)(sin8(theta) - 128); }
static inline int8_t icos8(uint8_t theta) { return (int8_t)(cos8(theta) - 128); }

void effectPlasma() {
    static uint32_t t = 0;

    for (uint8_t x = 0; x < MATRIX_W; x++) {
        uint8_t angle = (uint8_t)((uint16_t)x * 256 / MATRIX_W);
        int16_t cx    = (int16_t)icos8(angle) * 3 + 400;
        int16_t cy    = (int16_t)isin8(angle) * 3 + 400;

        for (uint8_t y = 0; y < MATRIX_H; y++) {
            uint16_t ny = y * 30;
            uint8_t  n1 = inoise8(cx,       cy + ny, (t >> 6) & 0xFFFF);
            uint8_t  n2 = inoise8(cx + 200, cy + ny, (t >> 5) & 0xFFFF);
            leds[translatePixel(x, y)] = CHSV(n1 / 2 + n2 / 2, 255, 255);
        }
    }
    FastLED.show();
    t += 100;
}

// ── Fireplace ────────────────────────────────────────────────────────────────
// Classic DOOM-style fire simulation.
// A heat buffer (0-255) is maintained per pixel.
// Each frame:
//   1. Bottom row is seeded with full heat (with small random dips for flicker).
//   2. Each pixel cools slightly and its heat drifts upward from the row below.
// Heat values are mapped: black -> red -> orange -> yellow -> white

static uint8_t fireHeat[MATRIX_W][MATRIX_H];

static CRGB heatToColor(uint8_t heat) {
    if (heat < 86) {
        return CRGB(heat * 3, 0, 0);                          // black -> red
    } else if (heat < 171) {
        uint8_t t = heat - 86;
        return CRGB(255, t * 3, 0);                           // red -> orange/yellow
    } else {
        uint8_t t = heat - 171;
        return CRGB(255, 255, t * 3);                         // yellow -> white
    }
}

void effectFire() {
    // Seed bottom row with near-full heat, small random dips for flicker
    for (uint8_t x = 0; x < MATRIX_W; x++) {
        fireHeat[x][MATRIX_H - 1] = random8(200, 255);
    }

    // Diffuse heat upward — average from the row below with horizontal spread,
    // subtract a small random cooling amount per step
    for (uint8_t x = 0; x < MATRIX_W; x++) {
        for (uint8_t y = 0; y < MATRIX_H - 1; y++) {
            uint8_t left  = fireHeat[(x + MATRIX_W - 1) % MATRIX_W][y + 1];
            uint8_t mid   = fireHeat[x][y + 1];
            uint8_t right = fireHeat[(x + 1) % MATRIX_W][y + 1];
            uint16_t avg  = ((uint16_t)left + mid + right) / 3;
            uint8_t cooling = random8(0, 3);
            fireHeat[x][y] = (avg > cooling) ? avg - cooling : 0;
        }
    }

    // Render heat buffer to LEDs
    for (uint8_t x = 0; x < MATRIX_W; x++) {
        for (uint8_t y = 0; y < MATRIX_H; y++) {
            leds[translatePixel(x, y)] = heatToColor(fireHeat[x][y]);
        }
    }
    FastLED.show();
}

// ── GIF image playback (disabled — uncomment to re-enable) ──────────────────
// void effectImage(const uint32_t* frameData,
//                  uint8_t         nFrames,
//                  uint8_t         imgW,
//                  uint8_t         imgH,
//                  uint16_t        delayMs,
//                  bool            reset) {
//     static uint8_t  currentFrame = 0;
//     static uint32_t lastFrameMs  = 0;
//     if (reset) { currentFrame = 0; lastFrameMs = 0; return; }
//     uint32_t now = millis();
//     if (now - lastFrameMs < delayMs) return;
//     lastFrameMs = now;
//     const uint32_t* frame = frameData + (uint32_t)currentFrame * imgW * imgH;
//     FastLED.clear();
//     uint8_t drawW = min(imgW, (uint8_t)MATRIX_W);
//     uint8_t drawH = min(imgH, (uint8_t)MATRIX_H);
//     for (uint8_t y = 0; y < drawH; y++) {
//         for (uint8_t x = 0; x < drawW; x++) {
//             uint32_t px = pgm_read_dword(frame + y * imgW + x);
//             leds[translatePixel(x, y)] = CRGB((px>>16)&0xFF, (px>>8)&0xFF, px&0xFF);
//         }
//     }
//     FastLED.show();
//     currentFrame++;
//     if (currentFrame >= nFrames) currentFrame = 0;
// }
