#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hypersinv::overlay {

inline constexpr std::uint32_t kFnv1a32OffsetBasis = 2166136261u;
inline constexpr std::uint32_t kFnv1a32Prime = 16777619u;
inline constexpr std::uint64_t kFnv1a64OffsetBasis =
    UINT64_C(14695981039346656037);
inline constexpr std::uint64_t kFnv1a64Prime = UINT64_C(1099511628211);

struct RollingXorConfig {
    std::array<std::uint8_t, 32> Key{};
    std::uint32_t KeyLength = 0;
    std::uint32_t InitialState = 0;
    std::uint32_t StateStep = 0;
    std::uint32_t StateRotation = 0;
};

struct Fnv1a32Config {
    std::uint32_t OffsetBasis = kFnv1a32OffsetBasis;
    std::uint32_t Prime = kFnv1a32Prime;
};

struct Fnv1a64Config {
    std::uint64_t OffsetBasis = kFnv1a64OffsetBasis;
    std::uint64_t Prime = kFnv1a64Prime;
};

struct SubstitutionTable {
    std::array<std::uint8_t, 256> Encode{};
    std::array<std::uint8_t, 256> Decode{};
};

enum class TransformKind : std::uint32_t {
    None = 0,
    RollingXor = 1,
    Fnv1a32 = 2,
    Fnv1a64 = 3,
    RotateLeft = 4,
    RotateRight = 5,
    Substitute = 6,
    InverseSubstitute = 7,
};

enum class TextEncoding : std::uint32_t {
    Ascii = 1,
    Utf16Le = 2,
};

struct TransformConfig {
    TransformKind Kind = TransformKind::None;
    RollingXorConfig RollingXor{};
    Fnv1a32Config Fnv32{};
    Fnv1a64Config Fnv64{};
    SubstitutionTable Substitution{};
    std::uint32_t RotationBits = 0;
};

bool ApplyRollingXor(const std::uint8_t* input,
                     std::size_t size,
                     const RollingXorConfig& config,
                     std::vector<std::uint8_t>& output) noexcept;

std::uint32_t Fnv1a32(const std::uint8_t* input,
                      std::size_t size,
                      const Fnv1a32Config& config = {}) noexcept;

std::uint64_t Fnv1a64(const std::uint8_t* input,
                      std::size_t size,
                      const Fnv1a64Config& config = {}) noexcept;

bool RotateBytes(const std::uint8_t* input,
                 std::size_t size,
                 std::uint32_t bits,
                 bool left,
                 std::vector<std::uint8_t>& output) noexcept;

bool BuildAffineSubstitution(std::uint8_t multiplier,
                             std::uint8_t addend,
                             SubstitutionTable& table) noexcept;

bool ApplySubstitution(const std::uint8_t* input,
                       std::size_t size,
                       const SubstitutionTable& table,
                       bool inverse,
                       std::vector<std::uint8_t>& output) noexcept;

std::wstring FormatAscii(const std::uint8_t* input, std::size_t size);
std::wstring FormatUtf16Le(const std::uint8_t* input, std::size_t size);

bool TransformToText(const std::uint8_t* input,
                     std::size_t size,
                     const TransformConfig& transform,
                     TextEncoding encoding,
                     std::wstring& output);

bool ProjectWorldToScreen(const float viewProjection[16],
                          float worldX,
                          float worldY,
                          float worldZ,
                          float viewportWidth,
                          float viewportHeight,
                          float& screenX,
                          float& screenY) noexcept;

} // namespace hypersinv::overlay
