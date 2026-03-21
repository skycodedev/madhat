#include <Arduino.h>
#include <FastLED.h>
#include "config.h"
#include "effects.h"

// ── Include generated GIF headers (created at build time from images/*.gif) ──
// #if __has_include("generated/demo.h")
//   #include "generated/demo.h"
//   #define HAS_DEMO_GIF
// #endif

// ── LED array (shared with effects.cpp via extern) ────────────────────────────
CRGB leds[NUM_LEDS];

// ── Mode state ────────────────────────────────────────────────────────────────
// Modes:  0 = scroll text
//         1 = plasma
//         2 = fire
// #ifdef HAS_DEMO_GIF
//   #define NUM_MODES 4   // add mode 3 = GIF when re-enabling
// #else
#define NUM_MODES 3
// #endif

volatile bool    modeChanged = false;
volatile uint8_t currentMode = 0;

// ── Button debounce ───────────────────────────────────────────────────────────
#define DEBOUNCE_MS 200

void myISR() {
    static volatile uint32_t lastTrigger = 0;
    uint32_t now = millis();
    if (now - lastTrigger < DEBOUNCE_MS) return;
    lastTrigger = now;
    modeChanged = true;
    currentMode = (currentMode + 1) % NUM_MODES;
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), myISR, FALLING);

    FastLED.addLeds<WS2811, LED_PIN, GRB>(leds, NUM_LEDS)
           .setCorrection(TypicalLEDStrip);
    FastLED.setBrightness(30);
    FastLED.clear();
    FastLED.show();
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {

    // ── Mode 0: Scrolling rainbow text ───────────────────────────────────────
    if (currentMode == 0) {
        if (modeChanged) {
            modeChanged = false;
            effectScrollText("HELLO", true);
            return;
        }
        effectScrollText("HELLO");
        delay(40);

    // ── Mode 1: Plasma ────────────────────────────────────────────────────────
    } else if (currentMode == 1) {
        if (modeChanged) {
            modeChanged = false;
            FastLED.clear();
        }
        effectPlasma();
        delay(16);

    // ── Mode 2: Fire ──────────────────────────────────────────────────────────
    } else if (currentMode == 2) {
        if (modeChanged) {
            modeChanged = false;
            FastLED.clear();
        }
        effectFire();
        delay(40);

    // ── Mode 3: GIF playback (disabled) ──────────────────────────────────────
    // } else if (currentMode == 3) {
    //     if (modeChanged) {
    //         modeChanged = false;
    //         effectImage(nullptr, 0, 0, 0, 0, true);
    //         FastLED.clear();
    //         return;
    //     }
    //     effectImage(
    //         &DEMO_DATA[0][0],
    //         DEMO_FRAMES,
    //         DEMO_W,
    //         DEMO_H,
    //         DEMO_DELAY
    //     );
    }
}
