#include "app.hpp"
#include "mdns.hpp"
#include "base64.hpp"
#include "led_data.hpp"
#include "api_server.hpp"
#include "lwip_guard.hpp"

#include <cmath>
#include <stdio.h>

// What currently owns the matrix: the procedural animation, or a frame
// posted via POST /matrix.
enum class DisplayMode : uint8_t {
    Animation,
    Manual
};

struct SharedData {
    LedData<matrixRows, matrixCols> matrix;
    bool ledState;
    DisplayMode mode;
};
SharedData sharedData {
    .matrix = {},
    .ledState = false,
    .mode = DisplayMode::Animation
};

// The base64 GET/POST endpoints serialize the matrix with reinterpret_cast +
// sizeof; guard the packed-layout assumption that makes that well-defined.
static_assert(sizeof(sharedData.matrix) == matrixRows * matrixCols * 3,
              "LedData must stay packed: rows * cols * 3 bytes");

void setup_routes(ApiServer& server) {
    server.add_endpoint("/", Method::GET, [](std::string_view) -> Response {
        // static: built once, reused for every request (heap, not ROM)
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
            "           <li>/matrix - POST: Accept base64 encoded LED matrix data, show it (switches to manual mode).</li>"
            "           <li>/animate - GET: Switch back to the procedural animation.</li>"
            "       </ul>"
            "   </body>"
            "</html>"
        );

        return {200, "text/html", content.c_str()};
    });

    server.add_endpoint("/led", Method::GET, [](std::string_view) -> Response {
        LwipGuard guard{};
        sharedData.ledState = !sharedData.ledState;
        return {200, "text/plain", sharedData.ledState ? "LED ON" : "LED OFF"};
    });

    server.add_endpoint("/animate", Method::GET, [](std::string_view) -> Response {
        LwipGuard guard{};
        sharedData.mode = DisplayMode::Animation;
        return {200, "text/plain", "Animation mode"};
    });

    server.add_endpoint("/matrix", Method::GET, [](std::string_view) -> Response {
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
            std::memcpy(&sharedData.matrix, decodedData.data(), decodedData.size());
            // A posted frame wins over the animation until /animate is called.
            sharedData.mode = DisplayMode::Manual;
        }

        return {200, "text/plain", "Matrix updated successfully"};
    });
}

// ---- Rainbow scroll animation --------------------------------------------------
// Parameters (rainbowCyclesPerSec, brightness) live in config.hpp.

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
    // Scale to the target brightness (keeps the hue; >>8 means 255 maps to
    // 254, a standard fast approximation of full scale).
    p.r = (p.r * brightness) >> 8;
    p.g = (p.g * brightness) >> 8;
    p.b = (p.b * brightness) >> 8;
    return p;
}

// Advances the rainbow by dt seconds and writes the new frame into the matrix.
// The phase is delta-time driven, so the scroll speed is frame-rate independent.
// One full hue cycle spans the whole matrix diagonally (columns included).
static void animate_rainbow(float dt) {
    huePhase += rainbowCyclesPerSec * dt * 256.0f;
    // fmodf (not a single subtract): a long stall must not leave the phase
    // denormalized for several frames.
    huePhase = std::fmodf(huePhase, 256.0f);

    for (uint col = 0; col < matrixCols; ++col) {
        for (uint row = 0; row < matrixRows; ++row) {
            uint32_t index = col * matrixRows + row;
            uint8_t hue = static_cast<uint8_t>(index * 256 / (matrixCols * matrixRows) + huePhase);
            sharedData.matrix.columns[col].pixels[row] = wheel(hue);
        }
    }
}

int main() {
    if (init_hardware() != 0) {
        printf("Hardware initialization failed\n");
        return -1;
    }

    auto server = ApiServer(httpPort);
    setup_routes(server);
    printf("HTTP server started on port %d with IP %s\n", httpPort, ip4addr_ntoa(netif_ip4_addr(netif_list)));

    [[maybe_unused]] auto mdns = MdnsServer(hostname, "ledfal", httpPort);
    printf("mDNS responder started with hostname: %s.local\n", hostname);

    uint64_t lastFrameUs = time_us_64();
    absolute_time_t nextFrame = make_timeout_time_us(frameIntervalUs);

    while (true) {
        {
            LwipGuard guard{};
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, sharedData.ledState);
        }

        uint64_t nowUs = time_us_64();
        float dt = (nowUs - lastFrameUs) / 1e6f;
        lastFrameUs = nowUs;

        {
            // Animation + transpose are both fast (us-scale); the lwIP lock
            // must NOT be held while waiting for the frame to clock out (ms).
            LwipGuard guard{};
            if (sharedData.mode == DisplayMode::Animation) {
                animate_rainbow(dt);
            }
            led_load_frame(sharedData.matrix.columns[0].pixels.data());
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
