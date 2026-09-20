#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace hypersinv::overlay {

inline constexpr std::uint32_t kOverlayMagic = 0x314F5648u; // HVO1
inline constexpr std::uint16_t kOverlayVersion = 1;
inline constexpr std::size_t kMaximumEntities = 256;
inline constexpr wchar_t kDefaultMappingName[] =
    L"Local\\HypersinvOverlayDataV1";

// Colors use 0xAARRGGBB. Coordinates are in physical pixels relative to the
// target window's client area.
struct EntityData {
    float screenX;
    float screenY;
    float width;
    float height;
    wchar_t name[32];
    std::uint32_t color;
    std::uint32_t isVisible;
};

// The producer owns this mapping. It publishes a snapshot by making Sequence
// odd, updating the payload, issuing a full memory barrier, then making
// Sequence even. The reader never writes to the mapping.
struct alignas(8) SharedOverlayHeader {
    std::uint32_t Magic;
    std::uint16_t Version;
    std::uint16_t HeaderSize;
    std::uint32_t EntityStride;
    std::uint32_t Capacity;
    std::uint32_t EntityCount;
    std::uint32_t Reserved0;
    volatile std::uint64_t Sequence;
    std::uint64_t ProducerTimestampQpc;
    std::uint64_t TargetWindow;
    std::uint32_t ProducerProcessId;
    std::uint32_t Reserved1;
};

struct alignas(8) SharedOverlayBuffer {
    SharedOverlayHeader Header;
    EntityData Entities[kMaximumEntities];
};

struct OverlaySnapshot {
    SharedOverlayHeader Header{};
    EntityData Entities[kMaximumEntities]{};
};

inline bool IsHeaderValid(const SharedOverlayHeader& header) noexcept {
    return header.Magic == kOverlayMagic &&
           header.Version == kOverlayVersion &&
           header.HeaderSize == sizeof(SharedOverlayHeader) &&
           header.EntityStride == sizeof(EntityData) &&
           header.Capacity > 0 &&
           header.Capacity <= kMaximumEntities &&
           header.EntityCount <= header.Capacity &&
           header.Reserved0 == 0 &&
           header.Reserved1 == 0;
}

static_assert(std::is_trivially_copyable_v<EntityData>);
static_assert(std::is_trivially_copyable_v<SharedOverlayHeader>);
static_assert(alignof(SharedOverlayHeader) >= alignof(std::uint64_t));
static_assert(offsetof(SharedOverlayHeader, Sequence) % alignof(std::uint64_t) == 0);

} // namespace hypersinv::overlay
