#include "../device_native_io_core.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void Check(bool condition, const char* name) {
    if (!condition) {
        std::cerr << "FAIL: " << name << '\n';
        ++failures;
    }
}

void TestLockedAddresses() {
    struct AddressCase { const char* text; const char* expected; };
    constexpr AddressCase cases[] = {
        {"00:50:56:C0:00:08", "00:50:56:C0:00:08"},
        {"00:50:56:C0:00:01", "00:50:56:C0:00:01"},
        {"C4:BD:E5:32:06:D1", "C4:BD:E5:32:06:D1"},
        {"D8:BB:C1:D8:57:20", "D8:BB:C1:D8:57:20"},
        {"02:90:10:20:30:40", "02:90:10:20:30:40"},
        {"02:90:10:20:30:41", "02:90:10:20:30:41"},
        {"02:90:10:20:30:42", "02:90:10:20:30:42"},
        {"02:90:10:20:30:43", "02:90:10:20:30:43"}
    };
    for (const auto& item : cases) {
        device_native_io::MacAddress address{};
        Check(device_native_io::ParseMac(item.text, &address), "parse locked address");
        Check(device_native_io::FormatMac(address) == item.expected, "format round trip");
        Check(device_native_io::IsUnicastMac(address), "locked address is unicast");
    }
}

void TestMalformedAndMulticastAddresses() {
    device_native_io::MacAddress address{};
    Check(!device_native_io::ParseMac("02-90-10-20-30-40", &address), "reject hyphen separators");
    Check(!device_native_io::ParseMac("02:90:10:20:30:4G", &address), "reject non-hex digit");
    Check(!device_native_io::ParseMac("02:90:10:20:30:4", &address), "reject short address");
    Check(!device_native_io::ParseMac("01:00:5E:00:00:01", &address) ||
          !device_native_io::IsUnicastMac(address), "reject multicast address as unicast");
    Check(!device_native_io::ParseMac("00:00:00:00:00:00", &address) ||
          !device_native_io::IsUnicastMac(address), "reject zero address as unicast");
    Check(!device_native_io::ParseMac("00:00:00:00:00:00", nullptr), "reject null output pointer");
}

void TestHexAndJsonEscaping() {
    const std::uint8_t bytes[] = {0x02, 0x90, 0x10, 0x20, 0x30, 0x40};
    Check(device_native_io::HexBytes(bytes, sizeof(bytes)) == "029010203040", "raw MAC byte hex");
    Check(device_native_io::JsonEscape("a\\b\"c\n") == "a\\\\b\\\"c\\n", "JSON escapes path characters");
    Check(device_native_io::JsonEscape(std::string_view("x\x01", 2)) == "x\\u0001", "JSON escapes control bytes");
}

}  // namespace

int main() {
    TestLockedAddresses();
    TestMalformedAndMulticastAddresses();
    TestHexAndJsonEscaping();
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "PASS: native reader core assertions\n";
    return EXIT_SUCCESS;
}
