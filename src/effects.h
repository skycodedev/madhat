#pragma once
#include <Arduino.h>

// ── Mode 0: Scrolling rainbow text ───────────────────────────────────────────
// Call once per frame. Pass resetScroll=true to restart from the beginning.
void effectScrollText(const char* text, bool resetScroll = false);

// ── Mode 1: Plasma / flowing color blobs ─────────────────────────────────────
void effectPlasma();

// ── Mode 3: Fireplace ───────────────────────────────────────────────────────
void effectFire();

// ── GIF image playback (disabled — uncomment to re-enable) ──────────────────
// void effectImage(const uint32_t* frameData,
//                  uint8_t         nFrames,
//                  uint8_t         imgW,
//                  uint8_t         imgH,
//                  uint16_t        delayMs,
//                  bool            reset = false);
