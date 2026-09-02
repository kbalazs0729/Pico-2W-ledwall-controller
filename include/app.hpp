#pragma once

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "pico/cyw43_arch.h"
#include "ws2812_bus.pio.h"

#include "config.hpp"
#include "led_data.hpp"

// ---- Public interface ----------------------------------------------------------

// Sets up stdio, Wi-Fi, and one PIO state machine + DMA channel per bus.
int init_hardware();

// Transposes the whole matrix (column-major packed pixels) into per-bus
// GRB bit-plane words in the internal frame buffers. Fast (us-scale);
// safe to call while holding the lwIP lock.
void led_load_frame(const Pixel* pixelsColMajor);

// Streams all frame buffers to their buses via DMA + PIO and blocks until
// every bus is done, then holds the lines low for latchTimeUs so the strips
// latch. Takes matrixRows * 24 * 1.25 us + latchTimeUs; call WITHOUT
// holding the lwIP lock.
void led_flush_frame();
