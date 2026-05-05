#pragma once

// =============================================================================
// madHat — Hardware configuration
//
// Target: ESP32-S3 driving a 60×8 WS2811 LED matrix (480 LEDs) with an
// INMP441 I2S MEMS microphone and a mode-select push button + brightness pot.
//
// NOTE: POT_PIN must be an ADC1 pin. ADC2 is unavailable while Wi-Fi is active.
//       GPIO 4 = ADC1_CH4 on the ESP32-S3 — safe to use here.
// =============================================================================

// ── LED matrix ────────────────────────────────────────────────────────────────
#define LED_PIN     6       // GPIO pin connected to the data line
#define LED_TYPE    WS2811
#define COLOR_ORDER GRB
#define NUM_LEDS    480
#define MATRIX_W    60
#define MATRIX_H     8

// ── IO ────────────────────────────────────────────────────────────────────────
#define BUTTON_PIN      5   // GPIO pin for mode button (active low, internal pull-up)
#define POT_PIN         4   // GPIO pin for brightness potentiometer (ADC1_CH4)
#define POT_MIN_BRIGHT  5   // minimum brightness (0-255) so LEDs never fully off
#define POT_MAX_BRIGHT  255 // maximum brightness

// ── Mode selector ─────────────────────────────────────────────────────────────
// Modes: 0=scroll text  1=plasma  2=fire  3=equalizer  4=fireworks
#define NUM_MODES   5
#define DEBOUNCE_MS 200     // ISR software debounce window (ms)

// ── Per-mode frame delays (ms) ────────────────────────────────────────────────
// These set the target inter-frame delay; actual frame time is delay + render.
#define DELAY_SCROLL_MS  40   // ~25 fps
#define DELAY_PLASMA_MS  16   // ~60 fps
#define DELAY_FIRE_MS     0   // 0 = run at max ESP32-S3 throughput (no artificial cap)
#define DELAY_EQ_MS      20   // ~50 fps
#define DELAY_FW_MS      30   // ~33 fps

// ── Initial brightness ────────────────────────────────────────────────────────
// Applied at startup before the potentiometer is first read.
#define INITIAL_BRIGHTNESS 30

// ── Debug logging ─────────────────────────────────────────────────────────────
// Set DEBUG to 1 to enable Serial output (115200 baud).
// Set DEBUG to 0 for production: Serial is never initialised and all
// DEBUG_LOG calls compile away to nothing — zero CPU cost, zero code size.
#define DEBUG 1

#if DEBUG
  #define DEBUG_LOG(...) Serial.printf(__VA_ARGS__)
#else
  #define DEBUG_LOG(...) ((void)0)
#endif
