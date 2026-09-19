#include "animation.hpp"

#include "generated/badapple_video.hpp"

#include "pico/time.h"

#include <cmath>

namespace {

// Tiny xorshift32 PRNG. rand() would lock and is overkill; this keeps step()
// cheap and self-contained. Each animation instance gets its own seed so their
// sequences don't correlate.
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}

    uint32_t next() {
        uint32_t x = s;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return s = x;
    }

    uint8_t byte() { return static_cast<uint8_t>(next() >> 24); }
    uint32_t below(uint32_t n) { return n ? next() % n : 0; }
};

// Seed for a new animation instance: mix the boot time with a counter so
// successive instances (and reboots) look different.
[[maybe_unused]] uint32_t nextSeed() {
    static uint32_t counter = 0;
    return static_cast<uint32_t>(time_us_64()) ^ (++counter * 2654435761u);
}

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
    // Full brightness; the global brightness scale is applied centrally in
    // led_load_frame() so it also covers posted /matrix frames.
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
                uint8_t hue = static_cast<uint8_t>(index * 256.0f / (matrixCols * matrixRows) + m_huePhase);
                matrix.columns[col].pixels[row] = wheel(hue);
            }
        }
    }

private:
    float m_huePhase = 0.0f; // accumulated hue offset in 1/256 hue units
};

// Plays the 1-bit video baked into flash (include/generated/badapple_video.hpp)
// in a loop. Frames advance at the video's own fps (accumulated delta time),
// independent of the display frame rate. Bit packing: pixel i = col*rows+row,
// byte i/8, bit 7-(i%8) — mirrors make_badapple.py.
class BadApple final : public Animation {
public:
    void step(float dt, LedData<matrixRows, matrixCols>& matrix) override {
        static_assert(badapple::width == matrixCols && badapple::height == matrixRows,
                      "badapple_video.hpp was generated for a different matrix size; "
                      "re-run make_badapple.py with the current --width/--height");

        m_accumSec += dt;
        constexpr float frameSec = 1.0f / badapple::fps;
        while (m_accumSec >= frameSec) {
            m_accumSec -= frameSec;
            m_frame = (m_frame + 1) % badapple::frameCount;
        }

        const uint8_t* frame = badapple_video + m_frame * badapple::frameBytes;
        // Full white; led_load_frame() applies the global brightness scale.
        constexpr uint8_t white = 255;
        for (uint32_t col = 0; col < matrixCols; ++col) {
            for (uint32_t row = 0; row < matrixRows; ++row) {
                uint32_t i = col * matrixRows + row;
                bool on = frame[i / 8] & (1u << (7 - (i % 8)));
                matrix.columns[col].pixels[row] = on ? Pixel{white, white, white}
                                                     : Pixel{0, 0, 0};
            }
        }
    }

private:
    float m_accumSec = 0.0f;    // time accumulated towards the next video frame
    uint32_t m_frame = 0;
};

} // namespace

std::unique_ptr<Animation> make_animation(AnimationType type) {
    switch (type) {
        case AnimationType::BadApple:
            return std::make_unique<BadApple>();
        case AnimationType::Rainbow:
        case AnimationType::Count:
        default:
            return std::make_unique<Rainbow>();
    }
}
