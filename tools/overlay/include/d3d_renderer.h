#pragma once

#include <windows.h>

#include <d2d1.h>
#include <d3d11.h>
#include <dwrite.h>
#include <dxgi.h>
#include <wrl/client.h>

namespace hypersinv::overlay {

class D3dRenderer final {
public:
    bool Initialize(HWND hwnd, UINT width, UINT height) noexcept;
    bool Resize(UINT width, UINT height) noexcept;
    void Shutdown() noexcept;

    bool BeginFrame() noexcept;
    bool EndFrame() noexcept;

    void DrawBox(float x,
                 float y,
                 float width,
                 float height,
                 DWORD color) noexcept;
    void DrawLine(float x1,
                  float y1,
                  float x2,
                  float y2,
                  DWORD color) noexcept;
    void DrawText(float x,
                  float y,
                  const wchar_t* text,
                  DWORD color) noexcept;

private:
    bool CreateDeviceAndSwapChain() noexcept;
    bool CreateSizeDependentResources() noexcept;
    D2D1_COLOR_F DecodeColor(DWORD color) const noexcept;

    HWND hwnd_ = nullptr;
    UINT width_ = 0;
    UINT height_ = 0;
    bool drawing_ = false;

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTargetView_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blendState_;
    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory_;
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> d2dTarget_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    Microsoft::WRL::ComPtr<IDWriteFactory> writeFactory_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat_;
};

} // namespace hypersinv::overlay
