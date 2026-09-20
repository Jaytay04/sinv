#include "overlay_protocol.h"
#include "data_transform.h"
#include "shared_memory_publisher.h"
#include "shared_memory_reader.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

using namespace hypersinv::overlay;

namespace {

int Fail(const char* message) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}
} // namespace

int wmain() {
    wchar_t mappingName[96]{};
    std::swprintf(mappingName,
                  _countof(mappingName),
                  L"Local\\HypersinvOverlayProtocolTest_%lu_%llu",
                  GetCurrentProcessId(),
                  static_cast<unsigned long long>(GetTickCount64()));

    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE,
                                        nullptr,
                                        PAGE_READWRITE,
                                        0,
                                        sizeof(SharedOverlayBuffer),
                                        mappingName);
    if (mapping == nullptr) {
        return Fail("CreateFileMappingW");
    }
    auto* shared = static_cast<SharedOverlayBuffer*>(
        MapViewOfFile(mapping,
                      FILE_MAP_READ | FILE_MAP_WRITE,
                      0,
                      0,
                      sizeof(SharedOverlayBuffer)));
    if (shared == nullptr) {
        CloseHandle(mapping);
        return Fail("MapViewOfFile");
    }

    std::memset(shared, 0, sizeof(*shared));
    shared->Header.Magic = kOverlayMagic;
    shared->Header.Version = kOverlayVersion;
    shared->Header.HeaderSize = sizeof(SharedOverlayHeader);
    shared->Header.EntityStride = sizeof(EntityData);
    shared->Header.Capacity = static_cast<std::uint32_t>(kMaximumEntities);
    shared->Header.EntityCount = 1;
    shared->Header.Sequence = 2;
    shared->Entries[0] = EntityData{10.0f,
                                     20.0f,
                                     30.0f,
                                     40.0f,
                                     0.0f, 0.0f, 0.0f,
                                     static_cast<std::uint32_t>(
                                         CoordinateSpace::Screen),
                                     L"protocol-test",
                                     0xff123456u,
                                     1};

    SharedMemoryReader reader;
    if (!reader.Open(mappingName)) {
        UnmapViewOfFile(shared);
        CloseHandle(mapping);
        return Fail("reader.Open");
    }

    OverlaySnapshot snapshot{};
    if (reader.Read(snapshot) != SnapshotReadStatus::Success ||
        snapshot.Header.EntityCount != 1 ||
        snapshot.Entries[0].screenX != 10.0f ||
        snapshot.Entries[0].color != 0xff123456u ||
        std::wstring(snapshot.Entries[0].name) != L"protocol-test") {
        return Fail("valid snapshot round trip");
    }

    shared->Header.Sequence = 3;
    if (reader.Read(snapshot) != SnapshotReadStatus::Busy) {
        return Fail("odd sequence must not be consumed");
    }

    shared->Header.Sequence = 4;
    shared->Header.EntityCount =
        static_cast<std::uint32_t>(kMaximumEntities + 1);
    if (reader.Read(snapshot) != SnapshotReadStatus::InvalidData) {
        return Fail("oversized entity count must be rejected");
    }

    reader.Close();
    UnmapViewOfFile(shared);
    CloseHandle(mapping);
    std::puts("PASS: overlay shared-memory protocol");
    return 0;
}
