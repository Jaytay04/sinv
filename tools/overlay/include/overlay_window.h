#pragma once

#include <windows.h>

#include <string>

namespace hypersinv::overlay {

class OverlayWindow final {
public:
    OverlayWindow() = default;
    ~OverlayWindow();

    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    bool Create(int width, int height, bool showImmediately = true) noexcept;
    void Destroy() noexcept;
    bool SyncToWindow(HWND target, UINT& width, UINT& height) noexcept;

    [[nodiscard]] HWND Handle() const noexcept { return hwnd_; }

private:
    static LRESULT CALLBACK WindowProcedure(HWND hwnd,
                                            UINT message,
                                            WPARAM wParam,
                                            LPARAM lParam) noexcept;
    static bool GenerateRandomClassName(wchar_t* output,
                                        std::size_t outputCount) noexcept;

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    std::wstring className_;
};

} // namespace hypersinv::overlay
