# madHat

*ESP32-S3 firmware for a top hat with a 60×8 WS2811 LED matrix wrapped around the brim.*

---

## What is this

madHat is a wearable art piece — a top hat with a continuous 480-pixel LED display
running around its full circumference. The LED strip forms 60 vertical columns of
8 pixels; column 0 and column 59 are physically side-by-side, so images and
animations wrap seamlessly around the hat with no visible seam.

The hat is built to be worn at parties and festivals. It runs six animated display
modes — including a real-time audio equalizer that reacts to the music around it —
and is designed to be easy to customise: change the scroll text, tune the effects,
or drop in a GIF and rebuild.

**Highlights:**

- Full-circumference seamless display — the strip is one continuous ring
- Live mic-reactive equalizer with 15-second auto-gain (no manual level setting)
- Five purely visual effects: fireworks, scrolling text, fire, plasma, and rolling eyes
- Single button cycles modes instantly (interrupt-driven, debounced)
- GIF animation pipeline — drop a `.gif` in `images/`, rebuild, done

---

## Quick Start

1. Wire the hardware to the ESP32-S3 (see [Hardware](#hardware) below)
2. Install [PlatformIO](https://platformio.org/install) (VS Code extension or CLI)
3. Clone this repository and open it in PlatformIO
4. Run `pio run --target upload` to build and flash
5. Press the button to cycle through modes; turn the knob to adjust brightness

---

## Hardware

### Components

| Component       | Part / Detail                                      |
|-----------------|----------------------------------------------------|
| MCU             | ESP32-S3-DevKitC-1, 240 MHz dual-core Xtensa LX7  |
| LED matrix      | 480x WS2811, 60 columns x 8 rows, serpentine wiring |
| Microphone      | INMP441 I2S MEMS (44100 Hz, 24-bit, left channel)  |
| Mode button     | Tactile push button, active-low on GPIO 5          |
| Brightness knob | 10 kOhm potentiometer on GPIO 4                    |

### GPIO wiring

| GPIO | Connect to         | Notes                                                  |
|------|--------------------|--------------------------------------------------------|
| 6    | LED strip data in  | WS2811, GRB colour order, 480 LEDs                     |
| 5    | Button to GND      | Interrupt-driven (ISR); fires on button press          |
| 4    | Pot wiper          | Analog read, ADC1 — safe to use while Wi-Fi is off     |
| 15   | INMP441 SD         | I2S serial data                                        |
| 16   | INMP441 WS         | I2S word select (left/right clock)                     |
| 17   | INMP441 SCK        | I2S bit clock                                          |

### Physical assembly notes

**Power:** The LED strip can draw up to 1 A at full white brightness (480 LEDs x ~20 mA
each). Power the strip from a dedicated 5 V rail — do not draw from the ESP32-S3's 3.3 V
pin. The ESP32-S3 DevKit board itself can be powered from USB or from the 5 V rail via its
`5V` pin.

**Data line:** Place a 300–500 Ohm resistor in series on the LED data line (GPIO 6) to
protect against ringing and signal reflections on longer wire runs.

**Ring topology:** The LED strip starts at the physical top-right of the hat brim and runs
counter-clockwise, column by column. Column 0 (logical left) and column 59 (logical right)
are physically adjacent. If the image appears mirrored, you have fed from the wrong end of
the strip — swap the data connection to the other end.

---

## LED matrix wiring

The LED strip is wired in a **serpentine (zigzag)** pattern: even-numbered columns run
bottom-to-top, odd-numbered columns run top-to-bottom. The strip's first LED (index 0)
is at the physical top-right corner of the matrix.

`src/matrix.h` provides `translatePixel(x, y)`, which converts logical coordinates
(column `x` counting from the left, row `y` counting from the top) into the correct
physical LED index in the strip. All effect code calls this function — you do not need
to think about the physical wiring when writing effects.

```cpp
// Example: set the top-left pixel to red
leds[translatePixel(0, 0)] = CRGB::Red;
```

---

## Display modes

The push button (GPIO 5) cycles through six modes in order. The potentiometer (GPIO 4)
adjusts brightness continuously in every mode.

| # | Mode        | Description |
|---|-------------|-------------|
| 0 | Fireworks   | Up to 5 simultaneous rockets rise and burst in circle, cross, or ring patterns. The whole buffer fades to black each frame, leaving a comet-trail afterglow. |
| 1 | Scroll Text | Horizontally scrolling rainbow text using the built-in 5x7 bitmap font. The hue sweeps across all 60 columns and cycles continuously. Default text: `"FUCKING HIPPIES!!"` |
| 2 | Fire        | Cellular-automaton fire simulation. Eight ignition seeds at the base cycle positions randomly; floating embers drift upward above the flames. |
| 3 | Equalizer   | Real-time 60-band audio spectrum analyzer. The INMP441 microphone feeds a 512-point FFT (with a Hann window to reduce spectral bleed). A 15-second rolling auto-gain window normalizes loudness automatically. |
| 4 | Plasma      | Two layers of Perlin noise (a smooth randomness function) blend on different time axes, creating organic lava-lamp colour blobs. |
| 5 | Eyes        | Two independent eyeballs roll across the wrapping display. Each has a white sclera, randomly placed coloured veins, and a directional pupil that slowly shifts hue. |

**Changing the scroll text:** open `src/main.cpp` and find line 86 in `loop()`:

```cpp
case 1: effectScrollText("FUCKING HIPPIES!!"); delay(DELAY_SCROLL_MS); break;
```

Replace the string with whatever you want to display. Any printable ASCII characters
from space through lowercase `z` are supported.

---

## Prerequisites

- [PlatformIO](https://platformio.org/install) — CLI or IDE extension for VS Code
- Python 3.7+ — required by the GIF pre-build script (usually already present on macOS/Linux)
- USB cable capable of data transfer (not charge-only)

PlatformIO will automatically fetch the two Arduino libraries (`FastLED` and `arduinoFFT`)
on the first build. Python `Pillow` is auto-installed by the pre-build script if absent.

---

## Build and flash

```bash
# Build only
pio run

# Build and flash to the connected ESP32-S3
pio run --target upload

# Open the serial monitor (115200 baud) to read debug output
pio device monitor
```

The pre-build script `scripts/convert_gifs.py` runs automatically before every build.
It converts any `*.gif` in `images/` into C++ pixel arrays in `src/generated/`.

---

## Configuration

All GPIO pin assignments, matrix dimensions, and per-mode frame delays live in
[`src/config.h`](src/config.h). Key values:

| Constant             | Default | What it controls |
|----------------------|---------|------------------|
| `MATRIX_W`           | 60      | LED columns (must also equal `EQ_BANDS` in `src/audio.h`) |
| `MATRIX_H`           | 8       | LED rows |
| `DEBOUNCE_MS`        | 200     | Button debounce window in ms — increase if you get double-triggers |
| `INITIAL_BRIGHTNESS` | 30      | Startup brightness before the pot is first read (~12% of max) |
| `DEBUG`              | 1       | Set to `0` before a production build to silence all serial output |
| `DELAY_PLASMA_MS`    | 16      | Frame delay for Plasma mode (~60 fps) |
| `DELAY_EQ_MS`        | 20      | Frame delay for Equalizer mode (~50 fps) |
| `DELAY_FW_MS`        | 30      | Frame delay for Fireworks mode (~33 fps) |
| `DELAY_SCROLL_MS`    | 40      | Frame delay for Scroll Text mode (~25 fps) |
| `DELAY_EYES_MS`      | 40      | Frame delay for Eyes mode (~25 fps) |
| `DELAY_FIRE_MS`      | 0       | Fire runs at maximum CPU throughput (no cap) |

Visual tuning constants for each effect (fire shape, EQ smoothing, firework size,
eye speed, plasma scale, etc.) are defined as named constants near the top of
[`src/effects.cpp`](src/effects.cpp).

### Debug output

When `DEBUG 1` is set in `src/config.h`, the firmware prints startup messages and
mode-change events to the serial port at 115200 baud. Read them with:

```bash
pio device monitor
```

Set `DEBUG` to `0` for a production build. All `DEBUG_LOG(...)` calls compile away
completely — zero runtime cost, zero extra code size.

---

## Extending the firmware

### Adding a new coded effect

1. Add a state struct for your effect inside the `union` in `src/effects.cpp`
   (or skip this if your effect is stateless)
2. Add an initialisation block for the new mode number in `resetEffect()` in
   `src/effects.cpp`
3. Write the `effectMyThing()` function body in `src/effects.cpp` and declare it
   in `src/effects.h`
4. Increment `NUM_MODES` in `src/config.h` and add a matching `DELAY_*_MS` constant
5. Add a new `case` to the `switch` in `src/main.cpp`:

```cpp
case 6: effectMyThing(); delay(DELAY_MYTHING_MS); break;
```

The shared `leds[]` array and `translatePixel(x, y)` are available to all effects.
See any existing effect in `src/effects.cpp` as a starting point.

### Adding GIF animations

1. Place any `*.gif` file in the `images/` directory
2. Run `pio run` — the pre-build script scales and centre-crops each frame to 60x8
   pixels and generates `src/generated/<name>.h` and `src/generated/<name>.cpp`
3. Include the generated header in `src/main.cpp` and add a new case to the mode
   `switch` (follow the "Adding a new coded effect" steps above)

For a file named `demo.gif` the generated symbols are:

| Symbol        | Type                       | Value        |
|---------------|----------------------------|--------------|
| `DEMO_FRAMES` | `#define`                  | frame count  |
| `DEMO_DELAY`  | `#define`                  | ms per frame |
| `DEMO_W`      | `#define`                  | 60           |
| `DEMO_H`      | `#define`                  | 8            |
| `DEMO_DATA`   | `uint32_t[N][480]` PROGMEM | pixel data   |

Note: generated `.h` and `.cpp` files are excluded from git (see `.gitignore`).
Regenerate them locally with `pio run`.

---

## Dependencies

| Library       | Version  | Role |
|---------------|----------|------|
| FastLED       | ^3.10.3  | WS2811 LED driver, colour math, Perlin noise, trig helpers |
| arduinoFFT    | ^2.0.4   | 512-point FFT for the Equalizer mode |
| Python Pillow | latest   | Pre-build GIF-to-C++ converter (auto-installed if absent) |

Managed by PlatformIO — `lib_deps` in `platformio.ini` fetches the Arduino libraries
automatically on the first build.

---

## Project structure

```text
madHat/
├── src/
│   ├── main.cpp          Entry point — setup(), loop(), mode ISR, brightness pot
│   ├── config.h          All GPIO pins, matrix size, frame delays, debug flag
│   ├── audio.cpp/.h      I2S mic driver + 60-band auto-gain FFT engine
│   ├── effects.cpp/.h    All six visual effect implementations, shared state union
│   ├── matrix.h          translatePixel(x, y) — logical to physical LED index
│   ├── font5x7.h         91-character 5x7 bitmap font (ASCII space through z)
│   └── generated/        Auto-generated GIF headers — not committed to git
├── images/               Source GIF files + instructions for adding GIF modes
├── scripts/
│   └── convert_gifs.py   Pre-build script: scales GIFs to 60x8, writes C++ arrays
└── platformio.ini        PlatformIO build config — ESP32-S3, -O2, Arduino framework
```
