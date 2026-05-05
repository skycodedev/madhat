#pragma once
#include <Arduino.h>

// Call when switching modes — zeroes shared state union.
// Pass the mode number you are switching TO.
void resetEffect(uint8_t mode);

// Mode 0: Scrolling rainbow text
void effectScrollText(const char* text, bool resetScroll = false);

// Mode 1: Plasma / flowing color blobs
void effectPlasma();

// Mode 2: Fireplace
void effectFire();

// Mode 3: Graphic equalizer (requires INMP441 via I2S, see audio.h)
void effectEqualizer();

// Mode 4: Fireworks
void effectFireworks();
