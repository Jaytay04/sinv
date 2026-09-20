#include "data_transform.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <limits>

namespace hypersinv::overlay {
namespace {

std::uint32_t RotateLeft32(std::uint32_t value, std::uint32_t bits) noexcept {
    bits &= 31u;
    return bits == 0 ? value : (value << bits) | (value >> (32u - bits));
}

std::uint8_t RotateByte(std::uint8_t value,
                        std::uint32_t bits,
                        bool left) noexcept {
    bits &= 7u;
    if (bits == 0) {
        return value;
    }
    return left
        ? static_cast<std::uint8_t>((value << bits) | (value >> (8u - bits)))
        : static_cast<std::uint8_t>((value >> bits) | (value << (8u - bits)));
}

void AppendHexByte(std::wstring& output, std::uint8_t value) {
    constexpr wchar_t digits[] = L"0123456789ABCDEF";
    output += L"\\x";
    output += digits[(value >> 4u) & 0x0fu];
    output += digits[value & 0x0fu];
}

bool IsHighSurrogate(std::uint16_t value) noexcept {
    return value >= 0xd800u && value <= 0xdbffu;
}

bool IsLowSurrogate(std::uint16_t value) noexcept {
    return value >= 0xdc00u && value <= 0xdfffu;
}

} // namespace

bool ApplyRollingXor(const std::uint8_t* input,
                     std::size_t size,
                     const RollingXorConfig& config,
                     std::vector<std::uint8_t>& output) noexcept {
    output.clear();
    if ((input == nullptr && size != 0) || config.KeyLength == 0 ||
        config.KeyLength > config.Key.size() || config.StateRotation > 31) {
        return false;
    }
    try {
        output.resize(size);
    } catch (...) {
        return false;
    }
    std::uint32_t state = config.InitialState;
    for (std::size_t index = 0; index < size; ++index) {
        const std::uint8_t scheduled = config.Key[index % config.KeyLength];
        const std::uint8_t stream = static_cast<std::uint8_t>(
            scheduled ^ static_cast<std::uint8_t>(state));
        output[index] = static_cast<std::uint8_t>(input[index] ^ stream);
        state = RotateLeft32(state + config.StateStep + scheduled,
                             config.StateRotation);
    }
    return true;
}

std::uint32_t Fnv1a32(const std::uint8_t* input,
                      std::size_t size,
                      const Fnv1a32Config& config) noexcept {
    if (input == nullptr && size != 0) {
        return 0;
    }
    std::uint32_t hash = config.OffsetBasis;
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= input[index];
        hash *= config.Prime;
    }
    return hash;
}

std::uint64_t Fnv1a64(const std::uint8_t* input,
                      std::size_t size,
                      const Fnv1a64Config& config) noexcept {
    if (input == nullptr && size != 0) {
        return 0;
    }
    std::uint64_t hash = config.OffsetBasis;
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= input[index];
        hash *= config.Prime;
    }
    return hash;
}

bool RotateBytes(const std::uint8_t* input,
                 std::size_t size,
                 std::uint32_t bits,
                 bool left,
                 std::vector<std::uint8_t>& output) noexcept {
    output.clear();
    if ((input == nullptr && size != 0) || bits > 7) {
        return false;
    }
    try {
        output.resize(size);
    } catch (...) {
        return false;
    }
    for (std::size_t index = 0; index < size; ++index) {
        output[index] = RotateByte(input[index], bits, left);
    }
    return true;
}

bool BuildAffineSubstitution(std::uint8_t multiplier,
                             std::uint8_t addend,
                             SubstitutionTable& table) noexcept {
    if ((multiplier & 1u) == 0) {
        return false;
    }
    std::array<bool, 256> seen{};
    for (std::uint32_t value = 0; value < 256; ++value) {
        const std::uint8_t encoded = static_cast<std::uint8_t>(
            value * multiplier + addend);
        if (seen[encoded]) {
            return false;
        }
        seen[encoded] = true;
        table.Encode[value] = encoded;
        table.Decode[encoded] = static_cast<std::uint8_t>(value);
    }
    return true;
}

bool ApplySubstitution(const std::uint8_t* input,
                       std::size_t size,
                       const SubstitutionTable& table,
                       bool inverse,
                       std::vector<std::uint8_t>& output) noexcept {
    output.clear();
    if (input == nullptr && size != 0) {
        return false;
    }
    try {
        output.resize(size);
    } catch (...) {
        return false;
    }
    const auto& selected = inverse ? table.Decode : table.Encode;
    for (std::size_t index = 0; index < size; ++index) {
        output[index] = selected[input[index]];
    }
    return true;
}

std::wstring FormatAscii(const std::uint8_t* input, std::size_t size) {
    std::wstring output;
    if (input == nullptr && size != 0) {
        return output;
    }
    output.reserve(size);
    for (std::size_t index = 0; index < size; ++index) {
        const std::uint8_t value = input[index];
        if (value == 0) {
            break;
        }
        if (value >= 0x20u && value <= 0x7eu && value != '\\') {
            output += static_cast<wchar_t>(value);
        } else if (value == '\\') {
            output += L"\\\\";
        } else {
            AppendHexByte(output, value);
        }
    }
    return output;
}

std::wstring FormatUtf16Le(const std::uint8_t* input, std::size_t size) {
    std::wstring output;
    if (input == nullptr || (size & 1u) != 0) {
        return output;
    }
    for (std::size_t offset = 0; offset < size; offset += 2) {
        const std::uint16_t value = static_cast<std::uint16_t>(input[offset]) |
            static_cast<std::uint16_t>(input[offset + 1]) << 8u;
        if (value == 0) {
            break;
        }
        if (IsHighSurrogate(value)) {
            if (offset + 3 >= size) {
                output += static_cast<wchar_t>(0xfffd);
                break;
            }
            const std::uint16_t next =
                static_cast<std::uint16_t>(input[offset + 2]) |
                static_cast<std::uint16_t>(input[offset + 3]) << 8u;
            if (!IsLowSurrogate(next)) {
                output += static_cast<wchar_t>(0xfffd);
                continue;
            }
            output += static_cast<wchar_t>(value);
            output += static_cast<wchar_t>(next);
            offset += 2;
        } else if (IsLowSurrogate(value)) {
            output += static_cast<wchar_t>(0xfffd);
        } else {
            output += static_cast<wchar_t>(value);
        }
    }
    return output;
}

bool TransformToText(const std::uint8_t* input,
                     std::size_t size,
                     const TransformConfig& transform,
                     TextEncoding encoding,
                     std::wstring& output) {
    output.clear();
    if (input == nullptr && size != 0) {
        return false;
    }
    if (transform.Kind == TransformKind::Fnv1a32) {
        wchar_t buffer[32]{};
        _snwprintf_s(buffer, _countof(buffer), _TRUNCATE, L"%08X",
                     Fnv1a32(input, size, transform.Fnv32));
        output = buffer;
        return true;
    }
    if (transform.Kind == TransformKind::Fnv1a64) {
        wchar_t buffer[32]{};
        _snwprintf_s(buffer, _countof(buffer), _TRUNCATE, L"%016llX",
                     static_cast<unsigned long long>(
                         Fnv1a64(input, size, transform.Fnv64)));
        output = buffer;
        return true;
    }

    std::vector<std::uint8_t> transformed;
    bool valid = true;
    switch (transform.Kind) {
    case TransformKind::None:
        try {
            if (size != 0) {
                transformed.assign(input, input + size);
            }
        } catch (...) {
            valid = false;
        }
        break;
    case TransformKind::RollingXor:
        valid = ApplyRollingXor(input, size, transform.RollingXor, transformed);
        break;
    case TransformKind::RotateLeft:
        valid = RotateBytes(input, size, transform.RotationBits, true,
                            transformed);
        break;
    case TransformKind::RotateRight:
        valid = RotateBytes(input, size, transform.RotationBits, false,
                            transformed);
        break;
    case TransformKind::Substitute:
        valid = ApplySubstitution(input, size, transform.Substitution, false,
                                  transformed);
        break;
    case TransformKind::InverseSubstitute:
        valid = ApplySubstitution(input, size, transform.Substitution, true,
                                  transformed);
        break;
    default:
        valid = false;
        break;
    }
    if (!valid) {
        return false;
    }
    output = encoding == TextEncoding::Utf16Le
        ? FormatUtf16Le(transformed.data(), transformed.size())
        : FormatAscii(transformed.data(), transformed.size());
    return true;
}

bool ProjectWorldToScreen(const float viewProjection[16],
                          float worldX,
                          float worldY,
                          float worldZ,
                          float viewportWidth,
                          float viewportHeight,
                          float& screenX,
                          float& screenY) noexcept {
    if (viewProjection == nullptr || !std::isfinite(worldX) ||
        !std::isfinite(worldY) || !std::isfinite(worldZ) ||
        !std::isfinite(viewportWidth) || !std::isfinite(viewportHeight) ||
        viewportWidth <= 0.0f || viewportHeight <= 0.0f) {
        return false;
    }
    const float clipX = viewProjection[0] * worldX +
        viewProjection[1] * worldY + viewProjection[2] * worldZ +
        viewProjection[3];
    const float clipY = viewProjection[4] * worldX +
        viewProjection[5] * worldY + viewProjection[6] * worldZ +
        viewProjection[7];
    const float clipW = viewProjection[12] * worldX +
        viewProjection[13] * worldY + viewProjection[14] * worldZ +
        viewProjection[15];
    if (!std::isfinite(clipX) || !std::isfinite(clipY) ||
        !std::isfinite(clipW) || clipW <= 0.0001f) {
        return false;
    }
    const float normalizedX = clipX / clipW;
    const float normalizedY = clipY / clipW;
    screenX = (normalizedX + 1.0f) * 0.5f * viewportWidth;
    screenY = (1.0f - normalizedY) * 0.5f * viewportHeight;
    return std::isfinite(screenX) && std::isfinite(screenY);
}

} // namespace hypersinv::overlay
