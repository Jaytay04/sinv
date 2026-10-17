#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace device_native_io {

struct MacAddress {
    std::array<std::uint8_t, 6> bytes{};
};

bool ParseMac(std::string_view text, MacAddress* result);
bool IsUnicastMac(const MacAddress& address);
std::string FormatMac(const MacAddress& address);
std::string HexBytes(const std::uint8_t* bytes, std::size_t length);
std::string JsonEscape(std::string_view text);

}  // namespace device_native_io
