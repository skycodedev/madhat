#pragma once
#include <Arduino.h>

// ── Audio configuration ───────────────────────────────────────────────────────
#define MIC_SD_PIN   15
#define MIC_WS_PIN   16
#define MIC_SCK_PIN  17

// FFT sample count — must be a power of 2.
// 512 gives good frequency resolution at low CPU cost on ESP32-S3.
#define FFT_SAMPLES  512
#define SAMPLE_RATE  44100   // Hz

// Number of equalizer bands mapped to display columns (MATRIX_W = 60)
#define EQ_BANDS     60

// Initialise I2S and FFT. Call once from setup().
void audioInit();

// Read one FFT frame and compute band magnitudes.
// Results written into bands[EQ_BANDS], each value 0.0–1.0 (normalised).
// Returns true if a fresh frame was computed, false if still waiting for samples.
bool audioGetBands(float bands[EQ_BANDS]);
