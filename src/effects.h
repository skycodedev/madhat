#pragma once
#include <Arduino.h>

// Call when switching modes — zeroes shared state union, preventing the
// previous effect's data from corrupting the next one.
// Pass the mode number you are switching TO.
void resetEffect(uint8_t mode);

// ── Mode 0: Scrolling rainbow text ───────────────────────────────────────────
void effectScrollText(const char* text, bool resetScroll = false);

// ── Mode 1: Plasma / flowing color blobs ─────────────────────────────────────
void effectPlasma();

// ── Mode 2: Fireplace ───────────────────────────────────────────────────────
void effectFire();

// ── Mode 3: Fireworks ──────────────────────────────────────────────────────
void effectFireworks();

// ── GIF image playback (disabled — uncomment to re-enable) ──────────────────
// void effectImage(const uint32_t* frameData,
//                  uint8_t         nFrames,
//                  uint8_t         imgW,
//                  uint8_t         imgH,
//                  uint16_t        delayMs,
//                  bool            reset = false);
