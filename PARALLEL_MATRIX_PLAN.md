# Parallel WS2815 matrix — implementation plan (for later)

Target: the real wall, **75 rows × 21 columns = 1575 WS2815 LEDs**, driven as
**21 parallel column strips** from a **Pico 2 W (RP2350)**.

The current single-lane code (one strip on GP28) stays as-is for testing.
This document describes how to grow it into the parallel version when the
hardware is ready.

---

## 1. Hardware recap

### LED choice: WS2815

- 12 V power, **5 V logic data**, protocol-compatible with the WS2812
  800 kbit/s family (verify T0H/T1H against the WS2815 datasheet; adjust
  `T1/T2/T3` in the PIO program if needed — see §3).
- Dual data inputs per pixel: **DIN** and **BIN** (breakpoint-continue).
  For strips wired as one continuous chain, tie the **first LED's BIN to GND**.
- 12 V rail: ~4.5 A per 75-LED strip at full white (usually run dimmer).
  Inject 12 V at **both ends** of every strip. Still mind the current.

### Level shifting: 3× octal bus transceiver (74AHCT245 / 74HCT245)

- Pico GPIO is 3.3 V; WS2815 data wants 5 V logic. The bus ICs are the
  correct fix: 3 chips × 8 channels = 24 lanes, 21 used, 3 spare.
- All transceivers: DIR pin tied for Pico→LED direction, OE tied active.

### Wiring constraints (this is the important part)

Parallel output uses the PIO instructions `out pins, N` / `mov pins`, which
can **only write to N consecutive GPIO pins starting at a base pin**.
There is no arbitrary pin remapping — this is a hard hardware constraint.

→ Use **GP0 … GP20**, one per column:

```
GP0 → col 0, GP1 → col 1, … GP20 → col 20
```

- Avoid GP23/GP24/GP25/GP29 (used by the CYW43 WiFi chip on Pico W / Pico 2 W)
  and GP26–28 (ADC, and GP28 is currently used by the single-lane test build).
- 330 Ω series resistor per data lane, close to the first LED.
- **Common ground** between Pico, bus ICs, and the 12 V supply.
  21 data lanes with floating ground = random corruption.
- Tie the unused 8th input of each bus IC to GND (floating CMOS inputs can
  oscillate). GP14/GP15 could be routed through the spare channels as spare
  lanes for future expansion.

### Alternative pin grouping: 2 runs × 2 SMs (chosen for PCB layout)

The 21 lanes are split into two consecutive runs, one per bus-IC group:

| Run | Pins | Lanes | SM program |
|-----|------|-------|------------|
| A | GP0 – GP13 | 14 | `out x, 14` |
| B | GP16 – GP22 | 7 | `out x, 7` |

(GP14/GP15 are skipped; CYW43/ADC pins remain untouched.)

Consequences vs. a single 21-pin run:

- **2 state machines + 2 DMA channels** (RP2350 has 12 SMs — plenty).
  The `out` bit count is baked into the instruction, so the program is
  listed twice, identical except for `out x, 14` / `out x, 7`.
- The transpose splits each 21-bit plane word into two buffers:
  `planeA = plane & 0x3FFF`, `planeB = (plane >> 14) & 0x7F`.
- **Lanes do not need to be synchronized** — each strip is self-clocked and
  latches on its own gap. Still start both SMs together with
  `pio_enable_sm_mask_in_sync` to keep frames aligned.
- Frame end: wait for **both** DMA channels and **both** TX FIFOs to drain,
  then one shared 400 µs latch gap.
- Frame time is unchanged: 75 × 24 × 1.25 µs ≈ 2.25 ms.

### Pico 2 W (RP2350) port notes

- `PICO_BOARD pico2_w` in `CMakeLists.txt`. Re-check `lwipopts.h` still applies.
- RP2350 has **3 PIO blocks × 4 SMs** (RP2040 has 2×4) — plenty either way;
  we still only need one SM for all 21 lanes.
- Default `clk_sys` is 150 MHz (RP2040: 125 MHz). **No code change needed**:
  the clock divider is computed at runtime from `clock_get_hz(clk_sys)`.
- The PIO is the same architecture; `pioasm` and all programs work unchanged.

---

## 2. Why parallel at all

Serial-chaining 1575 LEDs would take 1575 × 24 × 1.25 µs ≈ **47 ms per frame**
(~20 fps ceiling). Driving 21 lanes in parallel keeps the frame time at the
single-strip value: 75 × 24 × 1.25 µs ≈ **2.25 ms** (~375 fps ceiling) —
all 21 columns receive each bit **simultaneously**.

| Quantity | Value |
|---|---|
| Bits per LED | 24 (GRB, MSB first) |
| Bit period | 1.25 µs (800 kbit/s) |
| Frame time (parallel) | 75 × 24 × 1.25 µs ≈ 2.25 ms |
| Latch gap | ≥ 280 µs (we use 400 µs) |
| Max frame rate | ~375 fps |
| Realistic animation target | 30–60 fps |

---

## 3. The parallel PIO program

Side-set can only drive a *constant* to its pins, so it cannot carry shifted
data to 21 different pins. Instead, data goes through the OUT pin mapping and
the waveform is shaped with `mov pins`:

```pio
.program ws2812_parallel

.define public T1 2
.define public T2 5
.define public T3 3

.wrap_target
    out x, 21                  ; grab one bit per column (one "bit-plane")
    mov pins, !null [T1 - 1]   ; ALL 21 pins high for T1 cycles
    mov pins, x     [T2 - 1]   ; bit=1 stays high, bit=0 drops low
    mov pins, null  [T3 - 1]   ; ALL 21 pins low for T3 cycles
.wrap
```

Timing per bit (10 cycles at 8 MHz SM clock = 1.25 µs, same as single-lane):

| bit | high | low |
|-----|------|-----|
| `0` | T1 (250 ns) | T2+T3 (1000 ns) |
| `1` | T1+T2 (875 ns) | T3 (375 ns) |

Latch behaviour is free: when the TX FIFO runs dry, the SM stalls at `out`,
and the pins hold the low level left by the last `mov pins, null`.

C-side SM config differences vs. the single-lane version:

```c
pio_gpio_init(s_pio, basePin + c);                 // for all 21 pins
pio_sm_set_consecutive_pindirs(s_pio, s_sm, basePin, 21, true);
sm_config_set_out_pins(&c, basePin, 21);           // NEW (was sideset)
// NO sideset config
sm_config_set_out_shift(&c, true, true, 21);       // shift RIGHT, autopull every 21 bits
sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
sm_config_set_clkdiv(&c, clock_get_hz(clk_sys) / (800000.0f * (T1+T2+T3)));
```

With shift-right + `out x, 21`, the **LSB of each word goes to the base pin**:
bit *c* of every FIFO word is the data bit for column *c* on GP(basePin + c).

---

## 4. The bit-plane frame format

The DMA buffer no longer holds pixels; it holds **bit-planes**.
For each LED position `row` along the strips, for each of the 24 color bits
(`bit` = 0..23, GRB order, MSB first), one 32-bit word:

```
frame[row * 24 + bit] = Σ over columns c:  colorBit(pixel[c][row], bit) << c
```

- Autopull threshold 21 ⇒ one word per bit-plane (upper 11 bits unused;
  simpler than tight packing, bandwidth is a non-issue).
- Buffer size: `matrixRows × 24 × 4` = 75 × 24 × 4 = **7200 bytes**.
- DMA transfer count: `matrixRows * 24` words. Everything else in the DMA
  config is identical to the single-lane version.

`LedData::getRowData(row, bit)` already computes almost exactly this word —
it needs one fix: channel order must be **GRB** (`bit/8`: 0→g, 1→r, 2→b),
matching the single-lane converter in `led_load_column`.

Transpose cost: 1800 words × 21 bit-extractions ≈ tens of µs on RP2350 —
irrelevant at 30–60 fps. If it ever matters, a 256-entry LUT per byte
(position) can turn it into table lookups; don't bother up front.

---

## 5. Code changes checklist (from current state)

1. Add `src/ws2812_parallel.pio` (keep `ws2812.pio`), register it with
   `pico_generate_pio_header` in `CMakeLists.txt`.
2. `app.hpp` config block:
   - `matrixRows = 75`, `matrixCols = 21`
   - replace `ledDataPin` with `ledPinBase = 0` (base of the 21-pin run)
   - drop `activeColumn` (all columns are driven every frame now)
3. `app.cpp`:
   - SM init: parallel variant (§3). Optionally select single-lane vs.
     parallel at compile time via `matrixCols > 1`.
   - Replace `led_load_column()` with `led_load_frame()` that transposes the
     **whole matrix** into bit-planes under the lwIP lock.
   - Frame buffer becomes `s_frameBuf[matrixRows * 24]`.
   - `led_flush_frame()`: DMA count `matrixRows * 24`; the wait/latch logic
     is unchanged.
4. `main.cpp`: call `led_load_frame(&sharedData.matrix)` once per frame.
5. `CMakeLists.txt`: `PICO_BOARD pico2_w`.
6. Re-check `FINDINGS.md` notes (chunked TCP send etc.) still hold — the
   matrix blob stays 75×21×3 = 4725 bytes, same as the old panel.

---

## 6. Animation architecture (the actual point of the project)

Animations are **procedural, computed in the main loop**; HTTP endpoints only
*select* the animation and set parameters. Keep the concurrency rules:

### Concurrency rules (already proven in the single-lane build)

- `sharedData` is written by lwIP callbacks (IRQ context) and read by the
  main loop. **All access under `LwipGuard`.**
- Handlers must be tiny: set a mode enum + a few params, return. Never
  compute frames in a handler.
- `led_load_frame()` (fast, µs) runs under the lock;
  `led_flush_frame()` (~2.7 ms) runs **without** the lock.

### Suggested structure

```c++
enum class Animation : uint8_t { Gradient, Fire, Rain, Stars, BadApple /*, …*/ };

struct SharedData {
    LedData<matrixRows, matrixCols> matrix;
    bool ledState;
    Animation animation;
    // small param block per animation (speed, intensity, hue, …)
};
```

Main loop per iteration:

```
1. service LED toggle
2. step the active animation one frame (writes into sharedData.matrix)
3. led_load_frame()   (under lock)
4. led_flush_frame()  (no lock)
5. frame pacing to target fps (e.g. sleep to 33 ms period for 30 fps)
```

### Notes on the planned animations

- **Fire / rain** are naturally *per-column* 1D simulations — a perfect fit
  for this matrix geometry. Fire: heat diffuses upward per column, map
  heat→color. Rain: droplets fall down columns with fading trails.
- **Stars**: sparse random pixels with per-star fade envelopes.
- **Bad Apple (monochrome)**: 1 bit/pixel ⇒ 75×21 bits ≈ **197 bytes/frame**
  raw. Compress (RLE or frame-diff) and either:
  - pre-upload the whole clip into flash/RAM in chunks via `POST /clip`
    (double-buffer the write target), or
  - stream and decode on the fly.
  Expand bit → (white/black) `Pixel` during the animation step, before the
  transpose. At 30 fps raw that's only ~6 KB/s — trivial for the WiFi stack,
  but still keep the chunked-send/reassembly lessons from `FINDINGS.md`
  in mind (never assume one TCP segment = one request).

### Endpoints (extend, don't redesign)

- `GET/POST /animation` — select animation + params (small JSON or form body).
- `POST /clip` — chunked monochrome clip upload (see above).
- Keep `/matrix` POST as the escape hatch (full raw frame override) —
  useful for debugging the bit-plane transpose against `show_image.py`.

---

## 7. Verification recipe (when the wall is wired)

1. Flash with `matrixCols = 21`, `ledPinBase = 0`, gradient fill.
2. **One strip connected to GP0** → same gradient as the single-lane build.
   If colors are wrong → GRB order in the transpose. If garbage → clkdiv
   (check `clock_get_hz` under WiFi) or T1/T2/T3 vs. the WS2815 datasheet.
3. Add strips one GPIO at a time; each should show its column's gradient.
   Misordered columns ⇒ wiring order vs. bit order (LSB = GP0 = column 0).
4. Full white at low brightness → check 12 V injection before cranking up.
