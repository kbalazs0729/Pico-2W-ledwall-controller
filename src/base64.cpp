#include "base64.hpp"

void toBase64(uint8_t* data, std::size_t length, std::string& dest) {
    std::string encoded;
    encoded.reserve(((length + 2) / 3) * 4);

    for (std::size_t i = 0; i < length; i += 3) {
        uint32_t octet_a = i < length ? data[i] : 0;
        uint32_t octet_b = (i + 1) < length ? data[i + 1] : 0;
        uint32_t octet_c = (i + 2) < length ? data[i + 2] : 0;

        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        encoded.push_back(base64_chars[(triple >> 18) & 0x3F]);
        encoded.push_back(base64_chars[(triple >> 12) & 0x3F]);
        encoded.push_back((i + 1) < length ? base64_chars[(triple >> 6) & 0x3F] : '=');
        encoded.push_back((i + 2) < length ? base64_chars[triple & 0x3F] : '=');
    }

    dest = std::move(encoded);
}

std::vector<uint8_t> fromBase64(const std::string& input) {
    std::vector<uint8_t> decoded;
    decoded.reserve((input.size() / 4) * 3);

    uint32_t val = 0;
    int valb = -8;
    for (char c : input) {
        if (c == '=') break;

        // Validate via strchr: indexing base64_chars by an arbitrary char
        // reads out of bounds for c >= 65 (the alphabet is only 64 chars).
        // Invalid characters (whitespace, etc.) are skipped.
        const char* pos = c > 0 ? strchr(base64_chars, c) : nullptr;
        if (pos == nullptr) continue;

        val = (val << 6) + (pos - base64_chars);
        valb += 6;
        if (valb >= 0) {
            decoded.push_back((val >> valb) & 0xFF);
            valb -= 8;
        }
    }

    return decoded;
}