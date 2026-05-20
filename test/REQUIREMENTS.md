# madHat — Test Requirements

---

## Overview

Tests split into two tiers.

**Host tests** run on your PC with no hardware. They cover all pure-logic code —
coordinate maths, frequency mapping, bounds guards, config consistency. Run them
with `pio test -e native -v`. They finish in under 2 seconds.

**Manual hardware checks** cover the things that only make sense on the real
device: the button (interrupt-driven, so timing matters), I2S microphone, ADC pot, and visual output. These are
described in the [Hardware checks](#hardware-checks) section below.

**Current count:** 16 host tests across 4 suites, all passing.

---

## Running the host tests

```bash
# Run all four suites
pio test -e native -v

# Run one suite by name
pio test -e native -f test_matrix -v
```

If `pio` is not on your PATH (common when PlatformIO is installed as a VS Code
extension only):

```bash
~/.platformio/penv/bin/pio test -e native -v
```

**In VS Code:** `Ctrl+Shift+P` → **Tasks: Run Task** → **PIO: Run Native Tests**.

---

## Test inventory

### `test/test_matrix/` — 4 tests

Tests for `translatePixel(x, y)` in `src/matrix.h:15`.

Every effect in the firmware calls `translatePixel` to convert a logical pixel
position into a physical LED index. A bug here corrupts the display for every
mode simultaneously, with no crash — just wrong pixels.

The function is copied verbatim into the test file because `src/matrix.h`
includes `Arduino.h`, which does not compile on the host. **If you change the
formula in `src/matrix.h`, update the copy in `test_matrix/test_main.cpp:16`
too** — otherwise the tests pass while the live code is broken.

| Test | What it checks | Failure means |
|---|---|---|
| `test_no_duplicate_indices` | Every `(x, y)` maps to a unique index; all 480 indices [0–479] are used | Two pixels share a physical LED, or some LEDs are never addressable |
| `test_strip_origin_top_right` | `translatePixel(59, 7)` returns `0` — the strip's first LED is the physical top-right pixel | The whole image is offset or mirrored; check strip connection direction |
| `test_even_col_bottom_to_top` | Column 0 (even): bottom pixel (y=7) has index 479, top pixel (y=0) has index 472 | Even columns display upside-down |
| `test_odd_col_top_to_bottom` | Column 1 (odd): top pixel (y=0) has index 471, bottom pixel (y=7) has index 464 | Odd columns display upside-down |

---

### `test/test_audio_bands/` — 5 tests

Tests for the equalizer frequency mapping (`src/audio.cpp:94–102`) and the
auto-gain rolling average (`src/audio.cpp:147–180`).

`audioInit()` precomputes which FFT frequency bins each of the 60 equalizer bars
maps to. `audioGetBands()` then normalises volume automatically using a 750-frame
rolling average (~15 s at 50 fps). These tests check that maths without needing
the I2S microphone.

The test file uses a local copy of the band-boundary computation. **If you change
`FREQ_LOW`, `FREQ_HIGH`, `EQ_BANDS`, `FFT_SAMPLES`, or `SAMPLE_RATE` in
`src/audio.cpp` or `src/audio.h`, re-run these tests.**

| Test | What it checks | Failure means |
|---|---|---|
| `test_every_band_has_at_least_one_bin` | `bandBinLow[b] <= bandBinHigh[b]` for all 60 bands | One or more EQ columns are permanently dark |
| `test_bin_boundaries_in_fft_range` | All bin indices stay in [1, 255] (the usable FFT range) | Equalizer reads from DC bucket or past end of FFT output |
| `test_bands_monotonically_non_decreasing` | Each band's lower bin is ≥ the previous band's lower bin | Frequency order is scrambled — bass bars light up with treble and vice versa |
| `test_gain_circular_buffer_sum_consistency` | Running sum stays accurate after >750 frames (more than one full buffer revolution) | Auto-gain drifts — EQ bars flatten or clip permanently regardless of actual volume |
| `test_gain_ref_floor_on_silence` | Gain reference never reaches zero (divide-by-zero guard) | Firmware crashes or outputs `inf`/`NaN` when the room is silent |

---

### `test/test_effects_bounds/` — 6 tests

Bounds and clipping tests for code in `src/effects.cpp` and `src/main.cpp`.

Several functions write pixels using a signed `y` coordinate that can legitimately
go negative (fireworks trail) or past `MATRIX_H` (burst radius). Without guards,
those writes corrupt `leds[]` and potentially adjacent memory. These tests use a
stub `leds` array with two sentinel guard pixels past the end and check those
sentinels are not overwritten.

Two tests (`test_fire_heat_index_in_bounds` and `test_fire_propagation_row_never_exceeds_height`)
check local copies of index formulas, **not the actual `fireGet`/`fireSet` macros
in `effects.cpp`**. If you change `FIRE_RISE` at `effects.cpp:30` or the heat
array formula at `effects.cpp:279`, update the matching constants in the test too.

| Test | Source location | What it checks | Failure means |
|---|---|---|---|
| `test_fwSetPixel_clips_invalid_y` | `effects.cpp:448–451` | `fwSetPixel` does not write for `y < 0` or `y >= MATRIX_H` | Firework trail writes outside `leds[]`, corrupting memory |
| `test_fire_heat_index_in_bounds` | `effects.cpp:279–280` | Fire heat index `x * FIRE_H + y` stays within `[0, FIRE_W*FIRE_H)` for all valid `x, y` | Fire effect writes outside the heat array, corrupting adjacent state |
| `test_fire_propagation_row_never_exceeds_height` | `effects.cpp:344` | Propagation loop never reads row `y + FIRE_RISE` when that row ≥ `MATRIX_H` | Fire reads heat from outside the array during propagation |
| `test_scroll_font_guard_rejects_out_of_range_chars` | `effects.cpp:217` | Characters outside `[0x20, 0x7A]` are rejected before font table lookup | Scroll text with unsupported characters reads garbage font data, lighting wrong pixels |
| `test_scroll_charIdx_always_within_text_length` | `effects.cpp:237–243` | Scroll character index never reaches `textLen` for any scroll position | Scroll reads past the end of the text string |
| `test_brightness_map_stays_in_range` | `main.cpp:43` | `map(raw, 0, 4095, 5, 255)` stays in `[POT_MIN_BRIGHT, POT_MAX_BRIGHT]` for all 4096 ADC values | Brightness goes below minimum (display appears dead) or above maximum |

**Note on the scroll test string:** `test_scroll_charIdx_always_within_text_length`
uses `"HELLO WORLD"` as its test string. The live scroll text in `main.cpp:86` is
`"FUCKING HIPPIES!!"`. If you change the scroll text, add it to this test too.

---

### `test/test_config/` — compile-time checks only

These are `static_assert` statements, not runtime tests. If any assertion fails,
**the build stops with an error** — you will not be able to flash bad firmware.

The single Unity test `test_static_asserts_passed` is a placeholder: if it
appears in the output, the build succeeded and all assertions are true.

| Assertion | What it prevents |
|---|---|
| `NUM_LEDS == MATRIX_W * MATRIX_H` | `translatePixel()` writing past the end of `leds[]` |
| `EQ_BANDS == MATRIX_W` | `effectEqualizer()` reading past the end of the `bands[]` array |
| `NUM_LEDS <= 65535` | `translatePixel()` return value overflowing `uint16_t` |
| `POT_MIN_BRIGHT > 0` | Display going fully dark at minimum pot position |
| `POT_MIN_BRIGHT < POT_MAX_BRIGHT` | Brightness range being inverted |
| `MATRIX_W > 0 && MATRIX_H > 0` | Zero-size matrix causing division by zero |
| `NUM_MODES > 0` | Mode wrap `% NUM_MODES` causing division by zero |

---

## Hardware checks

These cannot be automated without a board. Do them manually after flashing.

### Boot

Open `pio device monitor` immediately after flashing. Within 3 seconds you should see:

```
madHat firmware starting — 480 LEDs, 6 modes
FastLED initialised
Audio initialised
Setup complete — entering main loop
```

If the output stops at `FastLED initialised` and reboots, the I2S driver failed —
check that GPIO 15, 16, 17 are wired to the INMP441 and match `src/audio.h:3–5`.

### Mode switch (`src/main.cpp:32–38`)

With `DEBUG` set to `1` in `src/config.h`, each button press prints `Mode changed → N`.
Verify:

- Each press advances N by 1
- After mode 5, the next press shows `Mode changed → 0`
- Pressing the button multiple times within 200 ms registers only once (the
  debounce window is `DEBOUNCE_MS` in `src/config.h:30`)

### Brightness (`src/main.cpp:41–44`, `src/config.h:43`)

- At power-on, the display should start at low brightness (~12%) before the pot is turned — this is `INITIAL_BRIGHTNESS = 30` in `src/config.h`
- Turning the pot in any mode should change brightness within one frame (~40 ms)
- At minimum pot position the display should glow faintly — not go fully dark
- At maximum position the display should reach full brightness

### Equalizer (`src/audio.cpp`, `src/effects.cpp:377–427`)

Switch to mode 3 and tap the mic or play music nearby. The 60 bars should react
to the sound within one frame. If the bars are frozen at zero, the I2S driver is
not reading samples — re-check the mic wiring.

### GIF pre-build

After adding or changing a GIF in `images/`, run:

```bash
pio run
```

Check the build output for the GIF conversion log lines. Then verify the key defines in the generated header:

```bash
grep "^#define DEMO" src/generated/demo.h
```

Expected output:

```
#define DEMO_W       60
#define DEMO_H       8
#define DEMO_FRAMES  4
#define DEMO_DELAY   120  // ms per frame
```

`DEMO_W` must be 60 and `DEMO_H` must be 8. Any other values mean the script
cropped to the wrong dimensions.

---

## Adding a new test suite

1. Create a directory `test/test_<name>/`
2. Create `test/test_<name>/test_main.cpp`
3. Start the file with:
   ```cpp
   #include "../native_shims.h"
   #include <unity.h>
   ```
4. Write test functions with the signature `void test_<something>(void)`
5. Register them in `main()` with `RUN_TEST(test_<something>)`
6. Include `void setUp(void) {}` and `void tearDown(void) {}` — Unity requires them

If your test needs a stub LED array, use the sentinel pattern from
`test_effects_bounds/test_main.cpp:14–27`: size it as `CRGB leds[NUM_LEDS + 2]`,
fill with a known colour, and assert the last two slots are unchanged after the
function runs.

If the source function you want to test includes `Arduino.h` or `FastLED.h`, you
cannot include it directly. Either copy the specific logic under test into the
test file (label it with the source line number) or extend `native_shims.h` with
the minimum stub needed.

---

## Known gaps

The following are not covered by automated tests. They are the next priorities if
the test suite is expanded.

| Gap | Why it matters | How to test today |
|---|---|---|
| `(currentMode + 1) % NUM_MODES` wrap in `myISR()` (`main.cpp:38`) | Most common regression when adding a new mode — wrong `NUM_MODES` causes skip or infinite loop | Manual: press the button from mode 5, confirm mode 0 follows |
| `resetEffect()` post-conditions (`effects.cpp:180–201`) | Stale state from a previous mode causes visual glitches on mode switch | Manual: switch to Fire mode and check flames start from scratch; switch to Eyes and check eyes start at columns 15 and 45 |
| `eyeSetPixel` bounds guard (`effects.cpp:573–577`) | Parallel to the tested `fwSetPixel` guard — same risk, different function | Not tested; add to `test_effects_bounds` using the same sentinel pattern |
| I2S driver init return codes (`audio.cpp:88–90`) | Silent failure leaves Equalizer mode permanently blank | Manual: check "Audio initialised" appears in serial monitor on boot |
| GIF pixel data values | Generated C++ compiles but pixel values could be wrong if Pillow version changes | Manual: `grep "^#define DEMO" src/generated/demo.h` after running `pio run` |
