# Pico 2 W LED Wall Controller

Firmware for a **75 × 21 (1575-pixel) WS2815 LED wall** driven by a
**Raspberry Pi Pico 2 W (RP2350)**. It renders procedural and pre-baked
animations and exposes a small HTTP API, with mDNS discovery, for control.

## Quick start

1. Copy `include/secrets.hpp.example` to `include/secrets.hpp` and set your
   Wi-Fi credentials.
2. Build and flash the firmware (see [Building](#building)).
3. Open `http://ledfal.local` for the API index, or start the web UI:
   `cd web && npm install && npm run dev`.

## Features

- 21 column strips driven **in parallel** over PIO + DMA (~2.25 ms per frame
  plus a 400 µs latch gap)
- A single universal PIO program drives any bus from **1 to 32 lanes**
  (WS2812/WS2815, 800 kbit/s)
- Animations: a scrolling **Rainbow**, a 1-bit **Bad Apple** video (5258
  frames @ 24 fps) embedded in flash via `.incbin`, plus procedural
  **Fire** (two palettes), **Rain**, **Stars**, **Plasma**, **RainFill**
  (fills then flushes) and an auto-playing **Tetris** demo
- Hand-rolled HTTP server on raw lwIP TCP, discoverable at
  `http://ledfal.local`
- **Non-blocking Wi-Fi** with automatic reconnect and exponential backoff; the
  on-board LED blinks until the link is up
- Runtime global **brightness** (0–255), applied centrally to animations and
  uploaded frames
- Vertical/horizontal **flip** and a configurable **wire color order**
  (RGB/GRB) so the firmware matches the physical wiring
- Raw frame upload as a base64 blob, with a small Python toolchain and a
  **React control panel** (`web/`)
- **CORS-enabled** API, so a browser page can talk to the device directly

## Hardware

- Raspberry Pi Pico 2 W (RP2350, 4 MB flash)
- 21 × WS2815 strips, 75 LEDs each (12 V) — 1575 LEDs total
- 3 × 74HCT541 octal level shifters (3.3 V → 5 V data)
- 47 Ω series resistor per data lane, common ground

### Pin map

| GPIO | Use |
| --- | --- |
| GP0–GP13 | columns 0–13 (bus A, 14 lanes) |
| GP14–GP15 | reserved for buttons (not implemented) |
| GP16–GP22 | columns 14–20 (bus B, 7 lanes) |
| GP23–GP25, GP29 | CYW43 wireless (do not use) |

All of this is defined in `include/config.hpp`.

## Repository layout

```
src/        firmware: main loop, LED/PIO driver, HTTP server, routes, animations, Wi-Fi
include/    headers and config.hpp (all tunables); include/generated/ holds the embedded video
helpers/    Python tools: Bad Apple generator, image/base64 tools, noise generator, color test
web/        React + Vite control panel (brightness, animations, live preview, frame editor)
CMakeLists.txt
```

## Building

Requires the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk)
(2.3.1) and the ARM toolchain.

```bash
export PICO_SDK_PATH=/path/to/pico-sdk   # Windows: set the env var in System Properties
cmake -S . -B build
cmake --build build
```

Flash `build/my_pico_project.uf2` by copying it to the `RPI-RP2` mass-storage
drive that appears when the board is in BOOTSEL mode (or use `picotool`).

## Configuration

- `include/config.hpp` — matrix geometry, bus/pin layout, orientation
  (`flipVertical` / `flipHorizontal`), wire color order (`wireOrderRGB`),
  default brightness, and animation/Wi-Fi timing. Start here.
- Wi-Fi credentials: copy `include/secrets.hpp.example` to
  `include/secrets.hpp` and fill in `WIFI_SSID` / `WIFI_PASSWORD`. The file is
  gitignored.

## HTTP API

The device serves on port 80 as `http://ledfal.local`. Responses carry
`Access-Control-Allow-Origin: *` and `OPTIONS` preflight is handled, so a
browser page can call the API directly (see the [Web UI](#web-ui)).

| Method | Path | Description |
| --- | --- | --- |
| GET | `/` | HTML endpoint index |
| GET | `/animation` | Current state, e.g. `{"mode":"animation","animationId":0}` |
| POST | `/animation` | Select animation by id (see below); empty body resumes the current one |
| GET | `/brightness` | Current brightness, e.g. `{"brightness":200}` |
| POST | `/brightness` | Set global brightness (integer 0–255) |
| GET | `/matrix` | Current frame as a base64 blob |
| POST | `/matrix` | Upload a frame (base64, optional `pixels=` prefix); switches to manual mode |

Examples:

```bash
curl http://ledfal.local/animation
curl -X POST -d '64' http://ledfal.local/brightness
curl -X POST -d '1'  http://ledfal.local/animation   # play Bad Apple
curl -X POST -d ''   http://ledfal.local/animation   # resume the selected animation
```

### Animations

| id | Name | Description |
| --- | --- | --- |
| 0 | Rainbow | Diagonal hue scroll |
| 1 | Bad Apple | 1-bit video loop |
| 2 | Fire | Heat-diffusion flames rising from the bottom |
| 3 | Rain | Falling drops with fading trails |
| 4 | Stars | Twinkling colored sparks |
| 5 | Plasma | Drifting smooth color noise |
| 6 | Fire2 | Fire with the classic heat ramp (hotter, whiter tips) |
| 7 | RainFill | Rain that fills a water level, then flushes and repeats |
| 8 | Tetris | Auto-playing Tetris demo (10×20 well) |

Selection is delta-time driven and frame-rate independent; per-animation
tunables (speeds, densities) live in `include/config.hpp`.

### Frame format

`GET`/`POST /matrix` use a column-major RGB blob: for each column, then each
row, three bytes `R, G, B`, for a total of `width * height * 3` bytes,
standard-base64 encoded with padding and no line breaks. See
`helpers/BASE64_FORMAT.md`.

The blob is always **R, G, B**; the strip's physical wire order is handled in
the firmware via `wireOrderRGB` (`include/config.hpp`).

## Web UI

A local React app (in `web/`) controls the wall over HTTP. It talks to the
device directly (`http://ledfal.local` by default, editable in the header and
remembered) using the firmware's CORS header — no proxy required.

- **Brightness** slider (0–255, debounced)
- **Animation picker** (ids 0–8) plus "resume"
- **Live preview** of the current frame with a selectable refresh interval
  (`Off` / `0.5 s` / `1 s` / `2 s` / `5 s`)
- **Frame editor**: draw on a 21×75 canvas and upload; load the current frame

```bash
cd web
npm install
npm run dev        # open http://localhost:5173
```

Requires Node.js 18+. Point the device field at `ledfal.local` or the board's
IP. `npm run build` produces static files in `web/dist`.

## Helper scripts

- `helpers/bad_apple/make_badapple.py` — video → 1-bit frame dump + metadata
  header (requires `ffmpeg`)
- `helpers/img_to_base64/img_to_base64.py` — image → base64 frame blob
  (requires Pillow)
- `helpers/show_image.py` — decode a base64 blob and display/export it
- `helpers/perlin_noise.py` — generate colored Perlin-noise frames
- `helpers/color_test.py` — send a red/green/blue band frame to verify wiring
- `helpers/BASE64_FORMAT.md` — the blob format specification

Note: the source video (`badapple.mov`) is not tracked, so pass an input video
explicitly to `make_badapple.py`.

## How it works

At each 60 fps tick the main loop steps the active animation into a
column-major pixel matrix, transposes it into color bit-planes (applying
brightness, orientation, and the configured wire color order), then streams the
planes to every bus with one DMA transfer per bus, paced by its PIO state
machine. The lwIP/cyw43 callbacks run concurrently in IRQ context, so all
shared state is accessed under a recursive lock (`LwipGuard`).

## Special Thanks

- Kiss Konrád

## License

MIT — see [LICENSE](LICENSE).
