#include <Arduino.h>
#include <FastLED.h>
#include "config.h"
#include "matrix.h"
#include "font5x7.h"
#include "effects.h"
#include "audio.h"

extern CRGB leds[];

// =============================================================================
// State structs — all effect state lives in a single union so only the
// active effect uses RAM.  resetEffect() zeroes the union on mode switch.
//
// Union member sizes (approximate):
//   ScrollState   ~   3 bytes
//   PlasmaState   ~   4 bytes
//   EqState       ~ 601 bytes  (60 floats + 60 floats + 60 bytes)
//   FireState     ~ 484 bytes  (480 bytes heat + embers)
//   FireworksState~ 150 bytes  (5 × Firework)
// The union is sized by the largest member (EqState).
// =============================================================================

// ── Fire constants ─────────────────────────────────────────────────────────────
#define FIRE_W               MATRIX_W
#define FIRE_H               MATRIX_H
// Cooling values are already in final scale (applied directly as heat-units/step)
#define FIRE_COOL_LO         3    // minimum cooling per cell per frame
#define FIRE_COOL_HI         8    // maximum cooling per cell per frame
#define FIRE_RISE            1    // rows sampled below when propagating heat upward
                                  // 1 = normal rise speed, 2-3 = faster/taller flame
#define FIRE_RISE_EVERY      20    // propagate heat only 1-in-N frames
                                  // 1 = every frame (fastest rise)
                                  // 3 = every 3rd frame
                                  // 5 = every 5th frame (slowest rise)
#define FIRE_SEED_HOT        240  // peak heat value for a "hot" seed pixel
#define FIRE_SEED_HOT_CHANCE 16    // 1-in-N chance of spawning a hot seed pixel
#define MAX_EMBERS           8
#define EMBER_TTL_LO         8    // minimum frames between ember moves (6× slower than before)
#define EMBER_TTL_HI         40   // maximum frames between ember moves

// ── Scroll constants ───────────────────────────────────────────────────────────
// Characters in the 5×7 font range from 0x20 (space) to FONT_LAST_CHAR.
// Matches the last entry in font5x7[] — update both together if the font grows.
#define FONT_LAST_CHAR  0x7A   // lowercase 'z'
// Maximum safe text length before uint8_t charIdx would overflow (6 px/char)
#define SCROLL_MAX_LEN  42

// Hue shift per screen-pixel column (controls rainbow spread across text width)
#define SCROLL_HUE_SPREAD  4

// ── Plasma constants ───────────────────────────────────────────────────────────
#define PLASMA_SCALE    3    // amplitude of the cx/cy orbital (noise-coord units)
#define PLASMA_OFFSET   400  // DC offset to keep noise coords positive
#define PLASMA_NY_STEP  30   // vertical spacing in noise coordinate space
// Bit-shift amounts that convert the 32-bit time counter to noise speed inputs.
// Larger shift = slower animation on that axis.
#define PLASMA_T_SHIFT_X  6
#define PLASMA_T_SHIFT_Y  5

// ── Equalizer constants ────────────────────────────────────────────────────────
// EQ_BANDS comes from audio.h (== MATRIX_W == 60)
#define EQ_PEAK_HOLD    20     // frames the peak dot stays before falling
#define EQ_PEAK_FALL    0.04f  // peak dot drop rate per frame (fraction 0.0-1.0)
#define EQ_HUE_MAX      192   // hue at the treble end (violet on HSV wheel)
#define EQ_BRI_MIN      150   // brightness at the bottom of a bar
#define EQ_BRI_RANGE    105   // brightness added from bottom to top (min+range≤255)

// ── Fireworks constants ────────────────────────────────────────────────────────
#define MAX_FIREWORKS          5
#define FW_LAUNCH_CHANCE       20  // 1-in-N chance per idle slot per frame
#define FW_FADE_AMOUNT         55  // passed to fadeToBlackBy each frame (0-255)

// =============================================================================
// Structs
// =============================================================================

struct ScrollState {
    int16_t scrollOffset;
    uint8_t hueOffset;
};

struct PlasmaState {
    uint32_t t;
};

struct Ember {
    uint8_t x;
    int8_t  y;
    uint8_t heat;
    uint8_t ttl;
};

struct FireState {
    uint8_t heat[FIRE_W * FIRE_H];
    Ember   embers[MAX_EMBERS];
    uint8_t riseCounter;   // counts up to FIRE_RISE_EVERY; propagation runs when it wraps
};

// Renamed from FWState (conflict with enum) → FireworksState
// Enum for firework lifecycle phase renamed from FWState → FireworkPhase
enum FireworkPhase : uint8_t { FW_IDLE, FW_RISING, FW_EXPLODE };
enum FWShape       : uint8_t { FW_CIRCLE, FW_CROSS };

struct Firework {
    FireworkPhase state;
    FWShape shape;
    uint8_t x;
    int8_t  y;
    uint8_t burstY, hue, radius, ttl;
    uint8_t trailX[3];
    uint8_t trailY[3];
};

struct FireworksState {
    Firework fw[MAX_FIREWORKS];
};

struct EqState {
    float   bands[EQ_BANDS];    // smoothed bar height 0.0-1.0
    float   peaks[EQ_BANDS];    // peak-hold dot height 0.0-1.0
    uint8_t peakTtl[EQ_BANDS];  // frames until peak dot starts falling
};

// Single union for all effect state — only the active mode's member is live.
static union {
    ScrollState    scroll;
    PlasmaState    plasma;
    FireState      fire;
    FireworksState fireworks;
    EqState        eq;
} gState;

void resetEffect(uint8_t mode) {
    memset(&gState, 0, sizeof(gState));
    if (mode == 2) {
        for (uint8_t i = 0; i < MAX_EMBERS; i++) gState.fire.embers[i].y = -1;
    }
}

// =============================================================================
// Helpers
// =============================================================================

// isin8 / icos8: signed [-128, +127] wrappers around FastLED's unsigned
// sin8/cos8 (which return [0, 255]).  Used for smooth orbital motion.
static inline int8_t isin8(uint8_t theta) { return (int8_t)(sin8(theta) - 128); }
static inline int8_t icos8(uint8_t theta) { return (int8_t)(cos8(theta) - 128); }

// =============================================================================
// Mode 0: Scroll text
// =============================================================================

static void drawColumn(uint8_t screenX, char c, uint8_t charCol, uint8_t hueOffset) {
    if (c < 0x20 || c > FONT_LAST_CHAR) return;
    uint8_t charIndex = c - 0x20;
    uint8_t colBits   = font5x7[charIndex][charCol];
    CRGB    color     = CHSV((uint8_t)(hueOffset + screenX * SCROLL_HUE_SPREAD), 255, 255);
    for (uint8_t row = 0; row < 7; row++) {
        if (colBits & (1 << row))
            leds[translatePixel(screenX, row)] = color;
    }
}

void effectScrollText(const char* text, bool resetScroll) {
    if (resetScroll) { gState.scroll.scrollOffset = 0; return; }

    // Cap text length to avoid uint8_t charIdx overflow and out-of-bounds reads.
    // Characters beyond SCROLL_MAX_LEN are silently ignored.
    size_t  textLen = strnlen(text, SCROLL_MAX_LEN);
    int16_t textW   = (int16_t)textLen * 6;
    if (textW == 0) return;

    FastLED.clear();
    for (uint8_t screenX = 0; screenX < MATRIX_W; screenX++) {
        int16_t tilePos = ((int16_t)screenX + gState.scroll.scrollOffset) % textW;
        if (tilePos < 0) tilePos += textW;
        uint8_t charIdx = (uint8_t)(tilePos / 6);
        uint8_t charCol = (uint8_t)(tilePos % 6);
        if (charCol < 5)
            drawColumn(screenX, text[charIdx], charCol, gState.scroll.hueOffset);
    }
    FastLED.show();
    gState.scroll.hueOffset    += 2;
    gState.scroll.scrollOffset += 1;
    if (gState.scroll.scrollOffset >= textW) gState.scroll.scrollOffset = 0;
}

// =============================================================================
// Mode 1: Plasma
// =============================================================================

void effectPlasma() {
    for (uint8_t x = 0; x < MATRIX_W; x++) {
        uint8_t angle = (uint8_t)((uint16_t)x * 256 / MATRIX_W);
        // cx/cy: centre of the noise-coordinate orbit for this column.
        // PLASMA_OFFSET keeps coordinates positive so inoise8 doesn't wrap.
        int16_t cx    = (int16_t)icos8(angle) * PLASMA_SCALE + PLASMA_OFFSET;
        int16_t cy    = (int16_t)isin8(angle) * PLASMA_SCALE + PLASMA_OFFSET;
        for (uint8_t y = 0; y < MATRIX_H; y++) {
            uint16_t ny = y * PLASMA_NY_STEP;
            uint8_t  n1 = inoise8(cx,                cy + ny,
                                  (gState.plasma.t >> PLASMA_T_SHIFT_X) & 0xFFFF);
            uint8_t  n2 = inoise8(cx + PLASMA_OFFSET, cy + ny,
                                  (gState.plasma.t >> PLASMA_T_SHIFT_Y) & 0xFFFF);
            leds[translatePixel(x, y)] = CHSV(n1 / 2 + n2 / 2, 255, 255);
        }
    }
    FastLED.show();
    gState.plasma.t += 100;
}

// =============================================================================
// Mode 2: Fire
// =============================================================================

static inline uint8_t fireGet(uint8_t x, uint8_t y) { return gState.fire.heat[x * FIRE_H + y]; }
static inline void    fireSet(uint8_t x, uint8_t y, uint8_t v) { gState.fire.heat[x * FIRE_H + y] = v; }

// Maps a heat value (0-255) to a colour across 12 gradient bands.
//
//  Band  Range    Transition
//   1    1– 21    black → very dark red
//   2   22– 42    very dark red → dark red
//   3   43– 63    dark red → red
//   4   64– 84    red → red-orange
//   5   85–105    red-orange → orange
//   6  106–126    orange → deep orange
//   7  127–147    deep orange → light orange
//   8  148–168    light orange → yellow-orange
//   9  169–189    yellow-orange → warm yellow
//  10  190–210    warm yellow → yellow
//  11  211–231    yellow → yellow-white
//  12  232–255    yellow-white → white-hot
static CRGB heatToColor(uint8_t h) {
    if (h == 0) return CRGB(0, 0, 0);

    // Each band is 21 heat-units wide; t is position within that band (0–20).
    if (h < 22)  { uint8_t t = h - 1;   return CRGB(t * 5,          0,       0); }   // black → very dark red
    if (h < 43)  { uint8_t t = h - 22;  return CRGB(105 + t * 5,    0,       0); }   // very dark red → dark red
    if (h < 64)  { uint8_t t = h - 43;  return CRGB(210 + t,        0,       0); }   // dark red → red
    if (h < 85)  { uint8_t t = h - 64;  return CRGB(230 + t / 5,    t * 3,   0); }   // red → red-orange
    if (h < 106) { uint8_t t = h - 85;  return CRGB(234 + t / 10,   60 + t * 4, 0); } // red-orange → orange
    if (h < 127) { uint8_t t = h - 106; return CRGB(236 + t / 10,   140 + t * 3, 0); } // orange → deep orange
    if (h < 148) { uint8_t t = h - 127; return CRGB(238 + t / 10,   203 + t, 0); }   // deep orange → light orange
    if (h < 169) { uint8_t t = h - 148; return CRGB(240 + t / 10,   223 + t / 4, 0); } // light orange → yellow-orange
    if (h < 190) { uint8_t t = h - 169; return CRGB(242 + t / 10,   228 + t / 4, 0); } // yellow-orange → warm yellow
    if (h < 211) { uint8_t t = h - 190; return CRGB(246 + t / 10,   233 + t / 4, 0); } // warm yellow → yellow
    if (h < 232) { uint8_t t = h - 211; return CRGB(250 + t / 20,   238 + t / 4, t * 3); } // yellow → yellow-white
    {              uint8_t t = h - 232; return CRGB(255,             243 + t / 5, 60 + t * 8); } // yellow-white → white-hot
}

static void spawnEmber() {
    for (uint8_t i = 0; i < MAX_EMBERS; i++) {
        if (gState.fire.embers[i].y < 0) {
            gState.fire.embers[i].x    = random8(FIRE_W);
            gState.fire.embers[i].y    = FIRE_H - 2;
            gState.fire.embers[i].heat = random8(80, 160);
            gState.fire.embers[i].ttl  = random8(EMBER_TTL_LO, EMBER_TTL_HI + 1);
            return;
        }
    }
}

static void updateEmbers() {
    for (uint8_t i = 0; i < MAX_EMBERS; i++) {
        if (gState.fire.embers[i].y < 0) continue;
        if (--gState.fire.embers[i].ttl == 0) {
            gState.fire.embers[i].y--;
            // Cast to int16_t before subtracting 1 so that random8()==0 does not
            // cause uint8_t underflow (0 - 1 would wrap to 255, then % FIRE_W
            // would still overflow the uint8_t intermediate).
            gState.fire.embers[i].x = (uint8_t)(
                ((int16_t)gState.fire.embers[i].x + (int16_t)random8(0, 3) - 1 + FIRE_W)
                % FIRE_W
            );
            gState.fire.embers[i].ttl = random8(EMBER_TTL_LO, EMBER_TTL_HI + 1);
            if (gState.fire.embers[i].y < 0) gState.fire.embers[i].y = -1;
        }
    }
    if (random8(0, 6) == 0) spawnEmber();
}

void effectFire() {
    // Seed the bottom row
    for (uint8_t x = 0; x < FIRE_W; x++) {
        if (random8(FIRE_SEED_HOT_CHANCE) == 0)
            fireSet(x, FIRE_H - 1, random8(140, FIRE_SEED_HOT + 1));
        else
            fireSet(x, FIRE_H - 1, random8(5, 30));
    }
    // Propagate heat upward with cooling.
    // Runs once every FIRE_RISE_EVERY frames — set to 1 for every frame,
    // or higher (e.g. 5) to slow the rise to 1-in-5 frames.
    if (++gState.fire.riseCounter >= FIRE_RISE_EVERY) {
        gState.fire.riseCounter = 0;
        for (uint8_t x = 0; x < FIRE_W; x++) {
            for (uint8_t y = 0; y < FIRE_H - FIRE_RISE; y++) {
                uint8_t  left  = fireGet((x + FIRE_W - 1) % FIRE_W, y + FIRE_RISE);
                uint8_t  mid   = fireGet(x,                          y + FIRE_RISE);
                uint8_t  right = fireGet((x + 1)          % FIRE_W, y + FIRE_RISE);
                uint16_t avg   = ((uint16_t)left + mid + right) / 3;
                uint8_t  cool  = random8(FIRE_COOL_LO, FIRE_COOL_HI + 1);
                fireSet(x, y, (avg > cool) ? avg - cool : 0);
            }
        }
    }
    updateEmbers();

    // Render directly from heat — no interpolation needed on ESP32-S3
    for (uint8_t x = 0; x < FIRE_W; x++) {
        for (uint8_t y = 0; y < FIRE_H; y++) {
            leds[translatePixel(x, y)] = heatToColor(fireGet(x, y));
        }
    }
    for (uint8_t i = 0; i < MAX_EMBERS; i++) {
        if (gState.fire.embers[i].y < 0 || gState.fire.embers[i].y >= FIRE_H) continue;
        leds[translatePixel(gState.fire.embers[i].x, gState.fire.embers[i].y)] =
            heatToColor(gState.fire.embers[i].heat);
    }
    FastLED.show();
}

// =============================================================================
// Mode 3: Equalizer
// =============================================================================
// 60 columns = 60 frequency bands (logarithmically spaced 80Hz–16kHz).
// Each column draws a bar from the bottom up, coloured by hue (bass=red,
// treble=violet), plus a peak-hold dot that falls slowly after the bar drops.

void effectEqualizer() {
    // freshBands is declared static to avoid repeated 240-byte stack growth on
    // every call (especially since audioGetBands also has a large stack frame).
    static float freshBands[EQ_BANDS];
    bool  gotFrame = audioGetBands(freshBands);

    FastLED.clear();

    for (uint8_t col = 0; col < MATRIX_W; col++) {
        // Update bar height from audio if a fresh FFT frame arrived
        if (gotFrame) {
            gState.eq.bands[col] = freshBands[col];

            // Peak hold: raise peak if bar exceeds it, else hold/fall
            if (gState.eq.bands[col] >= gState.eq.peaks[col]) {
                gState.eq.peaks[col]   = gState.eq.bands[col];
                gState.eq.peakTtl[col] = EQ_PEAK_HOLD;
            } else {
                if (gState.eq.peakTtl[col] > 0) {
                    gState.eq.peakTtl[col]--;
                } else {
                    gState.eq.peaks[col] -= EQ_PEAK_FALL;
                    if (gState.eq.peaks[col] < 0.0f) gState.eq.peaks[col] = 0.0f;
                }
            }
        }

        // How many rows to light for this column (0-MATRIX_H)
        uint8_t barH = (uint8_t)(gState.eq.bands[col] * MATRIX_H);
        barH = min(barH, (uint8_t)MATRIX_H);

        // Hue: bass (col 0) = red (hue 0), treble (col MATRIX_W-1) = violet (EQ_HUE_MAX)
        uint8_t hue = (uint8_t)((uint16_t)col * EQ_HUE_MAX / (MATRIX_W - 1));

        // Draw bar from bottom up; brightness ramps from EQ_BRI_MIN at base
        // to (EQ_BRI_MIN + EQ_BRI_RANGE) = 255 at the top of the bar.
        for (uint8_t row = 0; row < barH; row++) {
            uint8_t physRow = MATRIX_H - 1 - row;   // row 0 = bottom of display
            uint8_t bri     = EQ_BRI_MIN + (uint8_t)((uint16_t)row * EQ_BRI_RANGE / MATRIX_H);
            leds[translatePixel(col, physRow)] = CHSV(hue, 255, bri);
        }

        // Peak dot — slightly desaturated and full-brightness
        uint8_t peakRow  = (uint8_t)(gState.eq.peaks[col] * (MATRIX_H - 1));
        peakRow          = min(peakRow, (uint8_t)(MATRIX_H - 1));
        uint8_t peakPhys = MATRIX_H - 1 - peakRow;
        leds[translatePixel(col, peakPhys)] = CHSV(hue, 180, 255);
    }

    FastLED.show();
}

// =============================================================================
// Mode 4: Fireworks
// =============================================================================

static void fwLaunch(Firework& fw) {
    fw.state  = FW_RISING;
    fw.x      = random8(MATRIX_W);
    fw.y      = MATRIX_H - 1;
    fw.burstY = random8(1, MATRIX_H / 2 + 1);
    fw.hue    = random8();
    fw.shape  = (random8(2) == 0) ? FW_CIRCLE : FW_CROSS;
    fw.radius = 0;
    fw.ttl    = 2;
    for (uint8_t i = 0; i < 3; i++) { fw.trailX[i] = fw.x; fw.trailY[i] = (uint8_t)fw.y; }
}

// fwSetPixel: bounds-safe pixel write.
// y is int8_t so callers can pass signed values directly; negative y is clipped.
static void fwSetPixel(uint8_t x, int8_t y, CRGB col) {
    if (y < 0 || y >= (int8_t)MATRIX_H) return;
    leds[translatePixel(x % MATRIX_W, (uint8_t)y)] = col;
}

static void fwUpdate(Firework& fw) {
    if (fw.ttl > 0) { fw.ttl--; return; }
    if (fw.state == FW_RISING) {
        for (uint8_t i = 2; i > 0; i--) { fw.trailX[i] = fw.trailX[i-1]; fw.trailY[i] = fw.trailY[i-1]; }
        fw.trailX[0] = fw.x; fw.trailY[0] = (uint8_t)fw.y;
        fw.y--;
        fw.ttl = 2;
        if (fw.y < 0 || fw.y <= (int8_t)fw.burstY) {
            fw.y     = fw.burstY;
            fw.state = FW_EXPLODE;
            fw.radius = 0;
            fw.ttl    = 3;
        }
    } else if (fw.state == FW_EXPLODE) {
        fw.radius++;
        fw.ttl = 4;
        if (fw.radius > 4) fw.state = FW_IDLE;
    }
}

// Helper: draw one explosion point symmetrically about (cx, burstY) at
// offset (+dx, +dy) and all reflections, wrapping X around the matrix width.
static void fwDrawSymmetric(uint8_t cx, uint8_t burstY, uint8_t dx, uint8_t dy, CRGB col) {
    fwSetPixel((cx + dx)          % MATRIX_W, (int8_t)(burstY + dy), col);
    fwSetPixel((cx + MATRIX_W - dx) % MATRIX_W, (int8_t)(burstY + dy), col);
    if (dy != 0) {
        fwSetPixel((cx + dx)          % MATRIX_W, (int8_t)(burstY - dy), col);
        fwSetPixel((cx + MATRIX_W - dx) % MATRIX_W, (int8_t)(burstY - dy), col);
    }
}

static void fwDraw(const Firework& fw) {
    if (fw.state == FW_IDLE) return;

    if (fw.state == FW_RISING) {
        fwSetPixel(fw.x, fw.y, CRGB::White);
        // Orange → dark-orange → amber trail gradient
        static const CRGB trail[3] = { CRGB(200,150,0), CRGB(150,60,0), CRGB(60,20,0) };
        for (uint8_t i = 0; i < 3; i++)
            fwSetPixel(fw.trailX[i], (int8_t)fw.trailY[i], trail[i]);
    }

    if (fw.state == FW_EXPLODE) {
        uint8_t r = fw.radius;
        // Brightness fades with radius: 255 → 200 → 150 → 90 → 50
        static const uint8_t briByrRadius[5] = { 255, 200, 150, 90, 50 };
        uint8_t bri = (r < 5) ? briByrRadius[r] : 50;
        CRGB    col = CHSV(fw.hue, 255, bri);

        if (fw.shape == FW_CIRCLE) {
            if (r == 0) {
                fwSetPixel(fw.x, (int8_t)fw.burstY, col);
            } else {
                // Cardinal points
                fwDrawSymmetric(fw.x, fw.burstY, r, 0, col);
                fwDrawSymmetric(fw.x, fw.burstY, 0, r, col);
                // Diagonal points at ~45° (approximated as d = (r+1)/2)
                uint8_t d = (r + 1) / 2;
                fwDrawSymmetric(fw.x, fw.burstY, d, d, col);
            }
        } else {
            // Cross: fill full arms along X and Y axes
            for (uint8_t i = 0; i <= r; i++) {
                fwDrawSymmetric(fw.x, fw.burstY, i, 0, col);
                fwDrawSymmetric(fw.x, fw.burstY, 0, i, col);
            }
        }
    }
}

void effectFireworks() {
    for (uint8_t i = 0; i < MAX_FIREWORKS; i++)
        if (gState.fireworks.fw[i].state == FW_IDLE && random8(FW_LAUNCH_CHANCE) == 0)
            fwLaunch(gState.fireworks.fw[i]);

    // Fade the whole buffer toward black (creates the comet-tail effect)
    fadeToBlackBy(leds, NUM_LEDS, FW_FADE_AMOUNT);

    for (uint8_t i = 0; i < MAX_FIREWORKS; i++) {
        fwUpdate(gState.fireworks.fw[i]);
        fwDraw(gState.fireworks.fw[i]);
    }
    FastLED.show();
}
