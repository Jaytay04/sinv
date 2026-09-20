#include "d3d_renderer.h"

#include <algorithm>
#include <cwchar>

namespace hypersinv::overlay {

bool D3dRenderer::Initialize(HWND hwnd, UINT width, UINT height) noexcept {
    Shutdown();
    if (hwnd == nullptr || width == 0 || height == 0) {
        return false;
    }

    hwnd_ = hwnd;
    width_ = width;
    height_ = height;

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                 d2dFactory_.GetAddressOf()))) {
        Shutdown();
        return false;
    }
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                   __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(
                                       writeFactory_.GetAddressOf())))) {
        Shutdown();
        return false;
    }
    if (FAILED(writeFactory_->CreateTextFormat(L"Segoe UI",
                                               nullptr,
                                               DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                               DWRITE_FONT_STYLE_NORMAL,
                                               DWRITE_FONT_STRETCH_NORMAL,
                                               16.0f,
                                               L"en-us",
                                               textFormat_.GetAddressOf()))) {
        Shutdown();
        return false;
    }
    textFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    if (!CreateDeviceAndSwapChain() || !CreateSizeDependentResources()) {
        Shutdown();
        return false;
    }
    return true;
}

bool D3dRenderer::CreateDeviceAndSwapChain() noexcept {
    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferCount = 1;
    description.BufferDesc.Width = width_;
    description.BufferDesc.Height = height_;
    description.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.BufferDesc.RefreshRate.Numerator = 60;
    description.BufferDesc.RefreshRate.Denominator = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.OutputWindow = hwnd_;
    description.SampleDesc.Count = 1;
    description.Windowed = TRUE;
    description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    constexpr UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL selected{};

    HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        levels,
        static_cast<UINT>(_countof(levels)),
        D3D11_SDK_VERSION,
        &description,
        swapChain_.GetAddressOf(),
        device_.GetAddressOf(),
        &selected,
        context_.GetAddressOf());

    if (FAILED(result)) {
        result = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            flags,
            levels + 1,
            static_cast<UINT>(_countof(levels) - 1),
            D3D11_SDK_VERSION,
            &description,
            swapChain_.GetAddressOf(),
            device_.GetAddressOf(),
            &selected,
            context_.GetAddressOf());
    }
    if (FAILED(result)) {
        return false;
    }

    D3D11_BLEND_DESC blendDescription{};
    blendDescription.RenderTarget[0].BlendEnable = TRUE;
    blendDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDescription.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDescription.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDescription.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDescription.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDescription.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDescription.RenderTarget[0].RenderTargetWriteMask =
        D3D11_COLOR_WRITE_ENABLE_ALL;
    return SUCCEEDED(device_->CreateBlendState(&blendDescription,
                                                blendState_.GetAddressOf()));
}

bool D3dRenderer::CreateSizeDependentResources() noexcept {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(swapChain_->GetBuffer(0,
                                     __uuidof(ID3D11Texture2D),
                                     reinterpret_cast<void**>(
                                         backBuffer.GetAddressOf())))) {
        return false;
    }
    if (FAILED(device_->CreateRenderTargetView(backBuffer.Get(),
                                               nullptr,
                                               renderTargetView_.GetAddressOf()))) {
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGISurface> surface;
    if (FAILED(backBuffer.As(&surface))) {
        return false;
    }

    const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                          D2D1_ALPHA_MODE_PREMULTIPLIED),
        0.0f,
        0.0f,
        D2D1_RENDER_TARGET_USAGE_NONE,
        D2D1_FEATURE_LEVEL_DEFAULT);
    if (FAILED(d2dFactory_->CreateDxgiSurfaceRenderTarget(
            surface.Get(), properties, d2dTarget_.GetAddressOf()))) {
        return false;
    }
    return SUCCEEDED(d2dTarget_->CreateSolidColorBrush(
        D2D1::ColorF(D2D1::ColorF::White), brush_.GetAddressOf()));
}

bool D3dRenderer::Resize(UINT width, UINT height) noexcept {
    if (swapChain_ == nullptr || width == 0 || height == 0) {
        return false;
    }
    if (width == width_ && height == height_) {
        return true;
    }

    drawing_ = false;
    brush_.Reset();
    d2dTarget_.Reset();
    renderTargetView_.Reset();
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    if (FAILED(swapChain_->ResizeBuffers(0,
                                         width,
                                         height,
                                         DXGI_FORMAT_UNKNOWN,
                                         0))) {
        return false;
    }
    width_ = width;
    height_ = height;
    return CreateSizeDependentResources();
}

void D3dRenderer::Shutdown() noexcept {
    drawing_ = false;
    if (context_ != nullptr) {
        context_->ClearState();
        context_->Flush();
    }
    textFormat_.Reset();
    writeFactory_.Reset();
    brush_.Reset();
    d2dTarget_.Reset();
    d2dFactory_.Reset();
    blendState_.Reset();
    renderTargetView_.Reset();
    swapChain_.Reset();
    context_.Reset();
    device_.Reset();
    hwnd_ = nullptr;
    width_ = 0;
    height_ = 0;
}

bool D3dRenderer::BeginFrame() noexcept {
    if (drawing_ || context_ == nullptr || renderTargetView_ == nullptr ||
        d2dTarget_ == nullptr) {
        return false;
    }

    constexpr float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    context_->OMSetRenderTargets(1, renderTargetView_.GetAddressOf(), nullptr);
    context_->OMSetBlendState(blendState_.Get(), nullptr, 0xffffffffu);
    context_->ClearRenderTargetView(renderTargetView_.Get(), clearColor);
    d2dTarget_->BeginDraw();
    d2dTarget_->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    drawing_ = true;
    return true;
}

bool D3dRenderer::EndFrame() noexcept {
    if (!drawing_) {
        return false;
    }
    drawing_ = false;
    const HRESULT drawResult = d2dTarget_->EndDraw();
    if (drawResult == D2DERR_RECREATE_TARGET) {
        brush_.Reset();
        d2dTarget_.Reset();
        return CreateSizeDependentResources();
    }
    if (FAILED(drawResult)) {
        return false;
    }

    const HRESULT presentResult = swapChain_->Present(1, 0);
    return presentResult != DXGI_ERROR_DEVICE_REMOVED &&
           presentResult != DXGI_ERROR_DEVICE_RESET &&
           SUCCEEDED(presentResult);
}

D2D1_COLOR_F D3dRenderer::DecodeColor(DWORD color) const noexcept {
    constexpr float scale = 1.0f / 255.0f;
    return D2D1::ColorF(static_cast<float>((color >> 16u) & 0xffu) * scale,
                        static_cast<float>((color >> 8u) & 0xffu) * scale,
                        static_cast<float>(color & 0xffu) * scale,
                        static_cast<float>((color >> 24u) & 0xffu) * scale);
}

void D3dRenderer::DrawBox(float x,
                          float y,
                          float width,
                          float height,
                          DWORD color) noexcept {
    if (!drawing_ || brush_ == nullptr || width <= 0.0f || height <= 0.0f) {
        return;
    }
    brush_->SetColor(DecodeColor(color));
    d2dTarget_->DrawRectangle(D2D1::RectF(x, y, x + width, y + height),
                              brush_.Get(),
                              2.0f);
}

void D3dRenderer::DrawLine(float x1,
                           float y1,
                           float x2,
                           float y2,
                           DWORD color) noexcept {
    if (!drawing_ || brush_ == nullptr) {
        return;
    }
    brush_->SetColor(DecodeColor(color));
    d2dTarget_->DrawLine(D2D1::Point2F(x1, y1),
                         D2D1::Point2F(x2, y2),
                         brush_.Get(),
                         1.5f);
}

void D3dRenderer::DrawText(float x,
                           float y,
                           const wchar_t* text,
                           DWORD color) noexcept {
    if (!drawing_ || brush_ == nullptr || textFormat_ == nullptr ||
        text == nullptr || text[0] == L'\0') {
        return;
    }
    brush_->SetColor(DecodeColor(color));
    const UINT32 length = static_cast<UINT32>(wcsnlen_s(text, 32));
    d2dTarget_->DrawTextW(text,
                          length,
                          textFormat_.Get(),
                          D2D1::RectF(x,
                                      y,
                                      static_cast<float>(width_),
                                      std::min(static_cast<float>(height_), y + 48.0f)),
                          brush_.Get(),
                          D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

} // namespace hypersinv::overlay
