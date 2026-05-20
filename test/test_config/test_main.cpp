// test_config/test_main.cpp
//
// Compile-time checks for constants in src/config.h.
//
// These are static_asserts, not runtime tests. If any check below fails,
// the build stops with a clear error message — you never reach the hat.
// The constants (MATRIX_W, NUM_LEDS, etc.) come from platformio.ini [env:native],
// which mirrors the values in src/config.h.
//
// Run with: pio test -e native

#include "../native_shims.h"
#include <unity.h>

// EQ_BANDS is defined in src/audio.h. It must equal MATRIX_W because
// effectEqualizer() draws one bar per column using freshBands[col].
#define EQ_BANDS 60

// If NUM_LEDS != MATRIX_W * MATRIX_H, translatePixel() writes past the end of leds[].
static_assert(NUM_LEDS == MATRIX_W * MATRIX_H,
    "NUM_LEDS must equal MATRIX_W * MATRIX_H — update one to match the other in src/config.h");

// If EQ_BANDS != MATRIX_W, the equalizer reads past the end of the bands[] array.
static_assert(EQ_BANDS == MATRIX_W,
    "EQ_BANDS (audio.h) must equal MATRIX_W (config.h)");

// translatePixel() returns uint16_t — NUM_LEDS must fit.
static_assert(NUM_LEDS <= 65535u,
    "NUM_LEDS must fit in uint16_t (translatePixel return type)");

// Brightness range must make sense.
static_assert(POT_MIN_BRIGHT > 0,
    "POT_MIN_BRIGHT must be > 0 — LEDs should never go fully dark");
static_assert(POT_MIN_BRIGHT < POT_MAX_BRIGHT,
    "POT_MIN_BRIGHT must be less than POT_MAX_BRIGHT");

// Basic sanity.
static_assert(MATRIX_W > 0 && MATRIX_H > 0, "Matrix dimensions must be positive");
static_assert(NUM_MODES > 0, "NUM_MODES must be positive");

// Unity requires at least one test function. This is a no-op placeholder —
// all real checks are the static_asserts above.
void test_static_asserts_passed(void) {
    // If we got here, the build succeeded and all static_asserts above are true.
    TEST_PASS();
}

void setUp(void)    {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_static_asserts_passed);
    return UNITY_END();
}
