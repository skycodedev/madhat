// =============================================================================
// madHat — main.cpp
//
// Entry point for the madHat ESP32-S3 LED matrix firmware.
// Hardware: 60×8 WS2811 LED matrix, INMP441 I2S mic, mode button, brightness pot.
//
// Mode ISR: button on BUTTON_PIN cycles through NUM_MODES modes with
// DEBOUNCE_MS software debounce.  currentMode and modeChanged are volatile
// uint8_t / bool — both are written inside the ISR and read in loop().
// uint8_t read/write is atomic on Xtensa, so no critical section is required,
// but the variables are declared volatile to prevent compiler reordering.
// =============================================================================

#include <Arduino.h>
#include <FastLED.h>
#include "config.h"
#include "effects.h"
#include "audio.h"

// LED array (shared with effects.cpp via extern)
CRGB leds[NUM_LEDS];

// ── GIF image mode (auto-detected) ────────────────────────────────────────────
// If a GIF has been placed in images/ and the pre-build script has generated
// src/generated/demo.h, the GIF playback mode is compiled in automatically.
// See images/README.md for how to add GIF modes.
// TODO: wire up __has_include("generated/demo.h") here to add a GIF mode.

volatile bool    modeChanged = false;
volatile uint8_t currentMode = 0;

void IRAM_ATTR myISR() {
    static volatile uint32_t lastTrigger = 0;
    uint32_t now = millis();
    if (now - lastTrigger < DEBOUNCE_MS) return;
    lastTrigger = now;
    modeChanged = true;
    currentMode = (currentMode + 1) % NUM_MODES;
}

void updateBrightness() {
    uint16_t raw = analogRead(POT_PIN);
    uint8_t  bri = map(raw, 0, 4095, POT_MIN_BRIGHT, POT_MAX_BRIGHT);
    FastLED.setBrightness(bri);
}

void setup() {
#if DEBUG
    Serial.begin(115200);
#endif
    DEBUG_LOG("\n\nmadHat firmware starting — %d LEDs, %d modes\n",
              NUM_LEDS, NUM_MODES);

    pinMode(BUTTON_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), myISR, FALLING);

    FastLED.addLeds<WS2811, LED_PIN, GRB>(leds, NUM_LEDS)
           .setCorrection(TypicalLEDStrip);
    FastLED.setBrightness(INITIAL_BRIGHTNESS);
    FastLED.clear();
    FastLED.show();
    DEBUG_LOG("FastLED initialised\n");

    audioInit();
    DEBUG_LOG("Audio initialised\n");

    resetEffect(0);
    DEBUG_LOG("Setup complete — entering main loop\n");
}

void loop() {
    // Read potentiometer every iteration (~10 µs ADC read, negligible at 25-60 fps)
    updateBrightness();

    if (modeChanged) {
        modeChanged = false;
        DEBUG_LOG("Mode changed → %d\n", currentMode);
        FastLED.clear();
        FastLED.show();
        resetEffect(currentMode);
        return;
    }

    switch (currentMode) {
        case 0: effectScrollText("HELLO"); delay(DELAY_SCROLL_MS); break;
        case 1: effectPlasma();            delay(DELAY_PLASMA_MS); break;
        case 2: effectFire();              delay(DELAY_FIRE_MS);   break;
        case 3: effectEqualizer();         delay(DELAY_EQ_MS);     break;
        case 4: effectFireworks();         delay(DELAY_FW_MS);     break;
    }
}
