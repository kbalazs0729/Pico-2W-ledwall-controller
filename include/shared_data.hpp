#pragma once

#include "animation.hpp"
#include "config.hpp"
#include "led_data.hpp"

// What currently owns the matrix: the procedural animation, or a frame
// posted via POST /matrix.
enum class DisplayMode : uint8_t {
    Animation,
    Manual
};

// State shared between the main loop and the lwIP IRQ-context HTTP handlers.
// ALL access must hold LwipGuard.
struct SharedData {
    LedData<matrixRows, matrixCols> matrix {};
    bool ledState = false;
    DisplayMode mode = DisplayMode::Animation;
    AnimationType animationType = AnimationType::Rainbow;
    uint8_t brightness = defaultBrightness;
};

// The base64 GET/POST endpoints serialize the matrix with reinterpret_cast +
// sizeof; guard the packed-layout assumption that makes that well-defined.
static_assert(sizeof(LedData<matrixRows, matrixCols>) == matrixRows * matrixCols * 3,
              "LedData must stay packed: rows * cols * 3 bytes");
