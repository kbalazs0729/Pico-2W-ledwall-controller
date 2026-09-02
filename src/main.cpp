#include "app.hpp"
#include "mdns.hpp"
#include "base64.hpp"
#include "led_data.hpp"
#include "api_server.hpp"
#include "lwip_guard.hpp"

#include <atomic>
#include <boards/pico_w.h>
#include <stdio.h>

struct SharedData {
    LedData<matrixRows, matrixCols> matrix;
    bool ledState;
};
SharedData sharedData {
    .matrix = {},
    .ledState = false
};

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

// ---- Rainbow scroll animation --------------------------------------------------

constexpr float rainbowCyclesPerSec = 2.0f; // one full hue cycle scrolls by every 4 s
constexpr uint8_t brightness = 255;          // 0..255, 128 = 50%

// Accumulated hue offset in 1/256 hue units; wraps at 256.
static float huePhase = 0.0f;

// Fast full-saturation HSV->RGB ("color wheel"): pos 0..255 sweeps R->G->B->R.
static Pixel wheel(uint8_t pos) {
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
    // Scale to the target brightness (keeps the hue, halves the intensity).
    p.r = (p.r * brightness) >> 8;
    p.g = (p.g * brightness) >> 8;
    p.b = (p.b * brightness) >> 8;
    return p;
}

// Advances the rainbow by dt seconds and writes the new frame into the matrix.
// The phase is delta-time driven, so the scroll speed is frame-rate independent.
static void animate_rainbow(float dt) {
    huePhase += rainbowCyclesPerSec * dt * 256.0f;
    if (huePhase >= 256.0f) {
        huePhase -= 256.0f;
    }

    for (uint i = 0; i < matrixRows; ++i) {
        uint8_t hue = static_cast<uint8_t>(i * 256 / matrixRows + huePhase);
        sharedData.matrix.columns[activeColumn].pixels[i] = wheel(hue);
    }
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

    uint64_t lastFrameUs = time_us_64();
    absolute_time_t nextFrame = make_timeout_time_us(frameIntervalUs);

    while (true) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, sharedData.ledState);

        uint64_t nowUs = time_us_64();
        float dt = (nowUs - lastFrameUs) / 1e6f;
        lastFrameUs = nowUs;

        {
            // Animation + snapshot are both fast (us-scale); the lwIP lock must
            // NOT be held while waiting for the frame to clock out (~5 ms).
            LwipGuard guard{};
            animate_rainbow(dt);
            led_load_column(sharedData.matrix.columns[activeColumn].pixels.data());
        }
        led_flush_frame();

        // Pace to 60 fps against the 1 MHz hardware timer. Fixed increments
        // of nextFrame avoid drift; if a frame ever overruns, sleep_until
        // returns immediately and we simply run late rather than skip.
        sleep_until(nextFrame);
        nextFrame = delayed_by_us(nextFrame, frameIntervalUs);
    }

    return 0;
}
