#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void toBase64(uint8_t* data, std::size_t length, std::string& dest);
std::vector<uint8_t> fromBase64(const std::string& input);