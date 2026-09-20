#include "overlay_protocol.h"
#include "shared_memory_publisher.h"
#include "shared_memory_reader.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

using namespace hypersinv::overlay;

namespace {

int Fail(const char* message) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

std::wstring UniqueMapping(const wchar_t* suffix) {
    wchar_t value[128]{};
    std::swprintf(value,
                  _countof(value),
                  L"Local\\HypersinvPublisherTest_%lu_%llu_%ls",
                  GetCurrentProcessId(),
                  static_cast<unsigned long long>(GetTickCount64()),
                  suffix);
    return value;
}

AnalysisResult MakeFrame(std::uint32_t marker) {
    AnalysisResult frame{};
    frame.Header.Magic = kOverlayMagic;
    frame.Header.Version = kOverlayVersion;
    frame.Header.HeaderSize = sizeof(SharedOverlayHeader);
    frame.Header.EntityStride = sizeof(EntityData);
    frame.Header.Capacity = static_cast<std::uint32_t>(kMaximumEntities);
    frame.Header.EntityCount = 64;
    frame.Header.SourceGeneration = marker;
    for (std::uint32_t index = 0; index < frame.Header.EntityCount; ++index) {
        EntityData& entry = frame.Entries[index];
        entry.screenX = static_cast<float>(marker);
        entry.screenY = static_cast<float>(index);
        entry.width = 10.0f;
        entry.height = 20.0f;
        entry.coordinateSpace = static_cast<std::uint32_t>(
            CoordinateSpace::Screen);
        entry.color = marker;
        entry.isVisible = 1;
    }
    return frame;
}

} // namespace

int wmain() {
    const std::wstring mappingName = UniqueMapping(L"Tearing");
    SharedMemoryPublisher publisher;
    if (!publisher.Open(mappingName, 0)) {
        return Fail("publisher.Open");
    }
    SharedMemoryReader reader;
    if (!reader.Open(mappingName)) {
        return Fail("reader.Open");
    }

    std::atomic<bool> complete{false};
    std::atomic<bool> torn{false};
    std::thread consumer([&]() {
        OverlaySnapshot snapshot{};
        while (!complete.load(std::memory_order_acquire)) {
            if (reader.Read(snapshot) != SnapshotReadStatus::Success ||
                snapshot.Header.EntityCount == 0) {
                YieldProcessor();
                continue;
            }
            const std::uint32_t marker = static_cast<std::uint32_t>(
                snapshot.Header.SourceGeneration);
            for (std::uint32_t index = 0;
                 index < snapshot.Header.EntityCount; ++index) {
                if (snapshot.Entries[index].screenX !=
                        static_cast<float>(marker) ||
                    snapshot.Entries[index].color != marker ||
                    snapshot.Entries[index].screenY !=
                        static_cast<float>(index)) {
                    torn.store(true, std::memory_order_release);
                    return;
                }
            }
        }
    });

    auto worst = std::chrono::steady_clock::duration::zero();
    for (std::uint32_t marker = 1; marker <= 1000; ++marker) {
        const AnalysisResult frame = MakeFrame(marker);
        const auto start = std::chrono::steady_clock::now();
        if (publisher.Publish(frame) != PublishStatus::Success) {
            complete.store(true, std::memory_order_release);
            consumer.join();
            return Fail("unlimited publisher rejected a frame");
        }
        OverlaySnapshot snapshot{};
        while (reader.Read(snapshot) != SnapshotReadStatus::Success ||
               snapshot.Header.SourceGeneration != marker) {
            YieldProcessor();
        }
        worst = (std::max)(worst, std::chrono::steady_clock::now() - start);
    }
    complete.store(true, std::memory_order_release);
    consumer.join();
    if (torn.load(std::memory_order_acquire)) {
        return Fail("reader observed a torn frame");
    }
    if (worst >= std::chrono::milliseconds(50)) {
        return Fail("synthetic publisher-to-reader latency exceeded 50ms");
    }

    SharedMemoryPublisher limited;
    if (!limited.Open(UniqueMapping(L"Rate"), 60)) {
        return Fail("rate-limited publisher.Open");
    }
    const AnalysisResult frame = MakeFrame(1);
    if (limited.Publish(frame) != PublishStatus::Success ||
        limited.Publish(frame) != PublishStatus::RateLimited) {
        return Fail("60 Hz publisher rate limit");
    }

    const double worstMilliseconds =
        std::chrono::duration<double, std::milli>(worst).count();
    std::printf(
        "PASS: tear-free publisher, 60 Hz limit, worst local latency %.3f ms\n",
        worstMilliseconds);
    return 0;
}
