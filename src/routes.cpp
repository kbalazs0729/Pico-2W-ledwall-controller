#include "routes.hpp"
#include "base64.hpp"
#include "lwip_guard.hpp"
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

// Strict unsigned parse of the whole view: rejects empty input, non-numeric
// text, and trailing garbage ("0abc"), and enforces an inclusive maximum.
bool parseUint(std::string_view text, unsigned long maxValue, unsigned long& out) {
    if (text.empty()) return false;
    std::string buffer(text);
    char* end = nullptr;
    unsigned long value = std::strtoul(buffer.c_str(), &end, 10);
    if (end == buffer.c_str() || *end != '\0' || value > maxValue) return false;
    out = value;
    return true;
}

} // namespace

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
            "           <li>/matrix - GET: Return the current LED matrix data in base64 format.</li>"
            "           <li>/matrix - POST: Accept base64 encoded LED matrix data, show it (switches to manual mode).</li>"
            "           <li>/animation - GET: Report the current display mode and animation id.</li>"
            "           <li>/animation - POST: Select animation by integer id (0 = Rainbow, 1 = BadApple, 2 = Fire, 3 = Rain, 4 = Stars, 5 = Plasma, 6 = Fire2, 7 = RainFill); an empty body resumes the current animation.</li>"
            "           <li>/brightness - GET: Report the current global brightness (0-255).</li>"
            "           <li>/brightness - POST: Set the global brightness (integer 0-255).</li>"
            "           <li>Build date: " __DATE__ " " __TIME__ ".</li>"
            "       </ul>"
            "   </body>"
            "</html>";

        return {200, "text/html", content};
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
        // mapping. Strict parse so "0abc" isn't read as id 0.
        unsigned long id = 0;
        if (!parseUint(body, static_cast<unsigned long>(AnimationType::Count) - 1, id)) {
            return {400, "text/plain", "Unknown animation id"};
        }
        m_shared.animationType = static_cast<AnimationType>(id);
        m_shared.mode = DisplayMode::Animation;
        return {200, "text/plain", "Animation selected"};
    });

    server.add_endpoint("/brightness", Method::GET, [this](std::string_view) -> Response {
        LwipGuard guard{};
        // Static scratch buffer, shared safely only because lwIP serializes
        // all callbacks in one context — do NOT touch from the main loop.
        static std::string status {};
        status = "{\"brightness\":" + std::to_string(m_shared.brightness) + "}";
        return {200, "application/json", status.c_str()};
    });

    server.add_endpoint("/brightness", Method::POST, [this](std::string_view body) -> Response {
        LwipGuard guard{};
        unsigned long value = 0;
        if (!parseUint(body, 255, value)) {
            return {400, "text/plain", "Brightness must be an integer 0-255"};
        }
        m_shared.brightness = static_cast<uint8_t>(value);
        return {200, "text/plain", "Brightness set"};
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
