#pragma once

#include <array>
#include <cstdint>

struct __attribute__((packed)) Pixel {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

template<uint8_t rows>
struct Column {
    std::array<Pixel, rows> pixels {};
};

/**
 * The LedData struct represents a 2D array of pixels for an LED matrix,
 * stored column-major: columns[col].pixels[row].
 *
 * The layout is packed (Pixel is 3 bytes, no padding), so the whole matrix
 * serializes to exactly rows * cols * 3 bytes in the order documented in
 * helpers/BASE64_FORMAT.md. This invariant is checked with a static_assert
 * at the SharedData declaration in main.cpp.
 *
 * Conversion to the WS2812 wire format (GRB bit-planes) happens in
 * app.cpp at frame load time, not here.
 */
template<uint8_t rows, uint8_t cols>
struct LedData {
public:
    LedData() = default;
    ~LedData() = default;

    std::array<Column<rows>, cols> columns {};
};
