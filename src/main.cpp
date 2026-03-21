#include <Arduino.h>
#include <FastLED.h>  // Use Version 3.1.3

// ── LEDs ─────────────────────────────────────────────────────────────────────
#define LED_PIN     6
#define LED_TYPE    WS2811
#define COLOR_ORDER GRB
#define NUM_LEDS    480

CRGB             leds[NUM_LEDS];
CRGBPalette16    currentPalette;
TBlendType       currentBlending;
uint8_t          brightness;

// ── IO ────────────────────────────────────────────────────────────────────────
#define POT_PIN    4
#define BUTTON_PIN 2

// ── Emergency party switch ────────────────────────────────────────────────────
#define OLD_MODE false

volatile bool ledState = true;

// ── Data structures ───────────────────────────────────────────────────────────
typedef struct images_old {
    byte sweden[73] = {
        1  , 32 , 64 , 96 , 128, 160, 192, 224, 0  ,
        32 , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  ,
        64 , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  ,
        96 , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  ,
        224, 224, 224, 224, 224, 224, 224, 224, 224,
        160, 0  , 0  , 0  , 160, 0  , 0  , 0  , 0  ,
        192, 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  ,
        224, 0  , 0  , 0  , 0  , 0  , 0  , 0  , 0  , 255
    };
} images_old;

// ── Forward declarations ──────────────────────────────────────────────────────
void displayLogo(byte image[]);
void paintLogo(byte image[], uint8_t startColumn);
void clearAllPixels();
uint16_t translatePixel(uint8_t x, uint8_t y);
void myISR();

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), myISR, FALLING);
    pinMode(LED_BUILTIN, OUTPUT);

    Serial.begin(9600);

    FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS)
           .setCorrection(TypicalLEDStrip);
    FastLED.setBrightness(30);

    currentPalette  = RainbowColors_p;
    currentBlending = LINEARBLEND;
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
    // brightness = analogRead(POT_PIN) / 20;
    // if (brightness < 10) brightness = 5;
    images_old test;
    displayLogo(test.sweden);
}

// ── Functions ─────────────────────────────────────────────────────────────────

void displayLogo(byte image[]) {
    for (uint8_t column = 0; column <= 60; column++) {
        clearAllPixels();
        paintLogo(image, column);
        delay(100);
    }
}

void paintLogo(byte image[], uint8_t startColumn) {
    uint16_t pixel = 0;
    while (image[pixel] != 255) {
        uint8_t x = (startColumn + (pixel % 9)) % 60;
        uint8_t y = pixel / 8;

        if (image[pixel] == 0) {
            leds[translatePixel(x, y)] = ColorFromPalette(HeatColors_p, image[pixel], 0, currentBlending);
        } else if (image[pixel] == 254) {
            leds[translatePixel(x, y)] = ColorFromPalette(HeatColors_p, image[pixel], 255, currentBlending);
        } else {
            leds[translatePixel(x, y)] = ColorFromPalette(currentPalette, image[pixel], 255, currentBlending);
        }
        pixel++;
    }
    FastLED.show();
}

void clearAllPixels() {
    for (uint16_t i = 0; i < NUM_LEDS; i++) {
        leds[i] = ColorFromPalette(currentPalette, 0, 0, currentBlending);
    }
}

uint16_t translatePixel(uint8_t x, uint8_t y) {
    bool     ledDirection     = x % 2;
    uint16_t translatedPixel  = ledDirection ? (8 * x) + y : (8 * x) + (7 - y);
    return 479 - translatedPixel;
}

void myISR() {
    ledState = !ledState;
}
