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
    for (uint8_t col = 0; col < matrixCols; ++col) {
        for (uint8_t row = 0; row < matrixRows; ++row) {
            sharedData.matrix.columns[col].pixels[row] = {
                static_cast<uint8_t>(matrixCols > 1 ? col * 255 / (matrixCols - 1) : 0),
                static_cast<uint8_t>(row * 255 / (matrixRows - 1)),
                0};
        }
    }

    while (true) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, sharedData.ledState);

        {
            // Snapshot only; the lwIP lock must not be held while waiting
            // for the frame to clock out (~5 ms).
            LwipGuard guard{};
            led_load_column(sharedData.matrix.columns[activeColumn].pixels.data());
        }
        led_flush_frame();
    }

    return 0;
}
