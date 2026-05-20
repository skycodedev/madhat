// test_matrix/test_main.cpp
//
// Tests for translatePixel() in src/matrix.h.
//
// This file contains a verbatim copy of translatePixel() so it compiles on the
// host without Arduino.h. If you change the formula in src/matrix.h, update
// the copy here too — otherwise these tests pass while the live code is broken.
//
// Run with: pio test -e native

#include "../native_shims.h"
#include <unity.h>

// Verbatim copy of src/matrix.h:15-20.
// MATRIX_W, MATRIX_H, NUM_LEDS are set by [env:native] in platformio.ini.
static inline uint16_t translatePixel(uint8_t x, uint8_t y) {
    bool     ledDirection    = x % 2;
    uint16_t translatedPixel = ledDirection
                                 ? (MATRIX_H * x) + y
                                 : (MATRIX_H * x) + ((MATRIX_H - 1) - y);
    return (NUM_LEDS - 1) - translatedPixel;
}

// ── TC-MAP-02 ─────────────────────────────────────────────────────────────────
// Every (x, y) pair maps to a unique index, and every index in [0, 479] is used.
// This also proves bounds: if all 480 indices are distinct and fit in [0, 479],
// no index can be out of range.
void test_no_duplicate_indices(void) {
    bool seen[NUM_LEDS] = {};
    for (uint8_t x = 0; x < MATRIX_W; x++) {
        for (uint8_t y = 0; y < MATRIX_H; y++) {
            uint16_t idx = translatePixel(x, y);
            char msg[48];
            snprintf(msg, sizeof(msg), "Duplicate idx %u at (%u,%u)", idx, x, y);
            TEST_ASSERT_FALSE_MESSAGE(seen[idx], msg);
            seen[idx] = true;
        }
    }
    for (uint16_t i = 0; i < NUM_LEDS; i++) {
        char msg[32];
        snprintf(msg, sizeof(msg), "Index %u never mapped", i);
        TEST_ASSERT_TRUE_MESSAGE(seen[i], msg);
    }
}

// ── TC-MAP-03 ─────────────────────────────────────────────────────────────────
// The strip's first LED (index 0) is the physical top-right pixel.
// col 59 is odd → raw = 8×59 + 7 = 479 → mirrored = 479-479 = 0.
// If this fails and the wiring is correct, the strip is connected backwards
// (see README "Ring topology").
void test_strip_origin_top_right(void) {
    TEST_ASSERT_EQUAL_UINT16(0, translatePixel(59, 7));
}

// ── TC-MAP-04 ─────────────────────────────────────────────────────────────────
// Even columns run bottom-to-top: the bottom pixel (y=7) has a higher physical
// index than the top pixel (y=0).
// col 0 (even): y=7 → raw=0,  result=479
//               y=0 → raw=7,  result=472
void test_even_col_bottom_to_top(void) {
    uint16_t top_idx    = translatePixel(0, 0); // logical top
    uint16_t bottom_idx = translatePixel(0, 7); // logical bottom
    TEST_ASSERT_EQUAL_UINT16(472, top_idx);
    TEST_ASSERT_EQUAL_UINT16(479, bottom_idx);
    TEST_ASSERT_GREATER_THAN_UINT16(top_idx, bottom_idx);
}

// ── TC-MAP-05 ─────────────────────────────────────────────────────────────────
// Odd columns run top-to-bottom: the top pixel (y=0) has a higher physical
// index than the bottom pixel (y=7).
// col 1 (odd): y=0 → raw=8,  result=471
//              y=7 → raw=15, result=464
void test_odd_col_top_to_bottom(void) {
    uint16_t top_idx    = translatePixel(1, 0); // logical top
    uint16_t bottom_idx = translatePixel(1, 7); // logical bottom
    TEST_ASSERT_EQUAL_UINT16(471, top_idx);
    TEST_ASSERT_EQUAL_UINT16(464, bottom_idx);
    TEST_ASSERT_GREATER_THAN_UINT16(bottom_idx, top_idx);
}

// ── Test runner ───────────────────────────────────────────────────────────────
void setUp(void)    {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_no_duplicate_indices);
    RUN_TEST(test_strip_origin_top_right);
    RUN_TEST(test_even_col_bottom_to_top);
    RUN_TEST(test_odd_col_top_to_bottom);
    return UNITY_END();
}
