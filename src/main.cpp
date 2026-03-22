#include <Arduino.h>
#include <FastLED.h>
#include "config.h"
#include "effects.h"

// ── LED array (shared with effects.cpp via extern) ────────────────────────────
CRGB leds[NUM_LEDS];

// ── Mode state ────────────────────────────────────────────────────────────────
// Modes:  0 = scroll text
//         1 = plasma
//         2 = fire
//         3 = fireworks
#define NUM_MODES 4

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
    resetEffect(0);
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {

    if (modeChanged) {
        modeChanged = false;
        FastLED.clear();
        FastLED.show();
        resetEffect(currentMode);
        return;
    }

    switch (currentMode) {

        case 0:
            effectScrollText("FUCKING HIPPIES ");
            delay(40);
            break;

        case 1:
            effectPlasma();
            delay(8);
            break;

        case 2:
            effectFire();
            delay(20);
            break;

        case 3:
            effectFireworks();
            delay(15);
            break;
    }
}
