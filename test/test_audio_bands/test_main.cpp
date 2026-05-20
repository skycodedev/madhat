// test_audio_bands/test_main.cpp
//
// Tests for the equalizer frequency mapping and auto-gain maths in src/audio.cpp.
//
// On startup, audioInit() maps 60 equalizer bars to ranges of FFT frequency bins.
// audioGetBands() then adjusts volume automatically using a rolling average.
// These tests check that maths without needing the I2S microphone or FFT library.
//
// Run with: pio test -e native

#include "../native_shims.h"
#include <unity.h>

// ── Constants mirrored from src/audio.h and src/audio.cpp ─────────────────────
// Kept local so this file does not pull in Arduino.h via audio.h.
#define EQ_BANDS     60
#define FFT_SAMPLES  512
#define SAMPLE_RATE  44100
#define FREQ_LOW     80.0f
#define FREQ_HIGH    16000.0f
#define BIN_HZ       ((float)SAMPLE_RATE / (float)FFT_SAMPLES)  // ~86.1 Hz/bin

#define GAIN_HISTORY_FRAMES 750
#define GAIN_MIN_REF        0.001f

// ── Helpers ───────────────────────────────────────────────────────────────────
static int bandBinLow [EQ_BANDS];
static int bandBinHigh[EQ_BANDS];

// Replicates audioInit() band-boundary computation from src/audio.cpp:94-102.
static void computeBandBoundaries(void) {
    float logLow  = log10f(FREQ_LOW);
    float logHigh = log10f(FREQ_HIGH);
    float logStep = (logHigh - logLow) / (float)EQ_BANDS;
    for (int b = 0; b < EQ_BANDS; b++) {
        float fLow  = powf(10.0f, logLow + (float)b       * logStep);
        float fHigh = powf(10.0f, logLow + (float)(b + 1) * logStep);
        bandBinLow [b] = max(1,                    (int)(fLow  / BIN_HZ));
        bandBinHigh[b] = min(FFT_SAMPLES / 2 - 1, (int)(fHigh / BIN_HZ));
    }
}

// ── TC-AUDIO-01 ───────────────────────────────────────────────────────────────
// Every band maps to at least one FFT bin: bandBinLow[b] <= bandBinHigh[b].
// An empty band (low > high) produces permanent zero output for that EQ column.
void test_every_band_has_at_least_one_bin(void) {
    computeBandBoundaries();
    for (int b = 0; b < EQ_BANDS; b++) {
        char msg[64];
        snprintf(msg, sizeof(msg),
                 "Band %d empty: low=%d high=%d", b, bandBinLow[b], bandBinHigh[b]);
        TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(bandBinHigh[b], bandBinLow[b], msg);
    }
}

// ── TC-AUDIO-02 ───────────────────────────────────────────────────────────────
// All bin indices stay inside the usable FFT range.
// Bin 0 is DC; the useful range is [1, FFT_SAMPLES/2 - 1] = [1, 255].
void test_bin_boundaries_in_fft_range(void) {
    computeBandBoundaries();
    for (int b = 0; b < EQ_BANDS; b++) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Band %d low=%d out of range", b, bandBinLow[b]);
        TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(1, bandBinLow[b], msg);

        snprintf(msg, sizeof(msg), "Band %d high=%d out of range", b, bandBinHigh[b]);
        TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(FFT_SAMPLES / 2 - 1, bandBinHigh[b], msg);
    }
}

// ── TC-AUDIO-03 ───────────────────────────────────────────────────────────────
// Band low boundaries are monotonically non-decreasing.
// Logarithmic spacing guarantees this; a formula regression would break it.
void test_bands_monotonically_non_decreasing(void) {
    computeBandBoundaries();
    for (int b = 1; b < EQ_BANDS; b++) {
        char msg[64];
        snprintf(msg, sizeof(msg),
                 "Band %d low(%d) < band %d low(%d)",
                 b, bandBinLow[b], b-1, bandBinLow[b-1]);
        TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(
            bandBinLow[b - 1], bandBinLow[b], msg);
    }
}

// ── TC-AUDIO-04 ───────────────────────────────────────────────────────────────
// The auto-gain keeps a rolling sum of the last 750 frame averages (about 15 s).
// Each frame it subtracts the oldest value and adds the newest. After many frames
// the running sum must match a direct re-sum of the buffer.
// If this test fails, the gain reference drifts over time — the EQ bars will
// flatten out or stay clipped at full height even when the room is quiet or loud.
// Replicates src/audio.cpp:172-176.
void test_gain_circular_buffer_sum_consistency(void) {
    static float gainHistory[GAIN_HISTORY_FRAMES];
    memset(gainHistory, 0, sizeof(gainHistory));
    float    gainSum  = 0.0f;
    uint16_t gainHead = 0;

    for (int i = 0; i < 1500; i++) {
        // Synthetic frame mean: ramp 0.0..0.99 repeating
        float value = (float)(i % 100) * 0.01f;
        gainSum -= gainHistory[gainHead];
        gainHistory[gainHead] = value;
        gainSum += value;
        gainHead = (gainHead + 1) % GAIN_HISTORY_FRAMES;
    }

    // Ground-truth: sum every slot directly
    float expected = 0.0f;
    for (int i = 0; i < GAIN_HISTORY_FRAMES; i++) expected += gainHistory[i];

    // Allow small float accumulation error (~1 ULP per addition × 750)
    TEST_ASSERT_FLOAT_WITHIN(0.01f, expected, gainSum);
}

// ── TC-AUDIO-05 ───────────────────────────────────────────────────────────────
// gainRef floor of GAIN_MIN_REF=0.001 prevents divide-by-zero in silence.
// Replicates src/audio.cpp:155-156 and :179-180.
void test_gain_ref_floor_on_silence(void) {
    // All-zero history (complete silence since power-on)
    float gainSum = 0.0f;
    float gainRef = gainSum / (float)GAIN_HISTORY_FRAMES;
    if (gainRef < GAIN_MIN_REF) gainRef = GAIN_MIN_REF;

    TEST_ASSERT_GREATER_OR_EQUAL_FLOAT(GAIN_MIN_REF, gainRef);

    // Must be usable as a divisor without producing infinity or NaN
    float result = 1.0f / gainRef;
    TEST_ASSERT_FALSE(isinf(result));
    TEST_ASSERT_FALSE(isnan(result));
}

// ── Test runner ───────────────────────────────────────────────────────────────
void setUp(void)    {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_every_band_has_at_least_one_bin);
    RUN_TEST(test_bin_boundaries_in_fft_range);
    RUN_TEST(test_bands_monotonically_non_decreasing);
    RUN_TEST(test_gain_circular_buffer_sum_consistency);
    RUN_TEST(test_gain_ref_floor_on_silence);
    return UNITY_END();
}
