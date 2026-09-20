#include "data_transform.h"
#include "d3d_renderer.h"
#include "overlay_protocol.h"
#include "overlay_window.h"
#include "shared_memory_publisher.h"
#include "shared_memory_reader.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <string>

using namespace hypersinv::overlay;

namespace {

int Fail(const char* message) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

} // namespace

int wmain() {
    OverlayWindow window;
    if (!window.Create(320, 240, false)) {
        return Fail("overlay window creation");
    }

    const LONG_PTR extendedStyle = GetWindowLongPtrW(window.Handle(), GWL_EXSTYLE);
    constexpr LONG_PTR requiredStyle = WS_EX_TOPMOST |
                                       WS_EX_TRANSPARENT |
                                       WS_EX_LAYERED |
                                       WS_EX_TOOLWINDOW |
                                       WS_EX_NOACTIVATE;
    if ((extendedStyle & requiredStyle) != requiredStyle) {
        return Fail("required extended window styles");
    }

    COLORREF colorKey = RGB(1, 1, 1);
    BYTE alpha = 0;
    DWORD layeredFlags = 0;
    if (!GetLayeredWindowAttributes(window.Handle(),
                                    &colorKey,
                                    &alpha,
                                    &layeredFlags) ||
        colorKey != RGB(0, 0, 0) ||
        (layeredFlags & LWA_COLORKEY) == 0) {
        return Fail("transparent black color key");
    }

    if (SendMessageW(window.Handle(), WM_NCHITTEST, 0, 0) != HTTRANSPARENT ||
        SendMessageW(window.Handle(), WM_MOUSEACTIVATE, 0, 0) != MA_NOACTIVATE) {
        return Fail("click-through/no-activation window behavior");
    }

    wchar_t firstClass[64]{};
    if (GetClassNameW(window.Handle(), firstClass, _countof(firstClass)) == 0) {
        return Fail("first randomized class name");
    }

    D3dRenderer renderer;
    if (!renderer.Initialize(window.Handle(), 320, 240)) {
        return Fail("D3D11/Direct2D initialization");
    }

    wchar_t mappingName[128]{};
    swprintf_s(mappingName,
               L"Local\\HypersinvPresentTest_%lu_%llu",
               GetCurrentProcessId(),
               static_cast<unsigned long long>(GetTickCount64()));
    SharedMemoryPublisher publisher;
    SharedMemoryReader reader;
    if (!publisher.Open(mappingName, 0) || !reader.Open(mappingName)) {
        return Fail("pipeline mapping initialization");
    }
    AnalysisResult frame{};
    frame.Header.Magic = kOverlayMagic;
    frame.Header.Version = kOverlayVersion;
    frame.Header.HeaderSize = sizeof(SharedOverlayHeader);
    frame.Header.EntityStride = sizeof(EntityData);
    frame.Header.Capacity = static_cast<std::uint32_t>(kMaximumEntities);
    frame.Header.EntityCount = 1;
    frame.Header.SceneFlags = kSceneHasViewProjection;
    frame.Header.SourceGeneration = 1;
    frame.Header.SourceSequence = 1;
    frame.Header.ViewProjection[0] = 1.0f;
    frame.Header.ViewProjection[5] = 1.0f;
    frame.Header.ViewProjection[10] = 1.0f;
    frame.Header.ViewProjection[15] = 1.0f;
    frame.Entries[0].width = 100.0f;
    frame.Entries[0].height = 80.0f;
    frame.Entries[0].coordinateSpace = static_cast<std::uint32_t>(
        CoordinateSpace::World);
    wcscpy_s(frame.Entries[0].name, L"pipeline-render-test");
    frame.Entries[0].color = 0xcc40a0ffu;
    frame.Entries[0].isVisible = 1;

    const auto pipelineStarted = std::chrono::steady_clock::now();
    if (publisher.Publish(frame) != PublishStatus::Success) {
        return Fail("pipeline publish");
    }
    OverlaySnapshot snapshot{};
    if (reader.Read(snapshot) != SnapshotReadStatus::Success ||
        snapshot.Header.EntityCount != 1) {
        return Fail("pipeline read");
    }
    float screenX = 0.0f;
    float screenY = 0.0f;
    if (!ProjectWorldToScreen(snapshot.Header.ViewProjection,
                              snapshot.Entries[0].worldX,
                              snapshot.Entries[0].worldY,
                              snapshot.Entries[0].worldZ,
                              320.0f,
                              240.0f,
                              screenX,
                              screenY) ||
        screenX != 160.0f || screenY != 120.0f ||
        !renderer.BeginFrame()) {
        return Fail("pipeline projection/render begin");
    }
    renderer.DrawBox(screenX,
                     screenY,
                     snapshot.Entries[0].width,
                     snapshot.Entries[0].height,
                     snapshot.Entries[0].color);
    renderer.DrawLine(0.0f, 0.0f, screenX, screenY, 0xff33cc66u);
    renderer.DrawText(screenX,
                      screenY,
                      snapshot.Entries[0].name,
                      0xffffffffu);
    if (!renderer.EndFrame()) {
        return Fail("pipeline D3D11 present");
    }
    const auto pipelineLatency =
        std::chrono::steady_clock::now() - pipelineStarted;
    if (pipelineLatency >= std::chrono::milliseconds(50)) {
        return Fail("publisher-to-present latency exceeded 50 ms");
    }
    renderer.Shutdown();
    window.Destroy();

    OverlayWindow secondWindow;
    if (!secondWindow.Create(64, 64, false)) {
        return Fail("second overlay window creation");
    }
    wchar_t secondClass[64]{};
    if (GetClassNameW(secondWindow.Handle(), secondClass, _countof(secondClass)) == 0 ||
        std::wstring(firstClass) == std::wstring(secondClass)) {
        return Fail("window class name must change per launch instance");
    }
    secondWindow.Destroy();

    std::printf(
        "PASS: overlay window and publisher-to-present pipeline %.3f ms\n",
        std::chrono::duration<double, std::milli>(pipelineLatency).count());
    return 0;
}
