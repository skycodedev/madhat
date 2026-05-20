// test_effects_bounds/test_main.cpp
//
// Bounds and clipping tests for effect helper functions.
//
// IMPORTANT: this file contains verbatim copies of small functions from
// src/effects.cpp. If you change the source, update the copies here too.
// Each copy is labelled with the line number it was taken from.
//
// Run with: pio test -e native

#include "../native_shims.h"
#include <unity.h>

// LED array stub with two guard slots past the end.
// If any function writes to leds[NUM_LEDS] or leds[NUM_LEDS+1] the sentinel
// check will catch it.
static CRGB leds[NUM_LEDS + 2];
static const CRGB SENTINEL = CRGB(0xAB, 0xCD, 0xEF);

static void resetLeds(void) {
    for (int i = 0; i < NUM_LEDS + 2; i++) leds[i] = SENTINEL;
}

static bool sentinelIntact(void) {
    return (leds[NUM_LEDS]   == SENTINEL &&
            leds[NUM_LEDS+1] == SENTINEL);
}

// Verbatim copy of src/matrix.h:15-20.
static inline uint16_t translatePixel(uint8_t x, uint8_t y) {
    bool     ledDirection    = x % 2;
    uint16_t translatedPixel = ledDirection
                                 ? (MATRIX_H * x) + y
                                 : (MATRIX_H * x) + ((MATRIX_H - 1) - y);
    return (NUM_LEDS - 1) - translatedPixel;
}

// Verbatim copy of src/effects.cpp:448-451.
static void fwSetPixel(uint8_t x, int8_t y, CRGB col) {
    if (y < 0 || y >= (int8_t)MATRIX_H) return;
    leds[translatePixel(x % MATRIX_W, (uint8_t)y)] = col;
}

// ── TC-EFF-01/02 (merged) ─────────────────────────────────────────────────────
// fwSetPixel must not write to leds[] for any y outside [0, MATRIX_H-1].
// On a fireworks rocket, the trail pixels are written at y-1..y-3. If the
// rocket head is near the top of the matrix, those values go negative. Without
// the guard, a negative y cast to uint8_t becomes a large positive number and
// writes far outside the array.
void test_fwSetPixel_clips_invalid_y(void) {
    resetLeds();
    // Negative y values
    fwSetPixel(5,   -1, CRGB(255, 0, 0));
    fwSetPixel(5,  -50, CRGB(255, 0, 0));
    fwSetPixel(0,   -1, CRGB(255, 0, 0));
    fwSetPixel(59,  -1, CRGB(255, 0, 0));
    // y >= MATRIX_H
    fwSetPixel(5, (int8_t)MATRIX_H,       CRGB(0, 255, 0));
    fwSetPixel(5, (int8_t)(MATRIX_H + 5), CRGB(0, 255, 0));
    fwSetPixel(5, (int8_t)127,            CRGB(0, 255, 0));
    TEST_ASSERT_TRUE_MESSAGE(sentinelIntact(), "fwSetPixel wrote past end of leds[]");
}

// ── TC-EFF-03 ─────────────────────────────────────────────────────────────────
// The fire heat array uses the index formula heat[x * FIRE_H + y] — copied from
// src/effects.cpp:279-280. This test checks the formula, not the actual source
// functions fireGet/fireSet. If someone changes the formula in effects.cpp but
// not here, this test still passes — check both places.
void test_fire_heat_index_in_bounds(void) {
    const int FIRE_W = MATRIX_W;
    const int FIRE_H = MATRIX_H;
    for (int x = 0; x < FIRE_W; x++) {
        for (int y = 0; y < FIRE_H; y++) {
            int idx = x * FIRE_H + y;
            char msg[48];
            snprintf(msg, sizeof(msg), "Fire OOB at (%d,%d)->%d", x, y, idx);
            TEST_ASSERT_LESS_THAN_MESSAGE(FIRE_W * FIRE_H, idx, msg);
        }
    }
}

// ── TC-EFF-04 ─────────────────────────────────────────────────────────────────
// The fire propagation loop at src/effects.cpp:344 reads heat at row y + FIRE_RISE.
// Verify that y + FIRE_RISE never reaches MATRIX_H inside the loop.
//
// FIRE_RISE is hardcoded to 1 below — matching effects.cpp:30.
// If you change FIRE_RISE in effects.cpp, update the value here too.
// This test does NOT read FIRE_RISE from effects.cpp automatically.
void test_fire_propagation_row_never_exceeds_height(void) {
    const int FIRE_H    = MATRIX_H;
    const int FIRE_RISE = 1; // must match effects.cpp:30
    for (int y = 0; y < FIRE_H - FIRE_RISE; y++) {
        int accessed_row = y + FIRE_RISE;
        char msg[48];
        snprintf(msg, sizeof(msg),
                 "Fire propagation OOB: row %d >= %d", accessed_row, FIRE_H);
        TEST_ASSERT_LESS_THAN_MESSAGE(FIRE_H, accessed_row, msg);
    }
    TEST_ASSERT_GREATER_THAN_INT(0, FIRE_H - FIRE_RISE);
}

// ── TC-EFF-05 ─────────────────────────────────────────────────────────────────
// drawColumn at src/effects.cpp:217 skips characters outside [0x20, 0x7A].
// If a character outside this range is passed, the font table index underflows
// and reads garbage data, lighting wrong pixels.
// The font supports ASCII space (0x20) through lowercase z (0x7A).
void test_scroll_font_guard_rejects_out_of_range_chars(void) {
    auto fontGuard = [](char c) -> bool {
        // Verbatim condition from effects.cpp:217
        return !(c < 0x20 || c > (char)0x7A);
    };

    TEST_ASSERT_TRUE(fontGuard(0x20));   // space — first valid char
    TEST_ASSERT_TRUE(fontGuard('A'));
    TEST_ASSERT_TRUE(fontGuard('z'));    // last valid char
    TEST_ASSERT_TRUE(fontGuard('0'));
    TEST_ASSERT_TRUE(fontGuard('!'));

    TEST_ASSERT_FALSE(fontGuard(0x1F)); // one below space
    TEST_ASSERT_FALSE(fontGuard(0x7B)); // '{' — one above 'z'
    TEST_ASSERT_FALSE(fontGuard('\0')); // null terminator
    TEST_ASSERT_FALSE(fontGuard('\n')); // newline
    TEST_ASSERT_FALSE(fontGuard('\t')); // tab
}

// ── TC-EFF-06 ─────────────────────────────────────────────────────────────────
// The scroll tiling at src/effects.cpp:237-243 divides the screen position by 6
// to get a character index. Check it never reaches textLen (which would be OOB).
//
// Note: this uses "HELLO WORLD" as a test string. The live scroll text in
// main.cpp is "FUCKING HIPPIES!!" — if you change that string, test it here too.
void test_scroll_charIdx_always_within_text_length(void) {
    const char* text    = "HELLO WORLD";
    size_t      textLen = strnlen(text, 42); // 42 = SCROLL_MAX_LEN
    int16_t     textW   = (int16_t)(textLen * 6);

    for (int16_t scrollOffset = 0; scrollOffset < textW; scrollOffset++) {
        for (uint8_t screenX = 0; screenX < MATRIX_W; screenX++) {
            int16_t tilePos = ((int16_t)screenX + scrollOffset) % textW;
            if (tilePos < 0) tilePos += textW;
            uint8_t charIdx = (uint8_t)(tilePos / 6);
            char msg[64];
            snprintf(msg, sizeof(msg),
                     "charIdx %u >= textLen %zu at screenX=%u scroll=%d",
                     charIdx, textLen, screenX, scrollOffset);
            TEST_ASSERT_LESS_THAN_MESSAGE((int)textLen, (int)charIdx, msg);
        }
    }
}

// ── TC-BRI-03 ─────────────────────────────────────────────────────────────────
// The brightness formula at src/main.cpp:43 maps ADC input [0, 4095] to
// [POT_MIN_BRIGHT, POT_MAX_BRIGHT]. Check it never goes below the minimum
// (which would make the display appear dead) or above the maximum.
void test_brightness_map_stays_in_range(void) {
    // Replicates Arduino map(): out_lo + (in - in_lo) * (out_hi - out_lo) / (in_hi - in_lo)
    auto arduino_map = [](long x, long in_lo, long in_hi, long out_lo, long out_hi) -> long {
        return out_lo + (x - in_lo) * (out_hi - out_lo) / (in_hi - in_lo);
    };

    for (int raw = 0; raw <= 4095; raw++) {
        long bri = arduino_map(raw, 0, 4095, POT_MIN_BRIGHT, POT_MAX_BRIGHT);
        char msg[56];
        snprintf(msg, sizeof(msg), "raw=%d -> bri=%ld below POT_MIN_BRIGHT", raw, bri);
        TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(POT_MIN_BRIGHT, (int)bri, msg);
        snprintf(msg, sizeof(msg), "raw=%d -> bri=%ld above POT_MAX_BRIGHT", raw, bri);
        TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(POT_MAX_BRIGHT, (int)bri, msg);
    }
}

// ── Test runner ───────────────────────────────────────────────────────────────
void setUp(void)    {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_fwSetPixel_clips_invalid_y);
    RUN_TEST(test_fire_heat_index_in_bounds);
    RUN_TEST(test_fire_propagation_row_never_exceeds_height);
    RUN_TEST(test_scroll_font_guard_rejects_out_of_range_chars);
    RUN_TEST(test_scroll_charIdx_always_within_text_length);
    RUN_TEST(test_brightness_map_stays_in_range);
    return UNITY_END();
}
