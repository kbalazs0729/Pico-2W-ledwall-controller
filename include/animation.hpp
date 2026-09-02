#pragma once

#include "config.hpp"
#include "led_data.hpp"

#include <memory>

// Animation types, resolved to driver objects by make_animation().
// To add an animation: enum entry here + a driver class + factory case in
// animation.cpp.
enum class AnimationType : uint8_t {
    Rainbow,
};

// An Animation owns its own state and advances one frame per step() call.
// step() runs on the main loop under the lwIP lock — keep it us-scale.
class Animation {
public:
    virtual ~Animation() = default;
    virtual void step(float dt, LedData<matrixRows, matrixCols>& matrix) = 0;
};

std::unique_ptr<Animation> make_animation(AnimationType type);
