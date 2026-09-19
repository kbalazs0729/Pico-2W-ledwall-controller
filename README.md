# Pico 2 W LED Wall Controller

Firmware for a **75 × 21 (1575-pixel) WS2815 LED wall** driven by a
**Raspberry Pi Pico 2 W (RP2350)**. It renders procedural and pre-baked
animations and exposes a small HTTP API, with mDNS discovery, for control.

## Features

- 21 column strips driven **in parallel** over PIO + DMA (~2.25 ms per frame
  plus a 400 µs latch gap)
- A single universal PIO program drives any bus from **1 to 32 lanes**
  (WS2812/WS2815, 800 kbit/s)
- Animations: a scrolling **Rainbow** and a 1-bit **Bad Apple** video
  (5258 frames @ 24 fps) embedded in flash via `.incbin`
- Hand-rolled HTTP server on raw lwIP TCP, discoverable at
  `http://ledfal.local`
- **Non-blocking Wi-Fi** with automatic reconnect and exponential backoff; the
  on-board LED blinks until the link is up
- Runtime global **brightness** (0–255), applied centrally to animations and
  uploaded frames
- Vertical/horizontal **flip** options so the firmware can match the physical
  wiring direction
- Raw frame upload as a base64 blob, with a small Python toolchain

## Hardware

- Raspberry Pi Pico 2 W (RP2350, 4 MB flash)
- 21 × WS2815 strips, 75 LEDs each (12 V) — 1575 LEDs total
- 3 × 74AHCT245 octal level shifters (3.3 V → 5 V data)
- 330 Ω series resistor per data lane, common ground, 12 V injected at both
  ends of each strip

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
helpers/    Python tools: Bad Apple generator, image/base64 tools, noise generator, format docs
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

- `include/config.hpp` — matrix geometry, bus/pin layout, orientation,
  default brightness, and timing. Start here.
- Wi-Fi credentials: copy `include/secrets.hpp.example` to
  `include/secrets.hpp` and fill in `WIFI_SSID` / `WIFI_PASSWORD`. The file is
  gitignored.

## HTTP API

The device serves on port 80 as `http://ledfal.local`.

| Method | Path | Description |
| --- | --- | --- |
| GET | `/` | HTML endpoint index |
| GET | `/led` | Toggle the internal LED state flag |
| GET | `/animation` | Current state, e.g. `{"mode":"animation","animationId":0}` |
| POST | `/animation` | Select animation by id (0 = Rainbow, 1 = Bad Apple); empty body resumes the current one |
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

### Frame format

`GET`/`POST /matrix` use a column-major RGB blob: for each column, then each
row, three bytes `R, G, B`, for a total of `width * height * 3` bytes,
standard-base64 encoded with padding and no line breaks. See
`helpers/BASE64_FORMAT.md`.

## Helper scripts

- `helpers/bad_apple/make_badapple.py` — video → 1-bit frame dump + metadata
  header (requires `ffmpeg`)
- `helpers/img_to_base64/img_to_base64.py` — image → base64 frame blob
  (requires Pillow)
- `helpers/show_image.py` — decode a base64 blob and display/export it
- `helpers/perlin_noise.py` — generate colored Perlin-noise frames
- `helpers/BASE64_FORMAT.md` — the blob format specification

Note: the source video (`badapple.mov`) is not tracked, so pass an input video
explicitly to `make_badapple.py`.

## How it works

At each 60 fps tick the main loop steps the active animation into a
column-major pixel matrix, transposes it into GRB bit-planes (applying
brightness and orientation), then streams the planes to every bus with one DMA
transfer per bus, paced by its PIO state machine. The lwIP/cyw43 callbacks run
concurrently in IRQ context, so all shared state is accessed under a recursive
lock (`LwipGuard`).

## Special Thanks

- _(add name here)_

## License

MIT — see [LICENSE](LICENSE).
