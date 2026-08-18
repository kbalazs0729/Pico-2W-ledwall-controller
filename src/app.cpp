#include "app.hpp"

#include <stdio.h>

#define WIFI_SSID "REDACTED-SSID"
#define WIFI_PASSWORD "REDACTED-PASSWORD"

// ---- LED driving state (file-local) --------------------------------------------

static PIO s_pio = pio0;
static uint s_sm;
static int s_dmaChan;

// One column of pixels in on-the-wire format. Kept separate from the matrix so
// a frame is a consistent snapshot even if an HTTP request updates the matrix
// mid-frame.
static uint32_t s_frameBuf[matrixRows];

int init_hardware() {
    stdio_init_all();

    if (cyw43_arch_init()) {
        return -1;
    }
    cyw43_arch_enable_sta_mode();

    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 10000)) {
        printf("Wi-Fi Connection Failed\n");
        return -1;
    }

    s_dmaChan = dma_claim_unused_channel(true);

    uint offset = pio_add_program(s_pio, &ws2812_program);
    s_sm = pio_claim_unused_sm(s_pio, true);

    pio_sm_config c = ws2812_program_get_default_config(offset);

    pio_gpio_init(s_pio, ledDataPin);
    pio_sm_set_consecutive_pindirs(s_pio, s_sm, ledDataPin, 1, true);

    sm_config_set_sideset_pins(&c, ledDataPin);
    // Shift left (MSB first), autopull a fresh word every 24 bits.
    sm_config_set_out_shift(&c, false, true, 24);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

    // The PIO program spends (T1 + T2 + T3) cycles per bit; WS2812 wants 800 kbit/s.
    float div = clock_get_hz(clk_sys) / (800000.0f * (ws2812_T1 + ws2812_T2 + ws2812_T3));
    sm_config_set_clkdiv(&c, div);

    pio_sm_init(s_pio, s_sm, offset, &c);
    pio_sm_set_enabled(s_pio, s_sm, true);

    return 0;
}

void led_load_column(const Pixel* pixels) {
    for (uint i = 0; i < matrixRows; ++i) {
        // WS2812 wire order is GRB (not RGB), MSB first; autopull every 24 bits
        // means the 24-bit value must be left-justified in the 32-bit word.
        s_frameBuf[i] = (uint32_t)pixels[i].g << 24
                      | (uint32_t)pixels[i].r << 16
                      | (uint32_t)pixels[i].b << 8;
    }
}

void led_flush_frame() {
    // The DMA engine feeds the state machine's TX FIFO at exactly the rate the
    // SM drains it (DREQ pacing), so no CPU involvement is needed per word.
    dma_channel_config c = dma_channel_get_default_config(s_dmaChan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);   // walk through the buffer
    channel_config_set_write_increment(&c, false); // always the same FIFO
    channel_config_set_dreq(&c, pio_get_dreq(s_pio, s_sm, true));

    dma_channel_configure(s_dmaChan, &c,
        &s_pio->txf[s_sm], // dst: state machine TX FIFO
        s_frameBuf,        // src: pixel words
        matrixRows,
        true);             // start immediately

    // Wait for the DMA to finish, then for the TX FIFO to drain...
    dma_channel_wait_for_finish_blocking(s_dmaChan);
    while (!pio_sm_is_tx_fifo_empty(s_pio, s_sm)) {
        tight_loop_contents();
    }
    // ...then hold the line low so the strip latches the frame.
    sleep_us(latchTimeUs);
}
