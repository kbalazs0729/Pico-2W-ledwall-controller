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

// Fire: classic heat diffusion. Logical row 0 is the top of the wall, so the
// fire is seeded on the last row and heat propagates upward (toward lower row
// indices). Runs at a fixed tick rate (config.hpp) so its look doesn't depend
// on the display frame rate.
class Fire final : public Animation {
public:
    // RedHeavy keeps red dominant the whole way (black->red->orange->yellow).
    // Classic saturates red first, then green, then blue, giving hotter,
    // whiter tips (black->red->orange->yellow->white).
    enum class Palette : uint8_t { RedHeavy, Classic };

    explicit Fire(Palette palette) : m_palette(palette), m_rng(nextSeed()) {}

    void step(float dt, LedData<matrixRows, matrixCols>& matrix) override {
        constexpr float tickSec = 1.0f / fireTickHz;
        m_accum += dt;
        // Bound the catch-up so a stall can't run a huge loop in one frame.
        constexpr int maxTicks = 4;
        int ticks = 0;
        while (m_accum >= tickSec && ticks < maxTicks) {
            m_accum -= tickSec;
            ++ticks;
            simTick();
        }
        if (m_accum > tickSec) m_accum = 0.0f; // drop the backlog

        for (uint32_t col = 0; col < matrixCols; ++col) {
            for (uint32_t row = 0; row < matrixRows; ++row) {
                matrix.columns[col].pixels[row] = color(m_palette, m_heat[row][col]);
            }
        }
    }

private:
    Palette m_palette;
    uint8_t m_heat[matrixRows][matrixCols] {};
    float m_accum = 0.0f;
    Rng m_rng;

    static Pixel color(Palette palette, uint8_t heat) {
        return palette == Palette::Classic ? classic(heat) : redHeavy(heat);
    }

    // Red stays on top throughout: green only starts once red is well up, and
    // is bounded so it can never exceed red (that inversion looked green).
    static Pixel redHeavy(uint8_t heat) {
        uint32_t r = heat;
        uint32_t g = heat > 128 ? (heat - 128u) * 2u : 0u;
        uint32_t b = heat > 224 ? (heat - 224u) * 4u : 0u;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        return {static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b)};
    }

    // Classic heat ramp: red saturates first, then green, then blue.
    static Pixel classic(uint8_t heat) {
        uint32_t x = heat * 3u;
        uint32_t r = x < 255u ? x : 255u;
        uint32_t g = x > 255u ? (x - 255u > 255u ? 255u : x - 255u) : 0u;
        uint32_t b = x > 510u ? (x - 510u > 255u ? 255u : x - 510u) : 0u;
        return {static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b)};
    }

    void simTick() {
        constexpr uint32_t last = matrixRows - 1;

        // Seed the bottom row with random sparks.
        for (uint32_t col = 0; col < matrixCols; ++col) {
            if (m_rng.byte() < fireSpawnThreshold) {
                m_heat[last][col] = static_cast<uint8_t>(
                    fireSparkMin + m_rng.byte() % (256 - fireSparkMin));
            } else {
                m_heat[last][col] = 0;
            }
        }

        // Propagate upward: average the cell below (weighted) with its
        // neighbours and the row below that, then cool a little.
        for (uint32_t row = 0; row < last; ++row) {
            for (uint32_t col = 0; col < matrixCols; ++col) {
                uint32_t below = m_heat[row + 1][col];
                uint32_t left = col > 0 ? m_heat[row + 1][col - 1] : below;
                uint32_t right = col + 1 < matrixCols ? m_heat[row + 1][col + 1] : below;
                uint32_t below2 = (row + 2 < matrixRows) ? m_heat[row + 2][col] : below;
                uint32_t avg = (below * 2 + left + right + below2) / 6;
                int v = static_cast<int>(avg) - fireCooling;
                m_heat[row][col] = v > 0 ? static_cast<uint8_t>(v) : 0;
            }
        }
    }
};

// A falling raindrop, shared by the Rain and RainFill animations.
struct Drop {
    float y;      // head position; 0 = top row
    float speed;  // rows per second
    uint8_t col;
    uint8_t len;  // trail length behind the head
    Pixel color;
};

// Respawn a drop above the top (scatter=true spreads it anywhere above so a
// fresh field starts populated).
void dropSpawn(Drop& d, Rng& rng, bool scatter) {
    d.col = static_cast<uint8_t>(rng.below(matrixCols));
    d.speed = rainSpeedRowsPerSec * (0.6f + rng.byte() / 255.0f * 0.8f);
    d.len = static_cast<uint8_t>(3 + rng.below(rainTrailLen - 2));
    d.y = scatter ? -static_cast<float>(rng.below(matrixRows))
                  : -static_cast<float>(rng.below(8));
    static const Pixel palette[] = {
        {0, 64, 255}, {0, 160, 255}, {40, 200, 255}, {180, 220, 255}};
    d.color = palette[rng.below(sizeof(palette) / sizeof(palette[0]))];
}

void dropDraw(LedData<matrixRows, matrixCols>& matrix, const Drop& d) {
    for (uint8_t t = 0; t <= d.len; ++t) {
        int row = static_cast<int>(d.y) - t;
        if (row < 0 || row >= static_cast<int>(matrixRows)) continue;
        // Head is full brightness, the trail fades out behind it.
        uint32_t scale = static_cast<uint32_t>(d.len - t) * 255u / (d.len + 1);
        matrix.columns[d.col].pixels[row] = Pixel{
            static_cast<uint8_t>(d.color.r * scale / 255),
            static_cast<uint8_t>(d.color.g * scale / 255),
            static_cast<uint8_t>(d.color.b * scale / 255),
        };
    }
}

// Rain: a fixed pool of drops falling down the columns, each with a fading
// trail. Drops respawn above the top once they fall past the bottom.
class Rain final : public Animation {
public:
    Rain() : m_rng(nextSeed()) {
        // Scatter the first pass so the field starts populated.
        for (auto& d : m_drops) dropSpawn(d, m_rng, true);
    }

    void step(float dt, LedData<matrixRows, matrixCols>& matrix) override {
        // Clear to black; each drop redraws its whole trail every frame.
        for (uint32_t col = 0; col < matrixCols; ++col) {
            for (uint32_t row = 0; row < matrixRows; ++row) {
                matrix.columns[col].pixels[row] = Pixel{0, 0, 0};
            }
        }

        for (auto& d : m_drops) {
            d.y += d.speed * dt;
            if (d.y - static_cast<float>(d.len) > static_cast<float>(matrixRows)) {
                dropSpawn(d, m_rng, false);
            }
            dropDraw(matrix, d);
        }
    }

private:
    Drop m_drops[rainDropCount] {};
    Rng m_rng;
};

// RainFill: rain plus a water level that rises one row every rainFillRowSec
// seconds. Once it reaches rainFillMaxFraction of the wall it flushes (drains
// over rainFillFlushSec) and the cycle repeats.
class RainFill final : public Animation {
public:
    RainFill() : m_rng(nextSeed()) {
        for (auto& d : m_drops) dropSpawn(d, m_rng, true);
    }

    void step(float dt, LedData<matrixRows, matrixCols>& matrix) override {
        constexpr int maxRows = static_cast<int>(rainFillMaxFraction * matrixRows);

        if (m_flushing) {
            m_level -= static_cast<float>(maxRows) / rainFillFlushSec * dt;
            if (m_level <= 0.0f) {
                m_level = 0.0f;
                m_flushing = false;
                m_fillAccum = 0.0f;
            }
        } else {
            m_fillAccum += dt;
            while (m_fillAccum >= rainFillRowSec &&
                   m_level < static_cast<float>(maxRows)) {
                m_fillAccum -= rainFillRowSec;
                m_level += 1.0f;
            }
            if (m_level >= static_cast<float>(maxRows)) {
                m_flushing = true;
                m_fillAccum = 0.0f;
            }
        }

        // Water occupies rows [surface, matrixRows); surface is its top row.
        const int height = static_cast<int>(matrixRows);
        int surface = height - static_cast<int>(m_level);
        if (surface < 0) surface = 0;
        if (surface > height) surface = height;

        // Advance drops; each respawns once its head reaches the water line.
        for (auto& d : m_drops) {
            d.y += d.speed * dt;
            if (d.y > static_cast<float>(surface)) {
                dropSpawn(d, m_rng, false);
            }
        }

        // Background above the water, and the water body below it.
        for (uint32_t col = 0; col < matrixCols; ++col) {
            for (int row = 0; row < height; ++row) {
                if (row < surface) {
                    matrix.columns[col].pixels[row] = Pixel{0, 0, 0};
                } else {
                    // Deeper water fades toward a darker blue.
                    uint32_t depth = static_cast<uint32_t>(row - surface);
                    uint32_t dim = depth * 4u < 255u ? 255u - depth * 4u : 96u;
                    matrix.columns[col].pixels[row] = Pixel{
                        0,
                        static_cast<uint8_t>(40u * dim / 255u),
                        static_cast<uint8_t>(170u * dim / 255u),
                    };
                }
            }
        }

        // Bright, rippling surface line.
        if (surface < height) {
            for (uint32_t col = 0; col < matrixCols; ++col) {
                uint32_t ripple = (col * 53u + m_ripplePhase) & 63u;
                matrix.columns[col].pixels[surface] = Pixel{
                    0,
                    static_cast<uint8_t>(150u + ripple),
                    255,
                };
            }
        }
        m_ripplePhase += 2;

        for (auto& d : m_drops) dropDraw(matrix, d);
    }

private:
    Drop m_drops[rainDropCount] {};
    Rng m_rng;
    float m_level = 0.0f;   // water height in rows
    float m_fillAccum = 0.0f;
    bool m_flushing = false;
    uint32_t m_ripplePhase = 0;
};

// Stars: sparse colored sparks that fade out over a dark field, so it reads as
// a twinkling starfield.
class Stars final : public Animation {
public:
    Stars() : m_rng(nextSeed()) {}

    void step(float dt, LedData<matrixRows, matrixCols>& matrix) override {
        // Fade the whole buffer toward black; k is the per-frame retention.
        float k = 1.0f - starsDecayPerSec * dt;
        if (k < 0.0f) k = 0.0f;
        for (uint32_t col = 0; col < matrixCols; ++col) {
            for (uint32_t row = 0; row < matrixRows; ++row) {
                Pixel& p = m_buf[row][col];
                p.r = static_cast<uint8_t>(p.r * k);
                p.g = static_cast<uint8_t>(p.g * k);
                p.b = static_cast<uint8_t>(p.b * k);
            }
        }

        // Spawn new sparks.
        m_spawn += starsSpawnPerSec * dt;
        for (int i = 0; i < 16 && m_spawn >= 1.0f; ++i) {
            m_spawn -= 1.0f;
            uint32_t col = m_rng.below(matrixCols);
            uint32_t row = m_rng.below(matrixRows);
            m_buf[row][col] = wheel(m_rng.byte());
        }
        if (m_spawn > 16.0f) m_spawn = 16.0f; // drop any backlog

        for (uint32_t col = 0; col < matrixCols; ++col) {
            for (uint32_t row = 0; row < matrixRows; ++row) {
                matrix.columns[col].pixels[row] = m_buf[row][col];
            }
        }
    }

private:
    Pixel m_buf[matrixRows][matrixCols] {};
    float m_spawn = 0.0f;
    Rng m_rng;
};

// Plasma: animated 3D value noise mapped through the color wheel, so smooth
// color clouds drift across the wall. Two octaves add a little detail.
class Plasma final : public Animation {
public:
    void step(float dt, LedData<matrixRows, matrixCols>& matrix) override {
        m_time += plasmaSpeed * dt;
        for (uint32_t col = 0; col < matrixCols; ++col) {
            for (uint32_t row = 0; row < matrixRows; ++row) {
                float x = col * plasmaScale;
                float y = row * plasmaScale;
                float v = noise(x, y, m_time) * 0.7f +
                          noise(x * 2.0f, y * 2.0f, m_time * 1.5f) * 0.3f;
                matrix.columns[col].pixels[row] =
                    wheel(static_cast<uint8_t>(v * 255.0f));
            }
        }
    }

private:
    float m_time = 0.0f;

    static uint32_t hash(int x, int y, int z) {
        uint32_t h = static_cast<uint32_t>(x) * 0x8DA6B343u ^
                     static_cast<uint32_t>(y) * 0xD8163841u ^
                     static_cast<uint32_t>(z) * 0xCB1AB31Fu;
        h ^= h >> 15;
        h *= 0x2C1B3C6Du;
        h ^= h >> 12;
        h *= 0x297A2D39u;
        h ^= h >> 15;
        return h;
    }

    static float corner(int x, int y, int z) {
        return static_cast<float>(hash(x, y, z) >> 24) / 255.0f;
    }

    static float fade(float t) { return t * t * (3.0f - 2.0f * t); }
    static float lerp(float a, float b, float t) { return a + (b - a) * t; }

    static float noise(float x, float y, float z) {
        int xi = static_cast<int>(std::floor(x));
        int yi = static_cast<int>(std::floor(y));
        int zi = static_cast<int>(std::floor(z));
        float xf = fade(x - static_cast<float>(xi));
        float yf = fade(y - static_cast<float>(yi));
        float zf = fade(z - static_cast<float>(zi));

        float x00 = lerp(corner(xi, yi, zi), corner(xi + 1, yi, zi), xf);
        float x10 = lerp(corner(xi, yi + 1, zi), corner(xi + 1, yi + 1, zi), xf);
        float x01 = lerp(corner(xi, yi, zi + 1), corner(xi + 1, yi, zi + 1), xf);
        float x11 = lerp(corner(xi, yi + 1, zi + 1), corner(xi + 1, yi + 1, zi + 1), xf);

        return lerp(lerp(x00, x10, yf), lerp(x01, x11, yf), zf);
    }
};

} // namespace

std::unique_ptr<Animation> make_animation(AnimationType type) {
    switch (type) {
        case AnimationType::BadApple:
            return std::make_unique<BadApple>();
        case AnimationType::Fire:
            return std::make_unique<Fire>(Fire::Palette::RedHeavy);
        case AnimationType::Fire2:
            return std::make_unique<Fire>(Fire::Palette::Classic);
        case AnimationType::Rain:
            return std::make_unique<Rain>();
        case AnimationType::RainFill:
            return std::make_unique<RainFill>();
        case AnimationType::Stars:
            return std::make_unique<Stars>();
        case AnimationType::Plasma:
            return std::make_unique<Plasma>();
        case AnimationType::Rainbow:
        case AnimationType::Count:
        default:
            return std::make_unique<Rainbow>();
    }
}
