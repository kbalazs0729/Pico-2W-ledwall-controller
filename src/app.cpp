#include "app.hpp"
#include "wifi.hpp"

#include <stdio.h>

// ---- LED driving state (file-local) --------------------------------------------

static PIO s_pio = pio0;
static uint s_sm[busCount];
static int s_dmaChan[busCount];

// Per-bus frame buffers: one 32-bit bit-plane word per LED per color bit.
// Bit c of a plane word is the data bit for the lane on pin (pinBase + c).
// Kept separate from the matrix so a frame is a consistent snapshot even if
// an HTTP request updates the matrix mid-frame.
static uint32_t s_frameBuf[busCount][matrixRows * 24];

int init_hardware() {
    stdio_init_all();

    if (cyw43_arch_init()) {
        return -1;
    }
    cyw43_arch_enable_sta_mode();

    // Non-blocking: the device boots and serves regardless of Wi-Fi state.
    // wifi.cpp connects and reconnects in the background (call wifi_service()
    // from the main loop).
    wifi_start(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK);

    int offset = pio_add_program(s_pio, &ws2812_bus_program);
    if (offset < 0) {
        printf("Failed to load PIO program (no instruction memory)\n");
        return -1;
    }

    // The PIO program spends 10 cycles per bit (out + 3 movs with [2] delays).
    float div = clock_get_hz(clk_sys) / (wsBitRate * 10.0f);

    uint32_t smMask = 0;
    for (std::size_t b = 0; b < busCount; ++b) {
        const BusConfig& bus = buses[b];
        s_dmaChan[b] = dma_claim_unused_channel(true);
        s_sm[b] = pio_claim_unused_sm(s_pio, true);

        pio_sm_config c = ws2812_bus_program_get_default_config(offset);

        for (uint i = 0; i < bus.lanes; ++i) {
            pio_gpio_init(s_pio, bus.pinBase + i);
        }
        pio_sm_set_consecutive_pindirs(s_pio, s_sm[b], bus.pinBase, bus.lanes, true);

        // MOV PINS asserts exactly this many pins from the base pin.
        sm_config_set_out_pins(&c, bus.pinBase, bus.lanes);
        // Shift right (lane bit c -> pin base+c), autopull one word per plane.
        sm_config_set_out_shift(&c, true, true, 32);
        sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
        sm_config_set_clkdiv(&c, div);

        pio_sm_init(s_pio, s_sm[b], offset, &c);
        smMask |= 1u << s_sm[b];
    }

    // Start all buses on the same clock edge so frames stay aligned.
    pio_enable_sm_mask_in_sync(s_pio, smMask);

    return 0;
}

void led_load_frame(const Pixel* pixelsColMajor, uint8_t brightness) {
    for (uint pos = 0; pos < matrixRows; ++pos) {
        // `pos` is the position along the strip: 0 is the first LED, i.e. the
        // data-in end. The logical matrix and the base64 API use row 0 = top,
        // so map the position to a source row here (config.hpp orientation).
        uint row = flipVertical ? (matrixRows - 1 - pos) : pos;

        for (uint bit = 0; bit < 24; ++bit) {
            // Gather one bit-plane across all columns. MSB first, three 8-bit
            // groups per LED; their order is configurable (config.hpp):
            // RGB strips (bit 0..7 = red) or GRB strips (0..7 = green).
            uint32_t plane = 0;
            for (uint col = 0; col < matrixCols; ++col) {
                uint lane = flipHorizontal ? (matrixCols - 1 - col) : col;
                const Pixel& p = pixelsColMajor[col * matrixRows + row];
                const uint8_t first = wireOrderRGB ? p.r : p.g;
                const uint8_t second = wireOrderRGB ? p.g : p.r;
                uint8_t channel = bit < 8 ? first : (bit < 16 ? second : p.b);
                if (brightness != 255) {
                    // Global linear scale; full scale maps 255 -> 255.
                    channel = static_cast<uint8_t>((channel * brightness) / 255);
                }
                plane |= ((channel >> (7 - (bit & 7))) & 1u) << lane;
            }
            // Distribute the plane over the buses, in bus order.
            uint8_t planeOffset = 0;
            for (std::size_t b = 0; b < busCount; ++b) {
                uint8_t lanes = buses[b].lanes;
                uint32_t mask = lanes >= 32 ? 0xFFFFFFFFu : ((1u << lanes) - 1u);
                s_frameBuf[b][pos * 24 + bit] = (plane >> planeOffset) & mask;
                planeOffset += lanes;
            }
        }
    }
}

void led_flush_frame() {
    // Kick off one DMA transfer per bus, each paced by its state machine's
    // "TX FIFO has space" request line (DREQ) — no CPU work per word.
    for (std::size_t b = 0; b < busCount; ++b) {
        dma_channel_config c = dma_channel_get_default_config(s_dmaChan[b]);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, true);   // walk through the buffer
        channel_config_set_write_increment(&c, false); // always the same FIFO
        channel_config_set_dreq(&c, pio_get_dreq(s_pio, s_sm[b], true));

        dma_channel_configure(s_dmaChan[b], &c,
            &s_pio->txf[s_sm[b]], // dst: state machine TX FIFO
            s_frameBuf[b],        // src: bit-plane words
            matrixRows * 24,
            true);                // start immediately
    }

    // Wait for every bus: DMA done, then TX FIFO drained...
    for (std::size_t b = 0; b < busCount; ++b) {
        dma_channel_wait_for_finish_blocking(s_dmaChan[b]);
        while (!pio_sm_is_tx_fifo_empty(s_pio, s_sm[b])) {
            tight_loop_contents();
        }
    }
    // ...then hold the lines low so all strips latch the frame together.
    sleep_us(latchTimeUs);
}
