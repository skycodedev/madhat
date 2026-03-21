# images/

Place your GIF files here. They are automatically converted to C++ headers
at compile time by `scripts/convert_gifs.py`.

## Rules

- Any `*.gif` file dropped here will generate `src/generated/<name>_gif.h` and
  `src/generated/<name>_gif.cpp` before the build starts.
- Frames are scaled and centre-cropped to **60 × 8** pixels (the LED matrix size).
- The frame delay stored in the GIF is preserved.

## Using a generated image in main.cpp

For a file called `demo.gif` the generated symbols are:

| Symbol | Type | Description |
|---|---|---|
| `DEMO_DATA` | `uint32_t[N][480]` PROGMEM | All frame pixel data |
| `DEMO_FRAMES` | `#define` | Number of frames |
| `DEMO_W` | `#define` | Width (always 60) |
| `DEMO_H` | `#define` | Height (always 8) |
| `DEMO_DELAY` | `#define` | ms per frame |

`main.cpp` detects the header automatically via `__has_include` and adds the
GIF as mode 2 (button cycles: text → plasma → gif → text → ...).

For additional GIFs (e.g. `cat.gif`), include `generated/cat_gif.h` and call
`effectImage(&CAT_GIF_DATA[0][0], CAT_GIF_FRAMES, ...)` in a new mode.
