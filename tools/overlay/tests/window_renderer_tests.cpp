#include "d3d_renderer.h"
#include "overlay_window.h"

#include <windows.h>

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
    if (!renderer.Initialize(window.Handle(), 320, 240) ||
        !renderer.BeginFrame()) {
        return Fail("D3D11/Direct2D initialization");
    }
    renderer.DrawBox(10.0f, 10.0f, 100.0f, 80.0f, 0xcc40a0ffu);
    renderer.DrawLine(0.0f, 0.0f, 319.0f, 239.0f, 0xff33cc66u);
    renderer.DrawText(12.0f, 12.0f, L"render-test", 0xffffffffu);
    if (!renderer.EndFrame()) {
        return Fail("D3D11 present");
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

    std::puts("PASS: overlay window styles and DirectX render path");
    return 0;
}
