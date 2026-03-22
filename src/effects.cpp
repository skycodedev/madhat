#include <Arduino.h>
#include <FastLED.h>
#include "config.h"
#include "matrix.h"
#include "font5x7.h"
#include "effects.h"

extern CRGB leds[];

// ── Shared state union ────────────────────────────────────────────────────────
// All effect state lives here. Only one effect is active at a time, so they
// share the same RAM block. Switching modes calls resetEffect() which zeroes
// the union, cleanly initialising the next effect.

// ── Fire constants ────────────────────────────────────────────────────────────
#define FIRE_HALF   (MATRIX_W / 2)
#define FIRE_CELLS  (FIRE_HALF * MATRIX_H)
#define FIRE_BYTES  ((FIRE_CELLS + 1) / 2)   // 120 bytes, 4-bit packed

// ── Fire tuning ───────────────────────────────────────────────────────────────
#define INTERP_STEPS         2
#define FIRE_COOL_LO         1
#define FIRE_COOL_HI         2
#define FIRE_SEED_HOT        14
#define FIRE_SEED_HOT_CHANCE 2

// ── Fireworks constants ───────────────────────────────────────────────────────
#define MAX_FIREWORKS  4   // ← change to adjust simultaneous fireworks (max 4)
#define MAX_EMBERS     3

// ── State structs ─────────────────────────────────────────────────────────────

struct ScrollState {
    int16_t scrollOffset;
    uint8_t hueOffset;
};

struct PlasmaState {
    uint32_t t;
};

struct Ember {
    uint8_t x;
    int8_t  y;       // -1 = dead
    uint8_t heat;
    uint8_t ttl;
};

struct FireState {
    uint8_t heat[FIRE_BYTES];   // current frame
    uint8_t prev[FIRE_BYTES];   // previous frame (for interpolation)
    Ember   embers[MAX_EMBERS];
    uint8_t interpStep;
    bool    firstFrame;
};

enum FWState : uint8_t { FW_IDLE, FW_RISING, FW_EXPLODE };
enum FWShape : uint8_t { FW_CIRCLE, FW_CROSS };

struct Firework {
    FWState state;
    FWShape shape;
    uint8_t x, y, burstY, hue, radius, ttl;
    uint8_t trailX[3];
    uint8_t trailY[3];
};

struct FWState_ {
    Firework fw[MAX_FIREWORKS];
};

// ── The union itself ──────────────────────────────────────────────────────────
static union {
    ScrollState scroll;
    PlasmaState plasma;
    FireState   fire;
    FWState_    fireworks;
} S;

// Zero the union and set up any non-zero defaults for the incoming effect.
void resetEffect(uint8_t mode) {
    memset(&S, 0, sizeof(S));
    if (mode == 2) {
        // Fire: mark all embers as dead, set firstFrame
        for (uint8_t i = 0; i < MAX_EMBERS; i++) S.fire.embers[i].y = -1;
        S.fire.firstFrame = true;
    }
    // All other effects start correctly with zero-initialised state.
}

// ── Helpers ───────────────────────────────────────────────────────────────────

static inline int8_t isin8(uint8_t theta) { return (int8_t)(sin8(theta) - 128); }
static inline int8_t icos8(uint8_t theta) { return (int8_t)(cos8(theta) - 128); }

// ── Scroll text ───────────────────────────────────────────────────────────────

static void drawColumn(uint8_t screenX, char c, uint8_t charCol, uint8_t hueOffset) {
    if (c < 0x20 || c > 0x7A) return;
    uint8_t charIndex = c - 0x20;
    uint8_t colBits   = pgm_read_byte(&font5x7[charIndex][charCol]);
    CRGB    color     = CHSV((uint8_t)(hueOffset + screenX * 4), 255, 255);
    for (uint8_t row = 0; row < 7; row++) {
        if (colBits & (1 << row))
            leds[translatePixel(screenX, row)] = color;
    }
}

void effectScrollText(const char* text, bool resetScroll) {
    if (resetScroll) { S.scroll.scrollOffset = 0; return; }

    uint8_t textLen = strlen(text);
    int16_t textW   = textLen * 6;

    FastLED.clear();
    for (uint8_t screenX = 0; screenX < MATRIX_W; screenX++) {
        int16_t tilePos = ((int16_t)screenX + S.scroll.scrollOffset) % textW;
        if (tilePos < 0) tilePos += textW;
        uint8_t charIdx = tilePos / 6;
        uint8_t charCol = tilePos % 6;
        if (charCol < 5)
            drawColumn(screenX, text[charIdx], charCol, S.scroll.hueOffset);
    }
    FastLED.show();
    S.scroll.hueOffset    += 2;
    S.scroll.scrollOffset += 1;
    if (S.scroll.scrollOffset >= textW) S.scroll.scrollOffset = 0;
}

// ── Plasma ────────────────────────────────────────────────────────────────────

void effectPlasma() {
    for (uint8_t x = 0; x < MATRIX_W; x++) {
        uint8_t angle = (uint8_t)((uint16_t)x * 256 / MATRIX_W);
        int16_t cx    = (int16_t)icos8(angle) * 3 + 400;
        int16_t cy    = (int16_t)isin8(angle) * 3 + 400;
        for (uint8_t y = 0; y < MATRIX_H; y++) {
            uint16_t ny = y * 30;
            uint8_t  n1 = inoise8(cx,       cy + ny, (S.plasma.t >> 6) & 0xFFFF);
            uint8_t  n2 = inoise8(cx + 200, cy + ny, (S.plasma.t >> 5) & 0xFFFF);
            leds[translatePixel(x, y)] = CHSV(n1 / 2 + n2 / 2, 255, 255);
        }
    }
    FastLED.show();
    S.plasma.t += 100;
}

// ── Fire ──────────────────────────────────────────────────────────────────────

static inline uint8_t heatBufGet(const uint8_t* buf, uint8_t x, uint8_t y) {
    uint16_t i = (uint16_t)x * MATRIX_H + y;
    return (i & 1) ? (buf[i >> 1] & 0x0F) : (buf[i >> 1] >> 4);
}
static inline void heatBufSet(uint8_t* buf, uint8_t x, uint8_t y, uint8_t v) {
    uint16_t i = (uint16_t)x * MATRIX_H + y;
    if (i & 1) buf[i >> 1] = (buf[i >> 1] & 0xF0) | (v & 0x0F);
    else        buf[i >> 1] = (buf[i >> 1] & 0x0F) | ((v & 0x0F) << 4);
}
static inline uint8_t fireGet(uint8_t x, uint8_t y)            { return heatBufGet(S.fire.heat, x, y); }
static inline void    fireSet(uint8_t x, uint8_t y, uint8_t v) { heatBufSet(S.fire.heat, x, y, v); }

static CRGB heatToColor(uint8_t h) {
    if (h == 0)  return CRGB(0, 0, 0);
    if (h <= 4)  return CRGB(h * 28, 0, 0);
    if (h <= 9)  { uint8_t t = h-4;  return CRGB(112+t*24, t*10, 0); }
    if (h <= 13) { uint8_t t = h-10; return CRGB(232+t*7, 50+t*40, 0); }
    return CRGB(255, 210, 20);
}

static void spawnEmber() {
    for (uint8_t i = 0; i < MAX_EMBERS; i++) {
        if (S.fire.embers[i].y < 0) {
            S.fire.embers[i].x    = random8(FIRE_HALF);
            S.fire.embers[i].y    = MATRIX_H - 2;
            S.fire.embers[i].heat = random8(3, 7);
            S.fire.embers[i].ttl  = random8(1, 3);
            return;
        }
    }
}

static void updateEmbers() {
    for (uint8_t i = 0; i < MAX_EMBERS; i++) {
        if (S.fire.embers[i].y < 0) continue;
        if (--S.fire.embers[i].ttl == 0) {
            S.fire.embers[i].y--;
            S.fire.embers[i].x = (S.fire.embers[i].x + random8(0,3) - 1 + FIRE_HALF) % FIRE_HALF;
            S.fire.embers[i].ttl = random8(1, 3);
            if (S.fire.embers[i].y < 0) S.fire.embers[i].y = -1;
        }
    }
    if (random8(0, 8) == 0) spawnEmber();
}

void effectFire() {
    if (S.fire.interpStep == 0 || S.fire.firstFrame) {
        memcpy(S.fire.prev, S.fire.heat, FIRE_BYTES);
        for (uint8_t x = 0; x < FIRE_HALF; x++) {
            if (random8(FIRE_SEED_HOT_CHANCE) == 0)
                fireSet(x, MATRIX_H - 1, random8(8, FIRE_SEED_HOT + 1));
            else
                fireSet(x, MATRIX_H - 1, random8(1, 3));
        }
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
        S.fire.firstFrame = false;
    }

    uint8_t blend = (uint8_t)(((uint16_t)(S.fire.interpStep + 1) * 256) / INTERP_STEPS);
    for (uint8_t hx = 0; hx < FIRE_HALF; hx++) {
        for (uint8_t y = 0; y < MATRIX_H; y++) {
            uint8_t hBlend = lerp8by8(heatBufGet(S.fire.prev, hx, y), fireGet(hx, y), blend);
            CRGB col = heatToColor(hBlend);
            leds[translatePixel(FIRE_HALF - 1 - hx, y)] = col;
            leds[translatePixel(FIRE_HALF     + hx, y)] = col;
        }
    }
    for (uint8_t i = 0; i < MAX_EMBERS; i++) {
        if (S.fire.embers[i].y < 0 || S.fire.embers[i].y >= MATRIX_H) continue;
        uint8_t hx  = S.fire.embers[i].x % FIRE_HALF;
        uint8_t y   = S.fire.embers[i].y;
        CRGB    col = heatToColor(S.fire.embers[i].heat);
        leds[translatePixel(FIRE_HALF - 1 - hx, y)] = col;
        leds[translatePixel(FIRE_HALF     + hx, y)] = col;
    }
    FastLED.show();
    S.fire.interpStep = (S.fire.interpStep + 1) % INTERP_STEPS;
}

// ── Fireworks ─────────────────────────────────────────────────────────────────

static void fwLaunch(Firework& fw) {
    fw.state  = FW_RISING;
    fw.x      = random8(MATRIX_W);
    fw.y      = MATRIX_H - 1;
    fw.burstY = random8(0, MATRIX_H / 2 + 1);
    fw.hue    = random8();
    fw.shape  = (random8(2) == 0) ? FW_CIRCLE : FW_CROSS;
    fw.radius = 0;
    fw.ttl    = 2;
    for (uint8_t i = 0; i < 3; i++) { fw.trailX[i] = fw.x; fw.trailY[i] = fw.y; }
}

static void fwSetPixel(uint8_t x, uint8_t y, CRGB col) {
    if (y >= MATRIX_H) return;
    leds[translatePixel(x % MATRIX_W, y)] = col;
}

static void fwUpdate(Firework& fw) {
    if (fw.ttl > 0) { fw.ttl--; return; }
    if (fw.state == FW_RISING) {
        for (uint8_t i = 2; i > 0; i--) { fw.trailX[i] = fw.trailX[i-1]; fw.trailY[i] = fw.trailY[i-1]; }
        fw.trailX[0] = fw.x; fw.trailY[0] = fw.y;
        fw.y--;
        fw.ttl = 2;
        if (fw.y <= fw.burstY) { fw.state = FW_EXPLODE; fw.radius = 0; fw.ttl = 3; }
    } else if (fw.state == FW_EXPLODE) {
        fw.radius++;
        fw.ttl = 4;
        if (fw.radius > 4) fw.state = FW_IDLE;
    }
}

static void fwDraw(const Firework& fw) {
    if (fw.state == FW_IDLE) return;
    if (fw.state == FW_RISING) {
        fwSetPixel(fw.x, fw.y, CRGB::White);
        CRGB trail[3] = { CRGB(200,150,0), CRGB(150,60,0), CRGB(60,20,0) };
        for (uint8_t i = 0; i < 3; i++) fwSetPixel(fw.trailX[i], fw.trailY[i], trail[i]);
    }
    if (fw.state == FW_EXPLODE) {
        uint8_t r   = fw.radius;
        uint8_t bri = (r==0)?255:(r==1)?200:(r==2)?150:(r==3)?90:50;
        CRGB    col = CHSV(fw.hue, 255, bri);
        if (fw.shape == FW_CIRCLE) {
            if (r == 0) { fwSetPixel(fw.x, fw.burstY, col); }
            else {
                fwSetPixel((fw.x + r) % MATRIX_W,              fw.burstY, col);
                fwSetPixel((fw.x + MATRIX_W - r) % MATRIX_W,   fw.burstY, col);
                if (fw.burstY >= r)           fwSetPixel(fw.x, fw.burstY - r, col);
                if (fw.burstY + r < MATRIX_H) fwSetPixel(fw.x, fw.burstY + r, col);
                uint8_t d = (r + 1) / 2;
                if (fw.burstY >= d) {
                    fwSetPixel((fw.x + d) % MATRIX_W,            fw.burstY - d, col);
                    fwSetPixel((fw.x + MATRIX_W - d) % MATRIX_W, fw.burstY - d, col);
                }
                if (fw.burstY + d < MATRIX_H) {
                    fwSetPixel((fw.x + d) % MATRIX_W,            fw.burstY + d, col);
                    fwSetPixel((fw.x + MATRIX_W - d) % MATRIX_W, fw.burstY + d, col);
                }
            }
        } else {
            for (uint8_t i = 0; i <= r; i++) {
                fwSetPixel((fw.x + i) % MATRIX_W,             fw.burstY, col);
                fwSetPixel((fw.x + MATRIX_W - i) % MATRIX_W,  fw.burstY, col);
                if (fw.burstY >= i)           fwSetPixel(fw.x, fw.burstY - i, col);
                if (fw.burstY + i < MATRIX_H) fwSetPixel(fw.x, fw.burstY + i, col);
            }
        }
    }
}

void effectFireworks() {
    for (uint8_t i = 0; i < MAX_FIREWORKS; i++)
        if (S.fireworks.fw[i].state == FW_IDLE && random8(20) == 0)
            fwLaunch(S.fireworks.fw[i]);

    for (uint16_t i = 0; i < NUM_LEDS; i++) leds[i].nscale8(200);

    for (uint8_t i = 0; i < MAX_FIREWORKS; i++) {
        fwUpdate(S.fireworks.fw[i]);
        fwDraw(S.fireworks.fw[i]);
    }
    FastLED.show();
}

// ── GIF image playback (disabled — uncomment to re-enable) ──────────────────
// void effectImage(const uint32_t* frameData,
//                  uint8_t         nFrames,
//                  uint8_t         imgW,
//                  uint8_t         imgH,
//                  uint16_t        delayMs,
//                  bool            reset) { ... }
