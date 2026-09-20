#include "overlay_window.h"

#include <bcrypt.h>
#include <dwmapi.h>

#include <array>
#include <cstdint>

namespace hypersinv::overlay {

OverlayWindow::~OverlayWindow() {
    Destroy();
}

bool OverlayWindow::GenerateRandomClassName(wchar_t* output,
                                            std::size_t outputCount) noexcept {
    if (output == nullptr || outputCount < 16) {
        return false;
    }

    std::array<std::uint8_t, 20> entropy{};
    if (BCryptGenRandom(nullptr,
                       entropy.data(),
                       static_cast<ULONG>(entropy.size()),
                       BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
        return false;
    }

    constexpr wchar_t alphabet[] =
        L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    constexpr std::size_t alphabetLength = (sizeof(alphabet) / sizeof(wchar_t)) - 1;
    const std::size_t count = (outputCount - 1 < entropy.size())
                                  ? outputCount - 1
                                  : entropy.size();
    for (std::size_t i = 0; i < count; ++i) {
        output[i] = alphabet[entropy[i] % alphabetLength];
    }
    output[count] = L'\0';
    return true;
}

bool OverlayWindow::Create(int width, int height, bool showImmediately) noexcept {
    Destroy();
    if (width <= 0 || height <= 0) {
        return false;
    }

    wchar_t className[32]{};
    if (!GenerateRandomClassName(className, _countof(className))) {
        return false;
    }

    instance_ = GetModuleHandleW(nullptr);
    className_ = className;

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &OverlayWindow::WindowProcedure;
    windowClass.hInstance = instance_;
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    windowClass.lpszClassName = className_.c_str();
    if (RegisterClassExW(&windowClass) == 0) {
        className_.clear();
        return false;
    }

    constexpr DWORD extendedStyle = WS_EX_TOPMOST |
                                    WS_EX_TRANSPARENT |
                                    WS_EX_LAYERED |
                                    WS_EX_TOOLWINDOW |
                                    WS_EX_NOACTIVATE;
    hwnd_ = CreateWindowExW(extendedStyle,
                            className_.c_str(),
                            L"",
                            WS_POPUP,
                            0,
                            0,
                            width,
                            height,
                            nullptr,
                            nullptr,
                            instance_,
                            this);
    if (hwnd_ == nullptr) {
        UnregisterClassW(className_.c_str(), instance_);
        className_.clear();
        return false;
    }

    if (!SetLayeredWindowAttributes(hwnd_, RGB(0, 0, 0), 255, LWA_COLORKEY)) {
        Destroy();
        return false;
    }

    const MARGINS margins{-1, -1, -1, -1};
    (void)DwmExtendFrameIntoClientArea(hwnd_, &margins);
    const BOOL excludedFromPeek = TRUE;
    (void)DwmSetWindowAttribute(hwnd_,
                                DWMWA_EXCLUDED_FROM_PEEK,
                                &excludedFromPeek,
                                sizeof(excludedFromPeek));

    if (showImmediately) {
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        UpdateWindow(hwnd_);
    }
    return true;
}

void OverlayWindow::Destroy() noexcept {
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (!className_.empty() && instance_ != nullptr) {
        UnregisterClassW(className_.c_str(), instance_);
        className_.clear();
    }
    instance_ = nullptr;
}

bool OverlayWindow::SyncToWindow(HWND target, UINT& width, UINT& height) noexcept {
    if (hwnd_ == nullptr || target == nullptr || !IsWindow(target) ||
        IsIconic(target)) {
        return false;
    }

    RECT client{};
    if (!GetClientRect(target, &client)) {
        return false;
    }
    POINT topLeft{client.left, client.top};
    POINT bottomRight{client.right, client.bottom};
    if (!ClientToScreen(target, &topLeft) || !ClientToScreen(target, &bottomRight)) {
        return false;
    }

    const LONG pixelWidth = bottomRight.x - topLeft.x;
    const LONG pixelHeight = bottomRight.y - topLeft.y;
    if (pixelWidth <= 0 || pixelHeight <= 0) {
        return false;
    }

    if (!SetWindowPos(hwnd_,
                      HWND_TOPMOST,
                      topLeft.x,
                      topLeft.y,
                      pixelWidth,
                      pixelHeight,
                      SWP_NOACTIVATE | SWP_SHOWWINDOW)) {
        return false;
    }

    width = static_cast<UINT>(pixelWidth);
    height = static_cast<UINT>(pixelHeight);
    return true;
}

LRESULT CALLBACK OverlayWindow::WindowProcedure(HWND hwnd,
                                                UINT message,
                                                WPARAM wParam,
                                                LPARAM lParam) noexcept {
    switch (message) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

} // namespace hypersinv::overlay
