#include "animation.hpp"

#include <cmath>

namespace {

// Fast full-saturation HSV->RGB ("color wheel"): pos 0..255 sweeps R->G->B->R.
Pixel wheel(uint8_t pos) {
    pos = 255 - pos;
    Pixel p;
    if (pos < 85) {
        p = {static_cast<uint8_t>(255 - pos * 3), static_cast<uint8_t>(pos * 3), 0};
    } else if (pos < 170) {
        pos -= 85;
        p = {0, static_cast<uint8_t>(255 - pos * 3), static_cast<uint8_t>(pos * 3)};
    } else {
        pos -= 170;
        p = {static_cast<uint8_t>(pos * 3), 0, static_cast<uint8_t>(255 - pos * 3)};
    }
    // Scale to the target brightness (keeps the hue; >>8 means 255 maps to
    // 254, a standard fast approximation of full scale).
    p.r = (p.r * brightness) >> 8;
    p.g = (p.g * brightness) >> 8;
    p.b = (p.b * brightness) >> 8;
    return p;
}

// One full hue cycle spans the whole matrix diagonally (columns included),
// scrolling at rainbowCyclesPerSec (config.hpp). The phase is delta-time
// driven, so the scroll speed is frame-rate independent.
class Rainbow final : public Animation {
public:
    void step(float dt, LedData<matrixRows, matrixCols>& matrix) override {
        m_huePhase += rainbowCyclesPerSec * dt * 256.0f;
        // fmodf (not a single subtract): a long stall must not leave the
        // phase denormalized for several frames.
        m_huePhase = std::fmodf(m_huePhase, 256.0f);

        for (uint32_t col = 0; col < matrixCols; ++col) {
            for (uint32_t row = 0; row < matrixRows; ++row) {
                uint32_t index = col * matrixRows + row;
                uint8_t hue = static_cast<uint8_t>(index * 256 / (matrixCols * matrixRows) + m_huePhase);
                matrix.columns[col].pixels[row] = wheel(hue);
            }
        }
    }

private:
    float m_huePhase = 0.0f; // accumulated hue offset in 1/256 hue units
};

} // namespace

std::unique_ptr<Animation> make_animation(AnimationType type) {
    switch (type) {
        case AnimationType::Rainbow:
        default:
            return std::make_unique<Rainbow>();
    }
}
