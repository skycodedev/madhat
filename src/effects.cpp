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
// DOOM-style fire, simulated on HALF the display width (30 cols) then
// mirrored symmetrically to the other half so both visible sides of the
// cylinder match. Seams at col 0 and col 30 blend naturally because the
// diffusion kernel wraps within the half-buffer.
// Halving the sim width saves 120 bytes, giving room for firePrev.
//
// RAM budget:
//   leds[480]  = 1440 B
//   fireHeat   =  120 B  (30*8 / 2, 4-bit packed)
//   firePrev   =  120 B
//   embers x3  =   12 B
//   stack+misc = ~150 B
//   Total      = 1842 B  (free: 206 B)

#define FIRE_HALF   (MATRIX_W / 2)                    // 30 columns simulated
#define FIRE_CELLS  (FIRE_HALF * MATRIX_H)             // 240 logical cells
#define FIRE_BYTES  ((FIRE_CELLS + 1) / 2)             // 120 bytes each buffer

static uint8_t fireHeat[FIRE_BYTES];   // current frame  (30*8 half-display)
static uint8_t firePrev[FIRE_BYTES];   // previous frame

static inline uint8_t heatBufGet(const uint8_t* buf, uint8_t x, uint8_t y) {
    uint16_t i = (uint16_t)x * MATRIX_H + y;
    return (i & 1) ? (buf[i >> 1] & 0x0F) : (buf[i >> 1] >> 4);
}
static inline void heatBufSet(uint8_t* buf, uint8_t x, uint8_t y, uint8_t v) {
    uint16_t i = (uint16_t)x * MATRIX_H + y;
    if (i & 1) buf[i >> 1] = (buf[i >> 1] & 0xF0) | (v & 0x0F);
    else        buf[i >> 1] = (buf[i >> 1] & 0x0F) | ((v & 0x0F) << 4);
}
static inline uint8_t fireGet(uint8_t x, uint8_t y)            { return heatBufGet(fireHeat, x, y); }
static inline void    fireSet(uint8_t x, uint8_t y, uint8_t v) { heatBufSet(fireHeat, x, y, v); }

// Mirror a half-display column (0..29) to its full-display column on each side.
// The half-buffer is laid out as a straight strip (col 0=seam, col 29=centre).
// Mirrored:  left side  col  29-x  maps to full col x
//            right side col  30+x  maps to full col x  (x=0..29)
static inline uint8_t mirrorX(uint8_t hx) {
    // hx 0..29 — col 0 is the seam, col 29 is the centre of the visible face
    return hx;  // used directly; rendering applies both sides below
}

// ── Colour palette ───────────────────────────────────────────────────────────
static CRGB heatToColor(uint8_t heat4) {
    if (heat4 == 0)  return CRGB(0, 0, 0);
    if (heat4 <= 4)  return CRGB(heat4 * 28, 0, 0);              // black -> dark red
    if (heat4 <= 9)  { uint8_t t = heat4-4; return CRGB(112+t*24, t*10, 0); } // dark red -> orange
    if (heat4 <= 13) { uint8_t t = heat4-10; return CRGB(232+t*7, 50+t*40, 0); } // orange -> yellow
    return CRGB(255, 210, 20);                                    // yellow-white tip
}

// ── Ember particles ───────────────────────────────────────────────────────────
#define MAX_EMBERS 5
struct Ember {
    uint8_t  x;       // column (wraps)
    int8_t   y;       // row, -1 = off screen (dead)
    uint8_t  heat;    // 1-6, fades each step
    uint8_t  ttl;     // frames until next move
};
static Ember embers[MAX_EMBERS];
static bool  embersInit = false;

static void spawnEmber() {
    // Find a dead slot
    for (uint8_t i = 0; i < MAX_EMBERS; i++) {
        if (embers[i].y < 0) {
            embers[i].x    = random8(FIRE_HALF);   // half-space, mirrored on render
            embers[i].y    = MATRIX_H - 2;   // start just above bottom
            embers[i].heat = random8(3, 7);
            embers[i].ttl  = random8(1, 3);
            return;
        }
    }
}

static void updateEmbers() {
    for (uint8_t i = 0; i < MAX_EMBERS; i++) {
        if (embers[i].y < 0) continue;
        if (--embers[i].ttl == 0) {
            embers[i].y--;                              // drift upward
            embers[i].x = (embers[i].x + random8(0,3) - 1 + FIRE_HALF) % FIRE_HALF;
            embers[i].ttl  = random8(1, 3);
            if (embers[i].y < 0)
                embers[i].y = -1;                       // flew off the top — kill
        }
    }
    // Randomly spawn new embers
    if (random8(0, 8) == 0) spawnEmber();
}

// ── Fire tuning ────────────────────────────────────────────────────────────────
// INTERP_STEPS : display frames between sim ticks. Higher = smoother & slower.
// FIRE_COOL_LO  : minimum cooling subtracted per row per tick (heat units 0-15).
// FIRE_COOL_HI  : maximum cooling (random between LO and HI each pixel).
//                 Higher values = shorter, darker flame.
//                 Recommended range: LO 1-3, HI 2-6.
#define INTERP_STEPS  4
#define FIRE_COOL_LO  1   // min cooling per pixel per tick
#define FIRE_COOL_HI  2   // max cooling — lower = taller tongues
#define FIRE_SEED_HOT 14  // max heat seeded at bottom (was 15 = always white)
#define FIRE_SEED_HOT_CHANCE 2  // 1-in-N chance a column gets a hot spot (rest stay cold)

void effectFire() {
    static uint8_t interpStep = 0;
    static bool    firstFrame = true;

    if (!embersInit) {
        for (uint8_t i = 0; i < MAX_EMBERS; i++) embers[i].y = -1;
        embersInit = true;
    }

    // ── Advance simulation (half-width) once per INTERP_STEPS calls ─────────
    if (interpStep == 0 || firstFrame) {
        memcpy(firePrev, fireHeat, FIRE_BYTES);

        // Seed bottom row — random hot patches with frequent cold gaps
        // so distinct flame tongues form rather than a solid white bar
        for (uint8_t x = 0; x < FIRE_HALF; x++) {
            if (random8(FIRE_SEED_HOT_CHANCE) == 0)
                fireSet(x, MATRIX_H - 1, random8(8, FIRE_SEED_HOT + 1)); // hot spot
            else
                fireSet(x, MATRIX_H - 1, random8(1, 3));                  // cold base
        }
        // Diffuse upward — wraps at x=0 and x=FIRE_HALF-1 (the two seam edges)
        for (uint8_t x = 0; x < FIRE_HALF; x++) {
            for (uint8_t y = 0; y < MATRIX_H - 1; y++) {
                uint8_t left  = fireGet((x + FIRE_HALF - 1) % FIRE_HALF, y + 1);
                uint8_t mid   = fireGet(x,                               y + 1);
                uint8_t right = fireGet((x + 1)             % FIRE_HALF, y + 1);
                uint8_t avg   = ((uint16_t)left + mid + right) / 3;
                uint8_t cool  = random8(FIRE_COOL_LO, FIRE_COOL_HI + 1);
                fireSet(x, y, (avg > cool) ? avg - cool : 0);
            }
        }
        updateEmbers();
        firstFrame = false;
    }

    // ── Interpolate and render ─────────────────────────────────────────────
    uint8_t blend = (uint8_t)(((uint16_t)(interpStep + 1) * 256) / INTERP_STEPS);

    for (uint8_t hx = 0; hx < FIRE_HALF; hx++) {
        for (uint8_t y = 0; y < MATRIX_H; y++) {
            uint8_t hBlend = lerp8by8(
                heatBufGet(firePrev, hx, y),
                fireGet(hx, y),
                blend
            );
            CRGB col = heatToColor(hBlend);

            // Left half:  hx=0 is the seam (col 0 & 59 meet here)
            //             hx=29 is the centre of the visible face (col 29)
            // Full col for left side  = FIRE_HALF - 1 - hx  (col 29..0)
            // Full col for right side = FIRE_HALF + hx       (col 30..59)
            leds[translatePixel(FIRE_HALF - 1 - hx, y)] = col;
            leds[translatePixel(FIRE_HALF     + hx, y)] = col;
        }
    }

    // Embers — mirror them too
    for (uint8_t i = 0; i < MAX_EMBERS; i++) {
        if (embers[i].y < 0 || embers[i].y >= MATRIX_H) continue;
        // ember x is in half-space (0..FIRE_HALF-1)
        uint8_t hx  = embers[i].x % FIRE_HALF;
        uint8_t y   = embers[i].y;
        CRGB    col = heatToColor(embers[i].heat);
        leds[translatePixel(FIRE_HALF - 1 - hx, y)] = col;
        leds[translatePixel(FIRE_HALF     + hx, y)] = col;
    }

    FastLED.show();
    interpStep = (interpStep + 1) % INTERP_STEPS;
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
