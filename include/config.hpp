#pragma once

// ============================================================================
// Single place for ALL configuration. If you reach for a number, it lives
// here. secrets.hpp is gitignored and provides WIFI_SSID / WIFI_PASSWORD
// (copy secrets.hpp.example, fill in your own).
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <iterator>

#include "secrets.hpp"

// ---- Network -------------------------------------------------------------------

// Hostname advertised via mDNS: http://<hostname>.local
constexpr char hostname[] = "ledfal";
constexpr int httpPort = 80;

// ---- Matrix geometry -------------------------------------------------------------

// LEDs per daisy-chained column strip. Also the DMA frame length per bus
// (one 32-bit bit-plane word per LED per color bit).
constexpr uint8_t matrixRows = 60;

// ---- Buses -----------------------------------------------------------------------

// A "bus" is one consecutive run of GPIOs driven by a single PIO state
// machine; each lane carries one column strip. Column c maps to bit c of
// the global bit-plane, distributed over the buses in order:
// bus 0 takes columns 0..lanes0-1, bus 1 the next lanes1 columns, etc.
//
// Dev rig:   1 bus  — GP0..3, 4 lanes.
// Final wall: 2 buses — GP0..13 (14 lanes) + GP16..22 (7 lanes),
//             buttons sit on GP14/GP15 between them (not implemented yet).
struct BusConfig {
    uint8_t pinBase; // first GPIO of the consecutive run
    uint8_t lanes;   // number of column strips on this bus (1..32)
};

constexpr BusConfig buses[] = {
    {0, 4},
    // Final wall:
    // {0, 14},
    // {16, 7},
};
constexpr std::size_t busCount = std::size(buses);

// Buttons (final wall): wired between the two buses. Reserved, not yet used.
constexpr uint8_t buttonPinA = 14;
constexpr uint8_t buttonPinB = 15;

// Total number of column strips = matrix width.
constexpr uint8_t matrixCols = [] {
    uint8_t n = 0;
    for (const auto& b : buses) n += b.lanes;
    return n;
}();

// Compile-time sanity checks on the bus layout.
constexpr bool busesValid = [] {
    for (const auto& b : buses) {
        if (b.lanes < 1 || b.lanes > 32) return false;
        if (b.pinBase + b.lanes > 23) return false; // GP23+ belongs to the WiFi chip
        if (b.pinBase <= buttonPinA && buttonPinA < b.pinBase + b.lanes) return false;
        if (b.pinBase <= buttonPinB && buttonPinB < b.pinBase + b.lanes) return false;
        for (const auto& other : buses) {
            if (&other == &b) continue;
            bool overlap = b.pinBase < other.pinBase + other.lanes &&
                           other.pinBase < b.pinBase + b.lanes;
            if (overlap) return false;
        }
    }
    return true;
}();
static_assert(busesValid, "Bus layout invalid: check lanes 1..32, pins < 23, no overlap, buttons not inside a bus");

// ---- WS2812 timing ----------------------------------------------------------------

// Protocol bit rate. The PIO spends 10 SM cycles per bit (see ws2812_bus.pio),
// so the SM runs at 10 * wsBitRate.
constexpr float wsBitRate = 800000.0f;

// Low time after each frame so every LED latches the new data.
// Datasheet: >= 50 us for classic WS2812, >= 280 us for modern clones.
// 400 us also covers the final word still shifting out of the OSR (~30 us).
constexpr uint32_t latchTimeUs = 400;

// Frame pacing for the main loop (60 fps). One frame flush takes
// matrixRows * 24 * 1.25 us + latchTimeUs (e.g. ~2.2 ms at 60 LEDs),
// so 16.6 ms leaves plenty of CPU slack for Wi-Fi and animations.
constexpr uint32_t frameIntervalUs = 1'000'000 / 60;

// ---- Animation defaults -------------------------------------------------------------

// Rainbow scroll: full hue cycles scrolled per second, and a global
// brightness scale (255 ≈ 100%, 128 ≈ 50%).
constexpr float rainbowCyclesPerSec = 2.0f;
constexpr uint8_t brightness = 255;
