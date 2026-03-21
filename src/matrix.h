#pragma once
#include <Arduino.h>
#include "config.h"

// Maps logical (x, y) coordinates to the physical LED index.
// The strip is wired in vertical columns, zigzag, reversed.
inline uint16_t translatePixel(uint8_t x, uint8_t y) {
    bool     ledDirection    = x % 2;
    uint16_t translatedPixel = ledDirection ? (8 * x) + y : (8 * x) + (7 - y);
    return 479 - translatedPixel;
}
