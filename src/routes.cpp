#include "routes.hpp"

#include "base64.hpp"
#include "lwip_guard.hpp"

#include <cstring>
#include <string>

void Routes::registerEndpoints(ApiServer& server) {
    server.add_endpoint("/", Method::GET, [](std::string_view) -> Response {
        // Lives in flash (.rodata), no heap. Response.body is a plain
        // const char*, so this is served straight from ROM.
        static const char content[] =
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
            "           <li>/animation - GET: Report the current display mode and animation id.</li>"
            "           <li>/animation - POST: Select animation by integer id (0 = Rainbow, 1 = BadApple); an empty body resumes the current animation.</li>"
            "           <li>Build date: " __DATE__ " " __TIME__ ".</li>"
            "       </ul>"
            "   </body>"
            "</html>";

        return {200, "text/html", content};
    });

    server.add_endpoint("/led", Method::GET, [this](std::string_view) -> Response {
        LwipGuard guard{};
        m_shared.ledState = !m_shared.ledState;
        return {200, "text/plain", m_shared.ledState ? "LED ON" : "LED OFF"};
    });

    server.add_endpoint("/animation", Method::GET, [this](std::string_view) -> Response {
        LwipGuard guard{};
        // Static scratch buffer, shared safely only because lwIP serializes
        // all callbacks in one context — do NOT touch from the main loop.
        static std::string status {};
        status = std::string("{\"mode\":\"") +
                 (m_shared.mode == DisplayMode::Animation ? "animation" : "manual") +
                 "\",\"animationId\":" +
                 std::to_string(static_cast<unsigned>(m_shared.animationType)) + "}";
        return {200, "application/json", status.c_str()};
    });

    server.add_endpoint("/animation", Method::POST, [this](std::string_view body) -> Response {
        LwipGuard guard{};
        // An empty body resumes the current animation (leaves manual mode
        // without changing the selected animation).
        if (body.empty()) {
            m_shared.mode = DisplayMode::Animation;
            return {200, "text/plain", "Animation resumed"};
        }
        // Otherwise the body is a plain integer: the AnimationType id
        // (0 = Rainbow, 1 = BadApple, ...). The frontend owns the id->name
        // mapping. Reject trailing garbage so "0abc" isn't read as id 0.
        std::string text(body);
        char* end = nullptr;
        unsigned long id = std::strtoul(text.c_str(), &end, 10);
        if (end == text.c_str() || *end != '\0' ||
            id >= static_cast<unsigned long>(AnimationType::Count)) {
            return {400, "text/plain", "Unknown animation id"};
        }
        m_shared.animationType = static_cast<AnimationType>(id);
        m_shared.mode = DisplayMode::Animation;
        return {200, "text/plain", "Animation selected"};
    });

    server.add_endpoint("/matrix", Method::GET, [this](std::string_view) -> Response {
        // Static scratch buffer to avoid reallocation on each request.
        // Safe to share between connections only because lwIP serializes
        // all callbacks in one context — do NOT touch from the main loop.
        static std::string base64Matrix {};
        {
            LwipGuard guard{};
            const uint8_t* matrixData = reinterpret_cast<const uint8_t*>(&m_shared.matrix);
            toBase64(matrixData, sizeof(m_shared.matrix), base64Matrix);
        }

        return {200, "text/plain", base64Matrix.c_str()};
    });

    server.add_endpoint("/matrix", Method::POST, [this](std::string_view body) -> Response {
        {
            LwipGuard guard{};
            // The body may be form-style ("pixels=<base64>"); keep only the value.
            constexpr std::string_view prefix = "pixels=";
            if (body.substr(0, prefix.size()) == prefix) {
                body.remove_prefix(prefix.size());
            }
            auto decodedData = fromBase64(std::string(body));
            if (decodedData.size() != sizeof(m_shared.matrix)) {
                return {400, "text/plain", "Invalid matrix data size"};
            }
            std::memcpy(&m_shared.matrix, decodedData.data(), decodedData.size());
            // A posted frame wins over the animation until /animation resumes it.
            m_shared.mode = DisplayMode::Manual;
        }

        return {200, "text/plain", "Matrix updated successfully"};
    });
}
