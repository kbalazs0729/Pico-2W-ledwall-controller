#pragma once

#include <array>
#include <cstdint>
#include <cstdio>

struct __attribute__((packed)) Pixel {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

template<uint8_t rows>
struct Column {
    std::array<Pixel, rows> pixels {0};
};

/**
 * The LedData struct represents a 2D array of pixels for an LED matrix.
 * It's main purpose is to store the pixel data for each pixel in a matrix AND
 * to allow accessing a "rowData" view of the matrix which is a uint32_t of the bits
 * representing the state of each pixel in a row. 
 *
 * This can be used to drive SPI based LED drivers like the WS2812 or APA102.
 */
template<uint8_t rows, uint8_t cols>
struct LedData {
public:
    LedData() = default;
    ~LedData() = default;

    /**
    * Gets the row of bits that represent the state of a specific row in the LED matrix.
    * 
    * @param row The row index for which pixels are to be retrieved.
    * @param bit The row index for the bits to be retrieved from the pixel data.
    * @return uint32_t A 32-bit integer where each bit represents the state of a pixel in the specified row.
    */
    uint32_t getRowData(uint8_t row, uint8_t bit) const {
        uint32_t rowData = 0;
        
        if (row >= rows) {
            printf("Warn: Row index %d is out of bounds for LedData with %d rows.\n", row, rows);
            return rowData;
        }

        if (bit >= 24) {
            printf("Warn: Bit index %d is out of bounds for LedData. Valid range is 0-23.\n", bit);
            return rowData;
        }

        for (uint8_t col = 0; col < cols; ++col) {
            // Access the pixel at the specified row and column
            const Pixel* pixel = &columns[col].pixels[row];
        
            // Determine which color channel to use based on the bit index
            const uint8_t* color = nullptr;
            switch (bit / 8) {
                case 0: color = &pixel->r; break;
                case 1: color = &pixel->g; break;
                case 2: color = &pixel->b; break;
                default: break;
            }

            // Extract the specific bit from the color channel and set it in the rowData
            if (color) {
                uint8_t colorBit = (*color >> (7 - (bit % 8))) & 0x01;
                rowData |= (colorBit << col);
            }
        }

        return rowData;
    }
    
    std::array<Column<rows>, cols> columns {0};
};