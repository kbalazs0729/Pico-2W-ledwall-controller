#include "app.hpp"
#include "mdns.hpp"
#include "base64.hpp"
#include "led_data.hpp"
#include "api_server.hpp"
#include "lwip_guard.hpp"

#include <atomic>
#include <boards/pico_w.h>
#include <stdio.h>

#define WIFI_SSID "REDACTED-SSID"
#define WIFI_PASSWORD "REDACTED-PASSWORD"

struct SharedData {
    LedData<75, 21> matrix;
    bool ledState;
};
SharedData sharedData {
    .matrix = {},
    .ledState = false
};

// ---- LED driving (PIO + DMA) ------------------------------------------------

constexpr int kLedPin = 28;
constexpr uint kLedsPerColumn = 75;
constexpr uint8_t kActiveColumn = 0; // drive only this column for now

static PIO led_pio = pio0;
static uint led_sm;
static int led_dma_chan;

// One column of pixels converted to the on-the-wire format the PIO expects.
// Kept separate from the matrix so a frame is a consistent snapshot even if
// an HTTP request updates the matrix mid-frame.
static uint32_t frame_buf[kLedsPerColumn];

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

    led_dma_chan = dma_claim_unused_channel(true);

    uint offset = pio_add_program(led_pio, &ws2812_program);
    led_sm = pio_claim_unused_sm(led_pio, true);

    pio_sm_config c = ws2812_program_get_default_config(offset);

    pio_gpio_init(led_pio, kLedPin);
    pio_sm_set_consecutive_pindirs(led_pio, led_sm, kLedPin, 1, true);

    sm_config_set_sideset_pins(&c, kLedPin);
    // Shift left (MSB first), autopull a fresh word every 24 bits.
    sm_config_set_out_shift(&c, false, true, 24);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

    // The program spends (T1 + T2 + T3) cycles per bit; WS2812 wants 800 kbit/s.
    float div = clock_get_hz(clk_sys) / (800000.0f * (ws2812_T1 + ws2812_T2 + ws2812_T3));
    sm_config_set_clkdiv(&c, div);

    pio_sm_init(led_pio, led_sm, offset, &c);
    pio_sm_set_enabled(led_pio, led_sm, true);

    return 0;
}

// Snapshot one matrix column into frame_buf, converting each pixel to the
// wire format: 24 bits GRB, MSB first, left-justified in a 32-bit word.
static void load_column(uint8_t col) {
    LwipGuard guard{};
    for (uint i = 0; i < kLedsPerColumn; ++i) {
        const Pixel& p = sharedData.matrix.columns[col].pixels[i];
        frame_buf[i] = (uint32_t)p.g << 24 | (uint32_t)p.r << 16 | (uint32_t)p.b << 8;
    }
}

// Hand the frame buffer to the DMA engine, which feeds the state machine's
// TX FIFO at exactly the rate the SM drains it (DREQ pacing). The CPU is
// free while the transfer runs.
static void start_frame_dma(const uint32_t* data, uint32_t word_count) {
    dma_channel_config c = dma_channel_get_default_config(led_dma_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);   // walk through the buffer
    channel_config_set_write_increment(&c, false); // always the same FIFO
    channel_config_set_dreq(&c, pio_get_dreq(led_pio, led_sm, true));

    dma_channel_configure(led_dma_chan, &c,
        &led_pio->txf[led_sm], // dst: state machine TX FIFO
        data,                  // src: pixel words
        word_count,
        true);                 // start immediately
}

void setup_routes(ApiServer& server) {
    server.add_endpoint("/", Method::GET, [](std::string_view body) -> Response {
        // Allocated as static const to serve from ROM
        static const auto content = std::string(
            "<html>"
            "   <head>"
            "       <title>Pico W LED Matrix API</title>"
            "       <style>body { font-family: Arial, sans-serif; background-color: #222; color: #eee; padding: 20px; }</style>"
            "   </head>"
            "   <body>"
            "       This is the Pico W LED Matrix API Endpoint.<br>"
            "       Available endpoints:<br>"
            "       <ul>"
            "           <li>/led - GET: Toggle the LED state and return the current state.</li>"
            "           <li>/matrix - GET: Return the current LED matrix data in base64 format.</li>"
            "           <li>/matrix - POST: Accept base64 encoded LED matrix data and update the matrix.</li>"
            "       </ul>"
            "   </body>"
            "</html>"
        );
        
        return {200, "text/html", content.c_str()};
    });
    
    server.add_endpoint("/led", Method::GET, [](std::string_view body) -> Response {
        {
            LwipGuard guard{};
            sharedData.ledState = !sharedData.ledState;
        }
        
        return {200, "text/plain", sharedData.ledState ? "LED ON" : "LED OFF"};
    });

    server.add_endpoint("/matrix", Method::GET, [](std::string_view body) -> Response {
        // Alloced as a static scratch buffer to avoid reallocation on each request.
        static std::string base64Matrix {};
        {
            LwipGuard guard{};
            uint8_t* matrixData = reinterpret_cast<uint8_t*>(&sharedData.matrix);
            std::size_t matrixSize = sizeof(sharedData.matrix);
            toBase64(matrixData, matrixSize, base64Matrix);
        }
        
        return {200, "text/plain", base64Matrix.c_str()};
    });

    server.add_endpoint("/matrix", Method::POST, [](std::string_view body) -> Response {
        {
            LwipGuard guard{};
            // The body may be form-style ("pixels=<base64>"); keep only the value.
            constexpr std::string_view prefix = "pixels=";
            if (body.substr(0, prefix.size()) == prefix) {
                body.remove_prefix(prefix.size());
            }
            auto decodedData = fromBase64(std::string(body));
            if (decodedData.size() != sizeof(sharedData.matrix)) {
                return {400, "text/plain", "Invalid matrix data size"};
            }
            std::memcpy(&sharedData.matrix.columns, decodedData.data(), decodedData.size());
        }

        return {200, "text/plain", "Matrix updated successfully"};
    });
}

int main() {
    if (init_hardware() != 0) {
        printf("Hardware initialization failed\n");
        return -1;
    }

    const int port = 80;
    auto server = ApiServer(port);
    setup_routes(server);
    printf("HTTP server started on port %d with IP %s\n", port, ip4addr_ntoa(netif_ip4_addr(netif_list)));

    const char* hostname = "ledfal";
    auto mdns = MdnsServer(hostname, "ledfal");
    printf("mDNS responder started with hostname: %s.local\n", hostname);

    // Fill the matrix with a gradient by index
    for (uint8_t col = 0; col < 21; ++col) {
        for (uint8_t row = 0; row < 75; ++row) {
            sharedData.matrix.columns[col].pixels[row] = {static_cast<uint8_t>(col * 255 / 20), static_cast<uint8_t>(row * 255 / 74), 0};
        }
    }

    while (true) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, sharedData.ledState);

        load_column(kActiveColumn);
        start_frame_dma(frame_buf, kLedsPerColumn);

        // Wait for the DMA to finish, then for the TX FIFO to drain...
        dma_channel_wait_for_finish_blocking(led_dma_chan);
        while (!pio_sm_is_tx_fifo_empty(led_pio, led_sm)) {
            tight_loop_contents();
        }
        // ...then hold the line low so the strip latches the frame.
        // Modern WS2812 clones need >= 280 us of low time; 400 us also
        // covers the last word still shifting out of the OSR (~30 us).
        sleep_us(400);
    }

    return 0;
}
