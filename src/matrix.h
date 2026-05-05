#pragma once
#include <Arduino.h>
#include "config.h"

// Maps logical (x, y) coordinates to the physical LED index.
//
// Physical wiring topology:
//   - LEDs are arranged in vertical columns (x selects column, y selects row).
//   - Odd columns run top-to-bottom; even columns run bottom-to-top (zigzag).
//   - The strip origin (index 0) is at the physical top-right corner, so the
//     entire translated index is mirrored: index = (NUM_LEDS - 1) - raw.
//
//   Column 0 (even): bottom→top  |  Column 1 (odd): top→bottom  | ...
//
inline uint16_t translatePixel(uint8_t x, uint8_t y) {
    bool     ledDirection    = x % 2;
    uint16_t translatedPixel = ledDirection
                                 ? (MATRIX_H * x) + y
                                 : (MATRIX_H * x) + ((MATRIX_H - 1) - y);
    return (NUM_LEDS - 1) - translatedPixel;
}
