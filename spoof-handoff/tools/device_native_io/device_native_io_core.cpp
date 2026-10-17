#include "device_native_io_core.h"

#include <array>

namespace device_native_io {
namespace {

int HexDigit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

}  // namespace

bool ParseMac(std::string_view text, MacAddress* result) {
    if (result == nullptr || text.size() != 17) return false;
    MacAddress parsed{};
    for (std::size_t i = 0; i < parsed.bytes.size(); ++i) {
        const std::size_t offset = i * 3;
        const int high = HexDigit(text[offset]);
        const int low = HexDigit(text[offset + 1]);
        if (high < 0 || low < 0) return false;
        if (i != parsed.bytes.size() - 1 && text[offset + 2] != ':') return false;
        parsed.bytes[i] = static_cast<std::uint8_t>((high << 4) | low);
    }
    *result = parsed;
    return true;
}

bool IsUnicastMac(const MacAddress& address) {
    bool hasNonzeroByte = false;
    for (const auto value : address.bytes) hasNonzeroByte = hasNonzeroByte || value != 0;
    return hasNonzeroByte && (address.bytes[0] & 0x01u) == 0;
}

std::string FormatMac(const MacAddress& address) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(17);
    for (std::size_t i = 0; i < address.bytes.size(); ++i) {
        if (i != 0) result.push_back(':');
        result.push_back(kHex[address.bytes[i] >> 4]);
        result.push_back(kHex[address.bytes[i] & 0x0f]);
    }
    return result;
}

std::string HexBytes(const std::uint8_t* bytes, std::size_t length) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(length * 2);
    for (std::size_t i = 0; i < length; ++i) {
        result.push_back(kHex[bytes[i] >> 4]);
        result.push_back(kHex[bytes[i] & 0x0f]);
    }
    return result;
}

std::string JsonEscape(std::string_view text) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(text.size() + 8);
    for (const unsigned char value : text) {
        switch (value) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (value < 0x20) {
                    result += "\\u00";
                    result.push_back(kHex[value >> 4]);
                    result.push_back(kHex[value & 0x0f]);
                } else {
                    result.push_back(static_cast<char>(value));
                }
                break;
        }
    }
    return result;
}

}  // namespace device_native_io
