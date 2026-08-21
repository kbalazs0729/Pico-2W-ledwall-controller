#pragma once

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "pico/cyw43_arch.h"
#include "ws2812.pio.h"
#include "led_data.hpp"

// ---- Compile-time configuration ----------------------------------------------
// These are the only numbers you should need to touch when the panel changes.

// Matrix geometry.
// matrixRows: number of LEDs in one daisy-chained column strip. This is also
//             the length of one DMA frame (one 32-bit GRB word per LED).
// matrixCols: number of column strips, each with its own data line.
//             (The old test panel is a single strip -> matrixCols = 1.)
constexpr uint8_t matrixRows = 165;
constexpr uint8_t matrixCols = 1;

// GPIO wired to the data-in of the first LED of the driven column strip.
constexpr uint ledDataPin = 28;

// Column strip driven while testing single-column output.
constexpr uint8_t activeColumn = 0;

// Low time after each frame so every LED in the chain latches the new data.
// Datasheet: >= 50 us for classic WS2812, >= 280 us for modern clones.
// 400 us also covers the final word still shifting out of the OSR (~30 us).
constexpr uint32_t latchTimeUs = 400;

// Frame pacing for the main loop (60 fps). One frame flush takes
// matrixRows * 24 * 1.25 us + latchTimeUs (~5.4 ms at 165 LEDs), so 16.6 ms
// leaves plenty of CPU slack for Wi-Fi and animations.
constexpr uint32_t frameIntervalUs = 1'000'000 / 60;

// ---- Public interface ----------------------------------------------------------

// Sets up stdio, Wi-Fi, PIO state machine and DMA channel.
int init_hardware();

// Snapshots one column of pixels into the internal frame buffer, converting
// each pixel to the wire format (24-bit GRB, MSB first, left-justified in a
// 32-bit word). Fast; safe to call while holding the lwIP lock.
void led_load_column(const Pixel* pixels);

// Streams the frame buffer to the LED strip via DMA + PIO and blocks until
// the frame (including the latch gap) is complete. Takes ~5 ms; call WITHOUT
// holding the lwIP lock.
void led_flush_frame();
