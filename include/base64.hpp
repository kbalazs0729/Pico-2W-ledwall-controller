#pragma once

#include <cstdint>
#include <string>
#include <vector>

void toBase64(const uint8_t* data, std::size_t length, std::string& dest);
std::vector<uint8_t> fromBase64(const std::string& input);