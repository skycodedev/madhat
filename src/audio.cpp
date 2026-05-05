#include "audio.h"
#include <driver/i2s.h>
#include <arduinoFFT.h>
#include <math.h>

// ── I2S port ──────────────────────────────────────────────────────────────────
#define I2S_PORT  I2S_NUM_0

// ── INMP441 bit-alignment constants ───────────────────────────────────────────
// The INMP441 outputs data left-justified in a 32-bit I2S word: the top 24 bits
// are the signed sample; the bottom 8 bits are always zero.
#define INMP441_SHIFT  8        // bits to right-shift to remove the zero padding
#define INMP441_SCALE  (1 << 23)  // full-scale divisor after shifting (24-bit signed)

// ── FFT buffers ───────────────────────────────────────────────────────────────
// Using float (32-bit) instead of double (64-bit) saves 4 KB of static RAM
// with no meaningful loss of precision for an audio equalizer application.
static float vReal[FFT_SAMPLES];
static float vImag[FFT_SAMPLES];

static ArduinoFFT<float> FFT(vReal, vImag, FFT_SAMPLES, SAMPLE_RATE);

// ── Frequency band mapping ────────────────────────────────────────────────────
// EQ_BANDS logarithmically spaced bands across FREQ_LOW–FREQ_HIGH.
// Low end starts at 80 Hz (below that is mostly rumble on a small mic).
#define FREQ_LOW   80.0f
#define FREQ_HIGH  16000.0f

// Each FFT bin covers SAMPLE_RATE / FFT_SAMPLES Hz
#define BIN_HZ  ((float)SAMPLE_RATE / (float)FFT_SAMPLES)

// Smoothing: blend previous band value with new one (0=no smoothing, 1=frozen)
#define SMOOTH_UP    0.6f   // rising  — fast attack
#define SMOOTH_DOWN  0.85f  // falling — slow decay (gives a nice visual trail)

// Per-band peak decay — fast (0.95) so it acts only as a ceiling guard for
// outlier bands. The primary gain control is the global rolling average below.
#define AUTOGAIN_DECAY  0.95f

// ── Global rolling-average gain ───────────────────────────────────────────────
// Circular buffer storing the mean magnitude across all EQ_BANDS for each of
// the last GAIN_HISTORY_FRAMES frames (~15 s at 50 fps).
// gainSum tracks the running total so the mean is O(1) per frame.
#define GAIN_HISTORY_FRAMES  750     // ~15 s × 50 fps
#define GAIN_MIN_REF         0.001f  // floor — prevents div-by-zero in silence

// Saturation-weighted gain push — when bands clip, the current frame is pushed
// into gainHistory with extra weight so gainRef rises faster and pulls gain down.
// sqrtf curve means even 1 saturated band out of 60 roughly doubles the weight.
#define SATURATION_THRESHOLD  0.85f  // band considered saturated above this level
#define SATURATION_BOOST      8.0f   // max extra multiplier (at 100% saturation → 9×)

static float    gainHistory[GAIN_HISTORY_FRAMES] = {};
static float    gainSum    = 0.0f;
static uint16_t gainHead   = 0;

// ── Precomputed band bin boundaries ───────────────────────────────────────────
// Computed once in audioInit() so no log10f/powf calls in the real-time path.
static int bandBinLow [EQ_BANDS];
static int bandBinHigh[EQ_BANDS];

// Running peak per band for auto-gain normalisation
static float bandPeak[EQ_BANDS];

// Raw sample buffer — static so it does not live on the task stack
static int32_t rawSamples[FFT_SAMPLES];

void audioInit() {
    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate          = SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 4,
        .dma_buf_len          = FFT_SAMPLES,
        .use_apll             = false,
        .tx_desc_auto_clear   = false,
        .fixed_mclk           = 0
    };
    i2s_pin_config_t pins = {
        .bck_io_num   = MIC_SCK_PIN,
        .ws_io_num    = MIC_WS_PIN,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = MIC_SD_PIN
    };
    ESP_ERROR_CHECK(i2s_driver_install(I2S_PORT, &cfg, 0, nullptr));
    ESP_ERROR_CHECK(i2s_set_pin(I2S_PORT, &pins));
    ESP_ERROR_CHECK(i2s_start(I2S_PORT));

    // Precompute logarithmically spaced bin boundaries for each EQ band.
    // This moves all log10f/powf calls out of the real-time audio path.
    float logLow  = log10f(FREQ_LOW);
    float logHigh = log10f(FREQ_HIGH);
    float logStep = (logHigh - logLow) / (float)EQ_BANDS;
    for (int b = 0; b < EQ_BANDS; b++) {
        float fLow  = powf(10.0f, logLow + (float)b       * logStep);
        float fHigh = powf(10.0f, logLow + (float)(b + 1) * logStep);
        bandBinLow [b] = max(1,                    (int)(fLow  / BIN_HZ));
        bandBinHigh[b] = min(FFT_SAMPLES / 2 - 1, (int)(fHigh / BIN_HZ));
    }

    // Initialise peaks to a small non-zero value to avoid div-by-zero at start
    for (int i = 0; i < EQ_BANDS; i++) bandPeak[i] = 1.0f;
}

bool audioGetBands(float bands[EQ_BANDS]) {
    // Read FFT_SAMPLES 32-bit samples from I2S (non-blocking)
    size_t bytesRead = 0;
    esp_err_t err = i2s_read(I2S_PORT, rawSamples, sizeof(rawSamples),
                             &bytesRead, 0);
    if (err != ESP_OK || bytesRead < sizeof(rawSamples)) return false;

    // Convert to float: right-shift to remove zero-padding, then normalise to [-1, +1]
    for (int i = 0; i < FFT_SAMPLES; i++) {
        vReal[i] = (float)(rawSamples[i] >> INMP441_SHIFT) / (float)INMP441_SCALE;
        vImag[i] = 0.0f;
    }

    FFT.windowing(FFTWindow::Hann, FFTDirection::Forward);
    FFT.compute(FFTDirection::Forward);
    FFT.complexToMagnitude();

    // Compute per-band magnitudes and store them before normalisation so we can
    // feed the rolling-average gain after the loop.
    static float mag [EQ_BANDS] = {};
    static float prev[EQ_BANDS] = {};

    for (int b = 0; b < EQ_BANDS; b++) {
        float sum = 0.0f;
        int   cnt = 0;
        for (int bin = bandBinLow[b]; bin <= bandBinHigh[b]; bin++) {
            sum += vReal[bin];
            cnt++;
        }
        mag[b] = (cnt > 0) ? (sum / (float)cnt) : 0.0f;

        // Per-band peak — fast decay (AUTOGAIN_DECAY = 0.95) so it acts only as
        // a ceiling guard for bands that spike well above the global average.
        if (mag[b] > bandPeak[b])
            bandPeak[b] = mag[b];
        else
            bandPeak[b] *= AUTOGAIN_DECAY;
    }

    // ── Rolling-average global gain ───────────────────────────────────────────
    // Compute the mean magnitude across all bands for this frame.
    float frameMean = 0.0f;
    for (int b = 0; b < EQ_BANDS; b++) frameMean += mag[b];
    frameMean /= (float)EQ_BANDS;

    // Compute gainRef from the current buffer state (before this frame's push)
    // so we can use it for saturation detection without a circular dependency.
    float gainRef = gainSum / (float)GAIN_HISTORY_FRAMES;
    if (gainRef < GAIN_MIN_REF) gainRef = GAIN_MIN_REF;

    // Count the fraction of bands that are saturated (normalised > threshold).
    // Uses the same ceiling formula as the output loop below.
    float saturatedFraction = 0.0f;
    for (int b = 0; b < EQ_BANDS; b++) {
        float ceiling    = (bandPeak[b] > gainRef) ? bandPeak[b] : gainRef;
        float normalised = constrain(mag[b] / ceiling, 0.0f, 1.0f);
        if (normalised > SATURATION_THRESHOLD) saturatedFraction += 1.0f;
    }
    saturatedFraction /= (float)EQ_BANDS;

    // sqrtf curve: steep at low saturation so even 1/60 bands roughly doubles
    // the frame weight, flattening toward SATURATION_BOOST at full saturation.
    float boostedMean = frameMean * (1.0f + sqrtf(saturatedFraction) * SATURATION_BOOST);

    // Push boosted frame into circular buffer, evicting the oldest value.
    gainSum -= gainHistory[gainHead];
    gainHistory[gainHead] = boostedMean;
    gainSum += boostedMean;
    gainHead = (gainHead + 1) % GAIN_HISTORY_FRAMES;

    // Recompute gainRef with the new frame included for use in the output loop.
    gainRef = gainSum / (float)GAIN_HISTORY_FRAMES;
    if (gainRef < GAIN_MIN_REF) gainRef = GAIN_MIN_REF;

    // ── Normalise, smooth, output ─────────────────────────────────────────────
    for (int b = 0; b < EQ_BANDS; b++) {
        // Primary normalisation against the 15-second rolling average.
        // bandPeak acts as a secondary ceiling so no single band can exceed 1.0.
        float ceiling    = (bandPeak[b] > gainRef) ? bandPeak[b] : gainRef;
        float normalised = constrain(mag[b] / ceiling, 0.0f, 1.0f);

        // Exponential smoothing: fast attack (SMOOTH_UP), slow decay (SMOOTH_DOWN)
        float smooth = (normalised > prev[b]) ? SMOOTH_UP : SMOOTH_DOWN;
        prev[b] = prev[b] * smooth + normalised * (1.0f - smooth);

        bands[b] = prev[b];
    }

    return true;
}
