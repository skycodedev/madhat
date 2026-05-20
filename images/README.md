# images/

Place your GIF files here. They are automatically converted to C++ headers
at compile time by `scripts/convert_gifs.py`.

## Rules

- Any `*.gif` file dropped here will generate `src/generated/<name>.h` and
  `src/generated/<name>.cpp` before the build starts.
- Frames are scaled and centre-cropped to **60 × 8** pixels (the LED matrix size).
- The frame delay stored in the first GIF frame is used for all frames.

## Using a generated image in main.cpp

For a file called `demo.gif` the generated symbols are:

| Symbol         | Type                        | Description         |
|----------------|-----------------------------|---------------------|
| `DEMO_DATA`    | `uint32_t[N][480]` PROGMEM  | All frame pixel data |
| `DEMO_FRAMES`  | `#define`                   | Number of frames    |
| `DEMO_W`       | `#define`                   | Width (always 60)   |
| `DEMO_H`       | `#define`                   | Height (always 8)   |
| `DEMO_DELAY`   | `#define`                   | ms per frame        |

To use it, include the generated header in `main.cpp` and add a new case to
the mode `switch` in `loop()`. See the main README for the full 5-step walkthrough.
