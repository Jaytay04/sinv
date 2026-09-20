#include "d3d_renderer.h"
#include "data_transform.h"
#include "overlay_protocol.h"
#include "overlay_window.h"
#include "shared_memory_reader.h"

#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <string>
#include <thread>

namespace hypersinv::overlay {
namespace {

struct Options {
    std::wstring mappingName = kDefaultMappingName;
    std::wstring targetTitle;
    DWORD targetProcessId = 0;
    HWND targetWindow = nullptr;
    bool targetExplicit = false;
    bool demo = false;
};

void ShowUsage() {
    MessageBoxW(nullptr,
                L"hypersinv_overlay options:\n"
                L"  --target-pid <pid>\n"
                L"  --target-hwnd <hex-or-decimal-hwnd>\n"
                L"  --target-title <window-title-substring>\n"
                L"  --mapping <Local\\mapping-name>\n"
                L"  --demo\n",
                L"Hypersinv overlay",
                MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
}

bool ParseUnsigned(const wchar_t* text, std::uint64_t& value) {
    if (text == nullptr || text[0] == L'\0') {
        return false;
    }
    wchar_t* end = nullptr;
    const unsigned long long parsed = wcstoull(text, &end, 0);
    if (end == text || *end != L'\0') {
        return false;
    }
    value = static_cast<std::uint64_t>(parsed);
    return true;
}

bool ParseOptions(Options& options) {
    int argumentCount = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments == nullptr) {
        return false;
    }

    bool valid = true;
    for (int i = 1; i < argumentCount && valid; ++i) {
        const std::wstring argument = arguments[i];
        if (argument == L"--help" || argument == L"-h") {
            ShowUsage();
            valid = false;
        } else if (argument == L"--demo") {
            options.demo = true;
        } else if ((argument == L"--mapping" || argument == L"--target-title") &&
                   i + 1 < argumentCount) {
            const std::wstring value = arguments[++i];
            if (argument == L"--mapping") {
                options.mappingName = value;
            } else {
                options.targetTitle = value;
                options.targetExplicit = true;
            }
        } else if ((argument == L"--target-pid" ||
                    argument == L"--target-hwnd") &&
                   i + 1 < argumentCount) {
            std::uint64_t value = 0;
            if (!ParseUnsigned(arguments[++i], value)) {
                valid = false;
            } else if (argument == L"--target-pid" && value <= MAXDWORD) {
                options.targetProcessId = static_cast<DWORD>(value);
                options.targetExplicit = true;
            } else if (argument == L"--target-hwnd") {
                options.targetWindow = reinterpret_cast<HWND>(
                    static_cast<std::uintptr_t>(value));
                options.targetExplicit = true;
            } else {
                valid = false;
            }
        } else {
            valid = false;
        }
    }
    LocalFree(arguments);
    return valid;
}

struct WindowSearch {
    DWORD processId = 0;
    std::wstring title;
    HWND result = nullptr;
};

BOOL CALLBACK FindWindowCallback(HWND hwnd, LPARAM parameter) {
    auto* search = reinterpret_cast<WindowSearch*>(parameter);
    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) {
        return TRUE;
    }

    if (search->processId != 0) {
        DWORD candidateProcessId = 0;
        GetWindowThreadProcessId(hwnd, &candidateProcessId);
        if (candidateProcessId != search->processId) {
            return TRUE;
        }
    }

    if (!search->title.empty()) {
        wchar_t title[512]{};
        GetWindowTextW(hwnd, title, _countof(title));
        std::wstring candidate = title;
        std::wstring expected = search->title;
        std::transform(candidate.begin(), candidate.end(), candidate.begin(), towlower);
        std::transform(expected.begin(), expected.end(), expected.begin(), towlower);
        if (candidate.find(expected) == std::wstring::npos) {
            return TRUE;
        }
    }

    search->result = hwnd;
    return FALSE;
}

HWND ResolveTargetWindow(const Options& options) {
    if (options.targetWindow != nullptr) {
        return IsWindow(options.targetWindow) ? options.targetWindow : nullptr;
    }
    if (options.targetProcessId != 0 || !options.targetTitle.empty()) {
        WindowSearch search{options.targetProcessId, options.targetTitle, nullptr};
        EnumWindows(&FindWindowCallback, reinterpret_cast<LPARAM>(&search));
        return search.result;
    }
    return GetForegroundWindow();
}

void EnablePerMonitorDpiAwareness() {
    using SetDpiAwarenessContext = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    const auto setAwareness = reinterpret_cast<SetDpiAwarenessContext>(
        GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (setAwareness != nullptr) {
        (void)setAwareness(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    } else {
        (void)SetProcessDPIAware();
    }
}

OverlaySnapshot MakeDemoSnapshot(UINT width, UINT height, double seconds) {
    OverlaySnapshot snapshot{};
    snapshot.Header.Magic = kOverlayMagic;
    snapshot.Header.Version = kOverlayVersion;
    snapshot.Header.HeaderSize = sizeof(SharedOverlayHeader);
    snapshot.Header.EntityStride = sizeof(EntityData);
    snapshot.Header.Capacity = static_cast<std::uint32_t>(kMaximumEntities);
    snapshot.Header.EntityCount = 3;
    snapshot.Header.Sequence = 2;

    const float pulse = 10.0f * static_cast<float>(std::sin(seconds * 2.0));
    const float centerX = static_cast<float>(width) * 0.5f;
    const float centerY = static_cast<float>(height) * 0.5f;
    snapshot.Entries[0] = EntityData{centerX - 120.0f,
                                      centerY - 100.0f,
                                      240.0f,
                                      200.0f,
                                      0.0f, 0.0f, 0.0f,
                                      static_cast<std::uint32_t>(
                                          CoordinateSpace::Screen),
                                      L"Primary region",
                                      0xff33cc66u,
                                      1};
    snapshot.Entries[1] = EntityData{centerX + 160.0f + pulse,
                                      centerY - 60.0f,
                                      140.0f,
                                      120.0f,
                                      0.0f, 0.0f, 0.0f,
                                      static_cast<std::uint32_t>(
                                          CoordinateSpace::Screen),
                                      L"Tracked item",
                                      0xe6ffb020u,
                                      1};
    snapshot.Entries[2] = EntityData{50.0f,
                                      70.0f,
                                      180.0f,
                                      90.0f,
                                      0.0f, 0.0f, 0.0f,
                                      static_cast<std::uint32_t>(
                                          CoordinateSpace::Screen),
                                      L"Debug sample",
                                      0xcc40a0ffu,
                                      1};
    return snapshot;
}

void RenderSnapshot(D3dRenderer& renderer,
                    const OverlaySnapshot& snapshot,
                    UINT width,
                    UINT height) {
    const std::uint32_t count = std::min<std::uint32_t>(
        snapshot.Header.EntityCount,
        static_cast<std::uint32_t>(kMaximumEntities));
    for (std::uint32_t i = 0; i < count; ++i) {
        const EntityData& entity = snapshot.Entries[i];
        if (entity.isVisible == 0 || !std::isfinite(entity.screenX) ||
            !std::isfinite(entity.screenY) || !std::isfinite(entity.width) ||
            !std::isfinite(entity.height) || entity.width <= 0.0f ||
            entity.height <= 0.0f) {
            continue;
        }

        float screenX = entity.screenX;
        float screenY = entity.screenY;
        if (entity.coordinateSpace ==
            static_cast<std::uint32_t>(CoordinateSpace::World)) {
            if ((snapshot.Header.SceneFlags & kSceneHasViewProjection) == 0 ||
                !ProjectWorldToScreen(snapshot.Header.ViewProjection,
                                      entity.worldX,
                                      entity.worldY,
                                      entity.worldZ,
                                      static_cast<float>(width),
                                      static_cast<float>(height),
                                      screenX,
                                      screenY)) {
                continue;
            }
        } else if (entity.coordinateSpace !=
                   static_cast<std::uint32_t>(CoordinateSpace::Screen)) {
            continue;
        }

        renderer.DrawBox(screenX,
                         screenY,
                         entity.width,
                         entity.height,
                         entity.color);
        renderer.DrawLine(static_cast<float>(width) * 0.5f,
                          static_cast<float>(height),
                          screenX + entity.width * 0.5f,
                          screenY + entity.height,
                          entity.color);
        renderer.DrawText(screenX,
                          std::max(0.0f, screenY - 22.0f),
                          entity.name,
                          entity.color);
    }
}

} // namespace
} // namespace hypersinv::overlay

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    using namespace hypersinv::overlay;

    EnablePerMonitorDpiAwareness();
    Options options;
    if (!ParseOptions(options)) {
        return 2;
    }

    SharedMemoryReader reader;
    OverlaySnapshot snapshot{};
    if (!options.demo) {
        (void)reader.Open(options.mappingName);
        if (reader.IsOpen()) {
            (void)reader.Read(snapshot);
        }
    }

    HWND target = ResolveTargetWindow(options);
    if (!options.targetExplicit && snapshot.Header.TargetWindow != 0) {
        const HWND publishedTarget = reinterpret_cast<HWND>(
            static_cast<std::uintptr_t>(snapshot.Header.TargetWindow));
        if (IsWindow(publishedTarget)) {
            target = publishedTarget;
        }
    }
    if (target == nullptr) {
        MessageBoxW(nullptr,
                    L"The target window could not be found.",
                    L"Hypersinv overlay",
                    MB_OK | MB_ICONERROR);
        return 3;
    }

    RECT targetRect{};
    if (!GetClientRect(target, &targetRect)) {
        return 4;
    }
    UINT width = static_cast<UINT>(std::max<LONG>(1, targetRect.right));
    UINT height = static_cast<UINT>(std::max<LONG>(1, targetRect.bottom));

    OverlayWindow window;
    if (!window.Create(static_cast<int>(width), static_cast<int>(height))) {
        return 5;
    }
    if (!window.SyncToWindow(target, width, height)) {
        return 6;
    }

    D3dRenderer renderer;
    if (!renderer.Initialize(window.Handle(), width, height)) {
        return 7;
    }

    const auto start = std::chrono::steady_clock::now();
    constexpr auto frameInterval = std::chrono::microseconds(16667);
    auto nextFrame = start;
    auto nextMappingAttempt = start;
    MSG message{};
    bool running = true;
    while (running) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!running) {
            break;
        }

        if (!IsWindow(target)) {
            break;
        }

        const UINT previousWidth = width;
        const UINT previousHeight = height;
        if (!window.SyncToWindow(target, width, height)) {
            ShowWindow(window.Handle(), SW_HIDE);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        if (width != previousWidth || height != previousHeight) {
            if (!renderer.Resize(width, height)) {
                break;
            }
        }

        const auto now = std::chrono::steady_clock::now();
        if (options.demo) {
            const double seconds = std::chrono::duration<double>(now - start).count();
            snapshot = MakeDemoSnapshot(width, height, seconds);
        } else {
            if (!reader.IsOpen() && now >= nextMappingAttempt) {
                (void)reader.Open(options.mappingName);
                nextMappingAttempt = now + std::chrono::seconds(1);
            }
            if (!reader.IsOpen() ||
                reader.Read(snapshot) != SnapshotReadStatus::Success) {
                snapshot = OverlaySnapshot{};
            } else if (!options.targetExplicit &&
                       snapshot.Header.TargetWindow != 0) {
                const HWND publishedTarget = reinterpret_cast<HWND>(
                    static_cast<std::uintptr_t>(snapshot.Header.TargetWindow));
                if (IsWindow(publishedTarget)) {
                    target = publishedTarget;
                }
            }
        }

        if (!renderer.BeginFrame()) {
            break;
        }
        RenderSnapshot(renderer, snapshot, width, height);
        if (!renderer.EndFrame()) {
            break;
        }

        nextFrame += frameInterval;
        const auto frameComplete = std::chrono::steady_clock::now();
        if (nextFrame > frameComplete) {
            std::this_thread::sleep_until(nextFrame);
        } else {
            nextFrame = frameComplete;
        }
    }

    renderer.Shutdown();
    window.Destroy();
    return 0;
}
