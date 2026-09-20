#include "data_transform.h"

#include <array>
#include <cstdio>
#include <string>
#include <vector>

using namespace hypersinv::overlay;

namespace {

int Fail(const char* message) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

bool Equal(const std::vector<std::uint8_t>& value,
           std::initializer_list<std::uint8_t> expected) {
    return value.size() == expected.size() &&
        std::equal(value.begin(), value.end(), expected.begin());
}

} // namespace

int main() {
    constexpr std::array<std::uint8_t, 5> hello{
        0x48, 0x65, 0x6c, 0x6c, 0x6f};
    if (Fnv1a32(hello.data(), hello.size()) != 0xf55c314bu ||
        Fnv1a64(hello.data(), hello.size()) !=
            UINT64_C(0x63f0bfacf2c00f6b)) {
        return Fail("standard FNV-1a vectors");
    }

    RollingXorConfig xorConfig{};
    xorConfig.Key[0] = 0x10;
    xorConfig.Key[1] = 0x20;
    xorConfig.Key[2] = 0x30;
    xorConfig.KeyLength = 3;
    xorConfig.InitialState = 0x12345678u;
    xorConfig.StateStep = 0x01020304u;
    xorConfig.StateRotation = 5;
    std::vector<std::uint8_t> encoded;
    if (!ApplyRollingXor(hello.data(), hello.size(), xorConfig, encoded) ||
        !Equal(encoded, {0x20, 0xc7, 0x90, 0x63, 0x25})) {
        return Fail("rolling XOR test vector");
    }
    std::vector<std::uint8_t> decoded;
    if (!ApplyRollingXor(encoded.data(), encoded.size(), xorConfig, decoded) ||
        !std::equal(decoded.begin(), decoded.end(), hello.begin())) {
        return Fail("rolling XOR reversibility");
    }

    constexpr std::array<std::uint8_t, 2> rotateInput{0x81, 0x12};
    if (!RotateBytes(rotateInput.data(), rotateInput.size(), 1, true, encoded) ||
        !Equal(encoded, {0x03, 0x24}) ||
        !RotateBytes(encoded.data(), encoded.size(), 1, false, decoded) ||
        !std::equal(decoded.begin(), decoded.end(), rotateInput.begin())) {
        return Fail("byte rotation vectors");
    }

    SubstitutionTable table{};
    constexpr std::array<std::uint8_t, 4> substitutionInput{0, 1, 2, 255};
    if (!BuildAffineSubstitution(197, 123, table) ||
        !ApplySubstitution(substitutionInput.data(), substitutionInput.size(),
                           table, false, encoded) ||
        !Equal(encoded, {123, 64, 5, 182}) ||
        !ApplySubstitution(encoded.data(), encoded.size(), table, true, decoded) ||
        !std::equal(decoded.begin(), decoded.end(),
                    substitutionInput.begin()) ||
        BuildAffineSubstitution(2, 1, table)) {
        return Fail("affine substitution vectors");
    }

    constexpr std::array<std::uint8_t, 5> ascii{'A', '\\', 1, 'B', 0};
    if (FormatAscii(ascii.data(), ascii.size()) != L"A\\\\\\x01B") {
        return Fail("ASCII formatting");
    }
    constexpr std::array<std::uint8_t, 6> utf16{
        'H', 0, 'i', 0, 0, 0};
    if (FormatUtf16Le(utf16.data(), utf16.size()) != L"Hi") {
        return Fail("UTF-16LE formatting");
    }

    const float identity[16]{
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };
    float screenX = 0;
    float screenY = 0;
    if (!ProjectWorldToScreen(identity, 0, 0, 0, 800, 600,
                              screenX, screenY) ||
        screenX != 400.0f || screenY != 300.0f ||
        !ProjectWorldToScreen(identity, 1, 1, 0, 800, 600,
                              screenX, screenY) ||
        screenX != 800.0f || screenY != 0.0f) {
        return Fail("world-to-screen projection vectors");
    }

    std::puts("PASS: transformation and projection vectors");
    return 0;
}
