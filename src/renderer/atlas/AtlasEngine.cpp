// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "AtlasEngine.h"

#include <dxgidebug.h>
#include "../../interactivity/win32/CustomWindowMessages.h"

#include "shader_vs.h"
#include "shader_ps.h"

#pragma warning(disable : 4100)

template<typename T, typename... Args>
constexpr bool any_negative(T arg, Args... args)
{
    static_assert(std::is_signed_v<T>);
    static_assert(std::conjunction_v<std::is_same<T, Args>...>);
    return (arg | (args | ...)) & (T(1) << std::numeric_limits<T>::digits);
}

#define getLocaleName(varName)               \
    wchar_t varName[LOCALE_NAME_MAX_LENGTH]; \
    _getLocaleName(varName);

static void _getLocaleName(wchar_t* localeName)
{
    if (!GetUserDefaultLocaleName(localeName, LOCALE_NAME_MAX_LENGTH))
    {
        static constexpr wchar_t fallback[] = L"en-US";
        memcpy(localeName, fallback, sizeof(fallback));
    }
    // GetUserDefaultLocaleName can return bullshit locales with trailing underscores. Strip it off.
    // See: https://docs.microsoft.com/en-us/windows/win32/intl/locale-names
    else if (auto p = wcschr(localeName, L'_'))
    {
        *p = L'\0';
    }
}

using namespace Microsoft::Console::Render;

AtlasEngine::AtlasEngine()
{
    THROW_IF_FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, _uuidof(_sr.d2dFactory), _sr.d2dFactory.put_void()));
    THROW_IF_FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(_sr.dwriteFactory), _sr.dwriteFactory.put_unknown()));
    _sr.isWindows10OrGreater = IsWindows10OrGreater();
}

#pragma region IRenderEngine

[[nodiscard]] HRESULT AtlasEngine::StartPaint() noexcept
try
{
    if (_api.hwnd)
    {
        RECT rect;
        LOG_IF_WIN32_BOOL_FALSE(GetClientRect(_api.hwnd, &rect));
        (void)SetWindowSize({ rect.right - rect.left, rect.bottom - rect.top });

        if (WI_IsFlagSet(_invalidations, invalidation_flags::title))
        {
            LOG_IF_WIN32_BOOL_FALSE(PostMessageW(_api.hwnd, CM_UPDATE_TITLE, 0, 0));
            WI_ClearFlag(_invalidations, invalidation_flags::title);
        }
    }

    if (_invalidations != invalidation_flags::none)
    {
        if (WI_IsFlagSet(_invalidations, invalidation_flags::device))
        {
            _createResources();
            WI_ClearFlag(_invalidations, invalidation_flags::device);
        }
        if (WI_IsFlagSet(_invalidations, invalidation_flags::size))
        {
            _recreateSizeDependentResources();
            WI_ClearFlag(_invalidations, invalidation_flags::size);
        }
        if (WI_IsFlagSet(_invalidations, invalidation_flags::font))
        {
            _recreateFontDependentResources();
            WI_ClearFlag(_invalidations, invalidation_flags::font);
        }
    }

    _rapi.currentForegroundColor = 0;
    _rapi.currentBackgroundColor = 0;
    return S_OK;
}
catch (const wil::ResultException& exception)
{
    return _handleException(exception);
}
CATCH_RETURN()

[[nodiscard]] HRESULT AtlasEngine::EndPaint() noexcept
{
    return S_OK;
}

[[nodiscard]] bool AtlasEngine::RequiresContinuousRedraw() noexcept
{
    return false;
}

void AtlasEngine::WaitUntilCanRender() noexcept
{
    if (_r.frameLatencyWaitableObject)
    {
        WaitForSingleObjectEx(_r.frameLatencyWaitableObject.get(), 1000, true);
    }
    else
    {
        Sleep(8);
    }
}

[[nodiscard]] HRESULT AtlasEngine::Present() noexcept
try
{
    {
#pragma warning(suppress : 26494) // Variable 'mapped' is uninitialized. Always initialize an object (type.5).
        D3D11_MAPPED_SUBRESOURCE mapped;
        THROW_IF_FAILED(_r.deviceContext->Map(_r.cellBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
        assert(mapped.RowPitch >= _r.cells.size() * sizeof(cell));
        memcpy(mapped.pData, _r.cells.data(), _r.cells.size() * sizeof(cell));
        _r.deviceContext->Unmap(_r.cellBuffer.get(), 0);
    }

    // After Present calls, the back buffer needs to explicitly be
    // re-bound to the D3D11 immediate context before it can be used again.
    _r.deviceContext->OMSetRenderTargets(1, _r.renderTargetView.addressof(), nullptr);
    _r.deviceContext->Draw(3, 0);

    THROW_IF_FAILED(_r.swapChain->Present(1, 0));

    // On some GPUs with tile based deferred rendering (TBDR) architectures, binding
    // RenderTargets that already have contents in them (from previous rendering) incurs a
    // cost for having to copy the RenderTarget contents back into tile memory for rendering.
    //
    // On Windows 10 with DXGI_SWAP_EFFECT_FLIP_DISCARD we get this for free.
    if (!_sr.isWindows10OrGreater)
    {
        _r.deviceContext->DiscardView(_r.renderTargetView.get());
    }

    return S_OK;
}
catch (const wil::ResultException& exception)
{
    return _handleException(exception);
}
CATCH_RETURN()

[[nodiscard]] HRESULT AtlasEngine::PrepareForTeardown(_Out_ bool* const pForcePaint) noexcept
{
    RETURN_HR_IF_NULL(E_INVALIDARG, pForcePaint);
    *pForcePaint = false;
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::ScrollFrame() noexcept
{
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::Invalidate(const SMALL_RECT* const psrRegion) noexcept
{
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::InvalidateCursor(const SMALL_RECT* const psrRegion) noexcept
{
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::InvalidateSystem(const RECT* const prcDirtyClient) noexcept
{
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::InvalidateSelection(const std::vector<SMALL_RECT>& rectangles) noexcept
{
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::InvalidateScroll(const COORD* const pcoordDelta) noexcept
{
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::InvalidateAll() noexcept
{
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::InvalidateCircling(_Out_ bool* const pForcePaint) noexcept
{
    RETURN_HR_IF_NULL(E_INVALIDARG, pForcePaint);
    *pForcePaint = false;
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::InvalidateTitle() noexcept
{
    WI_SetFlag(_invalidations, invalidation_flags::title);
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::PrepareRenderInfo(const RenderFrameInfo& info) noexcept
{
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::ResetLineTransform() noexcept
{
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::PrepareLineTransform(const LineRendition lineRendition, const size_t targetRow, const size_t viewportLeft) noexcept
{
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::PaintBackground() noexcept
{
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::PaintBufferLine(gsl::span<const Cluster> const clusters, const COORD coord, const bool fTrimLeft, const bool lineWrapped) noexcept
{
    const auto cchLine = clusters.size();
    RETURN_HR_IF(S_OK, !cchLine);

    auto data = _r.cells.data() + static_cast<size_t>(_api.cellCount.x) * coord.Y + coord.X;
    for (const auto& cluster : clusters)
    {
        auto ch = cluster.GetTextAsSingle();
        if (ch >= 256)
        {
            ch = L' ';
        }

        data->glyphIndex = ch * _api.cellSize.x;
        data->color.x = _rapi.currentForegroundColor;
        data->color.y = _rapi.currentBackgroundColor;
#pragma warning(suppress : 26481) // Don't use pointer arithmetic. Use span instead (bounds.1).
        ++data;
    }

    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::PaintBufferGridLines(const GridLines lines, const COLORREF color, const size_t cchLine, const COORD coordTarget) noexcept
{
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::PaintSelection(const SMALL_RECT rect) noexcept
{
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::PaintCursor(const CursorOptions& options) noexcept
{
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::UpdateDrawingBrushes(const TextAttribute& textAttributes, const gsl::not_null<IRenderData*> pData, const bool isSettingDefaultBrushes) noexcept
{
    const auto [fg, bg] = pData->GetAttributeColors(textAttributes);
    _rapi.currentForegroundColor = fg;
    _rapi.currentBackgroundColor = bg;
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::UpdateFont(const FontInfoDesired& fontInfoDesired, _Out_ FontInfo& fontInfo) noexcept
{
    return UpdateFont(fontInfoDesired, fontInfo, {}, {});
}

[[nodiscard]] HRESULT AtlasEngine::UpdateDpi(const int dpi) noexcept
try
{
    _api.dpi = dpi;
    return S_OK;
}
CATCH_RETURN()

[[nodiscard]] HRESULT AtlasEngine::UpdateViewport(const SMALL_RECT srNewViewport) noexcept
{
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::GetProposedFont(const FontInfoDesired& fontInfoDesired, _Out_ FontInfo& fontInfo, const int dpi) noexcept
try
{
    getLocaleName(localeName);
    const auto textFormat = _createTextFormat(std::wstring{ fontInfoDesired.GetFaceName() }.c_str(), DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, fontInfoDesired.GetEngineSize().Y, localeName);

    wil::com_ptr<IDWriteTextLayout> textLayout;
    THROW_IF_FAILED(_sr.dwriteFactory->CreateTextLayout(L"█", 1, textFormat.get(), FLT_MAX, FLT_MAX, textLayout.put()));

    DWRITE_TEXT_METRICS metrics;
    THROW_IF_FAILED(textLayout->GetMetrics(&metrics));

    fontInfo.SetFromEngine(
        fontInfoDesired.GetFaceName(),
        fontInfoDesired.GetFamily(),
        fontInfoDesired.GetWeight(),
        false,
        COORD{
            gsl::narrow_cast<SHORT>(std::ceil(metrics.width)),
            gsl::narrow_cast<SHORT>(std::ceil(metrics.height)),
        },
        fontInfoDesired.GetEngineSize());
    return S_OK;
}
CATCH_RETURN()

[[nodiscard]] HRESULT AtlasEngine::GetDirtyArea(gsl::span<const til::rectangle>& area) noexcept
{
    _rapi.dirtyArea = til::rectangle{ 0, 0, gsl::narrow_cast<int>(_api.cellCount.x), gsl::narrow_cast<int>(_api.cellCount.y) };
    area = gsl::span{ &_rapi.dirtyArea, 1 };
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::GetFontSize(_Out_ COORD* const pFontSize) noexcept
{
    RETURN_HR_IF_NULL(E_INVALIDARG, pFontSize);
    pFontSize->X = gsl::narrow_cast<SHORT>(_api.cellSize.x);
    pFontSize->Y = gsl::narrow_cast<SHORT>(_api.cellSize.y);
    return S_OK;
}

[[nodiscard]] HRESULT AtlasEngine::IsGlyphWideByFont(const std::wstring_view& glyph, _Out_ bool* const pResult) noexcept
{
    *pResult = false;
    return S_FALSE;
}

[[nodiscard]] HRESULT AtlasEngine::UpdateTitle(const std::wstring_view newTitle) noexcept
{
    return S_OK;
}

#pragma endregion

#pragma region DxRenderer

[[nodiscard]] bool AtlasEngine::GetRetroTerminalEffect() const noexcept
{
    return false;
}

[[nodiscard]] float AtlasEngine::GetScaling() const noexcept
{
    return _api.dpi / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
}

[[nodiscard]] HANDLE AtlasEngine::GetSwapChainHandle()
{
    if (!_r.device)
    {
        _createResources();
    }
    return _r.swapChainHandle.get();
}

[[nodiscard]] ::Microsoft::Console::Types::Viewport AtlasEngine::GetViewportInCharacters(const ::Microsoft::Console::Types::Viewport& viewInPixels) const noexcept
{
    return ::Microsoft::Console::Types::Viewport::FromDimensions(viewInPixels.Origin(), COORD{ gsl::narrow<short>(viewInPixels.Width() / _api.cellSize.x), gsl::narrow<short>(viewInPixels.Height() / _api.cellSize.y) });
}

[[nodiscard]] ::Microsoft::Console::Types::Viewport AtlasEngine::GetViewportInPixels(const ::Microsoft::Console::Types::Viewport& viewInCharacters) const noexcept
{
    return ::Microsoft::Console::Types::Viewport::FromDimensions(viewInCharacters.Origin(), COORD{ gsl::narrow<short>(viewInCharacters.Width() * _api.cellSize.x), gsl::narrow<short>(viewInCharacters.Height() * _api.cellSize.y) });
}

void AtlasEngine::SetAntialiasingMode(const D2D1_TEXT_ANTIALIAS_MODE antialiasingMode) noexcept
{
    _api.antialiasingMode = antialiasingMode;
    WI_SetFlag(_invalidations, invalidation_flags::font);
}

void AtlasEngine::SetCallback(std::function<void()> pfn)
{
    _api.swapChainChangedCallback = std::move(pfn);
}

void AtlasEngine::SetDefaultTextBackgroundOpacity(const float opacity) noexcept
{
}

void AtlasEngine::SetForceFullRepaintRendering(bool enable) noexcept
{
}

[[nodiscard]] HRESULT AtlasEngine::SetHwnd(const HWND hwnd) noexcept
{
    _api.hwnd = hwnd;
    return S_OK;
}

void AtlasEngine::SetPixelShaderPath(std::wstring_view value) noexcept
{
}

void AtlasEngine::SetRetroTerminalEffect(bool enable) noexcept
{
}

void AtlasEngine::SetSelectionBackground(const COLORREF color, const float alpha) noexcept
{
}

void AtlasEngine::SetSoftwareRendering(bool enable) noexcept
{
}

void AtlasEngine::SetWarningCallback(std::function<void(const HRESULT)> pfn)
{
}

[[nodiscard]] HRESULT AtlasEngine::SetWindowSize(const SIZE pixels) noexcept
{
    FAIL_FAST_IF(any_negative(pixels.cx, pixels.cy));

    if (u32x2 newSize{ static_cast<u32>(pixels.cx), static_cast<u32>(pixels.cy) }; _api.sizeInPixel != newSize)
    {
        _api.sizeInPixel = newSize;
        _api.cellCount = _api.sizeInPixel / _api.cellSize;
        WI_SetFlag(_invalidations, invalidation_flags::size);
    }

    return S_OK;
}

void AtlasEngine::ToggleShaderEffects()
{
}

[[nodiscard]] HRESULT AtlasEngine::UpdateFont(const FontInfoDesired& fontInfoDesired, FontInfo& fontInfo, const std::unordered_map<std::wstring_view, uint32_t>& features, const std::unordered_map<std::wstring_view, float>& axes) noexcept
{
    RETURN_IF_FAILED(GetProposedFont(fontInfoDesired, fontInfo, _api.dpi));

    _api.fontSize = fontInfoDesired.GetEngineSize().Y;
    _api.fontName = fontInfo.GetFaceName();
    WI_SetFlag(_invalidations, invalidation_flags::font);

    if (u32x2 newSize{ static_cast<u32>(fontInfo.GetSize().X), static_cast<u32>(fontInfo.GetSize().Y) }; _api.cellSize != newSize)
    {
        _api.cellSize = newSize;
        _api.cellCount = _api.sizeInPixel / _api.cellSize;
        WI_SetFlag(_invalidations, invalidation_flags::size);
    }

    return S_OK;
}

void AtlasEngine::UpdateHyperlinkHoveredId(const uint16_t hoveredId) noexcept
{
}

#pragma endregion

[[nodiscard]] HRESULT AtlasEngine::_handleException(const wil::ResultException& exception) noexcept
{
    const auto hr = exception.GetErrorCode();
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == D2DERR_RECREATE_TARGET)
    {
        _r = {};
        WI_SetFlag(_invalidations, invalidation_flags::device);
        return E_PENDING; // Indicate a retry to the renderer
    }
    return hr;
}

void AtlasEngine::_createResources()
{
    FAIL_FAST_IF(_api.sizeInPixel.x == 0 || _api.sizeInPixel.y == 0);
    assert(!_r.device);

#ifdef NDEBUG
    static constexpr
#endif
        auto deviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_SINGLETHREADED;

#ifndef NDEBUG
    // DXGI debug messages + enabling D3D11_CREATE_DEVICE_DEBUG if the Windows SDK was installed.
    if (wil::unique_hmodule module{ LoadLibraryExW(L"dxgidebug.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32) })
    {
        deviceFlags |= D3D11_CREATE_DEVICE_DEBUG;

        const auto DXGIGetDebugInterface = reinterpret_cast<HRESULT(WINAPI*)(REFIID, void**)>(GetProcAddress(module.get(), "DXGIGetDebugInterface"));
        THROW_LAST_ERROR_IF(!DXGIGetDebugInterface);

        wil::com_ptr<IDXGIInfoQueue> infoQueue;
        if (SUCCEEDED(DXGIGetDebugInterface(IID_PPV_ARGS(infoQueue.addressof()))))
        {
            // I didn't want to link with dxguid.lib just for getting DXGI_DEBUG_ALL.
            static constexpr GUID dxgiDebugAll = { 0xe48ae283, 0xda80, 0x490b, { 0x87, 0xe6, 0x43, 0xe9, 0xa9, 0xcf, 0xda, 0x8 } };
            for (auto severity : std::array{ DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_WARNING })
            {
                infoQueue->SetBreakOnSeverity(dxgiDebugAll, severity, true);
            }
        }
    }
#endif // NDEBUG

    // D3D device setup (basically a D3D class factory)
    {
        wil::com_ptr<ID3D11DeviceContext> deviceContext;

        static constexpr std::array driverTypes{
            D3D_DRIVER_TYPE_HARDWARE,
            D3D_DRIVER_TYPE_WARP,
        };
        static constexpr std::array featureLevels{
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_11_1,
        };

        HRESULT hr = S_OK;
        for (auto driverType : driverTypes)
        {
            hr = D3D11CreateDevice(
                /* pAdapter */ nullptr,
                /* DriverType */ driverType,
                /* Software */ nullptr,
                /* Flags */ deviceFlags,
                /* pFeatureLevels */ featureLevels.data(),
                /* FeatureLevels */ gsl::narrow_cast<UINT>(featureLevels.size()),
                /* SDKVersion */ D3D11_SDK_VERSION,
                /* ppDevice */ _r.device.put(),
                /* pFeatureLevel */ nullptr,
                /* ppImmediateContext */ deviceContext.put());
            if (SUCCEEDED(hr))
            {
                break;
            }
        }
        THROW_IF_FAILED(hr);

        _r.deviceContext = deviceContext.query<ID3D11DeviceContext1>();
    }

#ifndef NDEBUG
    // D3D debug messages
    if (deviceFlags & D3D11_CREATE_DEVICE_DEBUG)
    {
        const auto infoQueue = _r.device.query<ID3D11InfoQueue>();
        for (auto severity : std::array{ D3D11_MESSAGE_SEVERITY_CORRUPTION, D3D11_MESSAGE_SEVERITY_ERROR, D3D11_MESSAGE_SEVERITY_WARNING })
        {
            infoQueue->SetBreakOnSeverity(severity, true);
        }
    }
#endif // NDEBUG

    // D3D swap chain setup (the thing that allows us to present frames on the screen)
    {
        const auto supportsFrameLatencyWaitableObject = IsWindows8Point1OrGreater();

        // With C++20 we'll finally have designated initializers.
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
        swapChainDesc.Width = _api.sizeInPixel.x;
        swapChainDesc.Height = _api.sizeInPixel.y;
        swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferCount = 2;
        swapChainDesc.Scaling = DXGI_SCALING_NONE;
        swapChainDesc.SwapEffect = _sr.isWindows10OrGreater ? DXGI_SWAP_EFFECT_FLIP_DISCARD : DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        swapChainDesc.Flags = supportsFrameLatencyWaitableObject ? DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT : 0;

        wil::com_ptr<IDXGIFactory2> dxgiFactory;
        THROW_IF_FAILED(CreateDXGIFactory1(IID_PPV_ARGS(dxgiFactory.put())));

        if (_api.hwnd)
        {
            if (FAILED(dxgiFactory->CreateSwapChainForHwnd(_r.device.get(), _api.hwnd, &swapChainDesc, nullptr, nullptr, _r.swapChain.put())))
            {
                swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
                THROW_IF_FAILED(dxgiFactory->CreateSwapChainForHwnd(_r.device.get(), _api.hwnd, &swapChainDesc, nullptr, nullptr, _r.swapChain.put()));
            }
        }
        else
        {
            // We can't link with dcomp.lib, as dcomp.dll doesn't exist on Windows 7.
            wil::unique_hmodule module{ LoadLibraryExW(L"dcomp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32) };
            THROW_LAST_ERROR_IF(!module);

            const auto DCompositionCreateSurfaceHandle = reinterpret_cast<HRESULT(WINAPI*)(DWORD, SECURITY_ATTRIBUTES*, HANDLE*)>(GetProcAddress(module.get(), "DCompositionCreateSurfaceHandle"));
            THROW_LAST_ERROR_IF(!DCompositionCreateSurfaceHandle);

            // As per: https://docs.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-dcompositioncreatesurfacehandle
            static constexpr DWORD COMPOSITIONSURFACE_ALL_ACCESS = 0x0003L;
            THROW_IF_FAILED(DCompositionCreateSurfaceHandle(COMPOSITIONSURFACE_ALL_ACCESS, nullptr, _r.swapChainHandle.put()));
            THROW_IF_FAILED(dxgiFactory.query<IDXGIFactoryMedia>()->CreateSwapChainForCompositionSurfaceHandle(_r.device.get(), _r.swapChainHandle.get(), &swapChainDesc, nullptr, _r.swapChain.put()));
        }

        if (supportsFrameLatencyWaitableObject)
        {
            _r.frameLatencyWaitableObject.reset(_r.swapChain.query<IDXGISwapChain2>()->GetFrameLatencyWaitableObject());
            THROW_LAST_ERROR_IF(!_r.frameLatencyWaitableObject);
        }
    }

    // Our constant buffer will never get resized
    {
        D3D11_BUFFER_DESC constantBufferDesc{};
        constantBufferDesc.ByteWidth = sizeof(const_buffer);
        constantBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        THROW_IF_FAILED(_r.device->CreateBuffer(&constantBufferDesc, nullptr, _r.constantBuffer.put()));
    }

    THROW_IF_FAILED(_r.device->CreateVertexShader(&shader_vs[0], sizeof(shader_vs), nullptr, _r.vertexShader.put()));
    THROW_IF_FAILED(_r.device->CreatePixelShader(&shader_ps[0], sizeof(shader_ps), nullptr, _r.pixelShader.put()));

    if (_api.swapChainChangedCallback)
    {
        try
        {
            _api.swapChainChangedCallback();
        }
        CATCH_LOG();
    }

    WI_SetAllFlags(_invalidations, invalidation_flags::size | invalidation_flags::font);
}

void AtlasEngine::_recreateSizeDependentResources()
{
    FAIL_FAST_IF(_api.sizeInPixel.x == 0 || _api.sizeInPixel.y == 0);

    // ResizeBuffer() docs:
    //   Before you call ResizeBuffers, ensure that the application releases all references [...].
    //   You can use ID3D11DeviceContext::ClearState to ensure that all [internal] references are released.
    _r.renderTargetView.reset();
    _r.deviceContext->ClearState();

    THROW_IF_FAILED(_r.swapChain->ResizeBuffers(0, _api.sizeInPixel.x, _api.sizeInPixel.y, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT));

    // The RenderTargetView is later used with OMSetRenderTargets
    // to tell D3D where stuff is supposed to be rendered at.
    {
        wil::com_ptr<ID3D11Texture2D> buffer;
        THROW_IF_FAILED(_r.swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), buffer.put_void()));
        THROW_IF_FAILED(_r.device->CreateRenderTargetView(buffer.get(), nullptr, _r.renderTargetView.put()));
    }

    // Tell D3D which parts of the render target will be visible.
    // Everything outside of the viewport will be black.
    //
    // In the future this should cover the entire _api.sizeInPixel.x/_api.sizeInPixel.y.
    // The pixel shader should draw the remaining content in the configured background color.
    {
        D3D11_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(_api.cellCount.x * _api.cellSize.x);
        viewport.Height = static_cast<float>(_api.cellCount.y * _api.cellSize.y);
        _r.deviceContext->RSSetViewports(1, &viewport);
    }

    {
        _r.deviceContext->VSSetShader(_r.vertexShader.get(), nullptr, 0);
        _r.deviceContext->PSSetShader(_r.pixelShader.get(), nullptr, 0);

        // Our vertex shader uses a trick from Bill Bilodeau published in
        // "Vertex Shader Tricks" at GDC14 to draw a fullscreen triangle
        // without vertex/index buffers. This prepares our context for this.
        _r.deviceContext->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
        _r.deviceContext->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
        _r.deviceContext->IASetInputLayout(nullptr);
        _r.deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        _r.deviceContext->PSSetConstantBuffers(0, 1, _r.constantBuffer.addressof());
    }

    if (const auto cellsSize = _api.cellCount.area<size_t>(); cellsSize != _r.cells.size())
    {
        // Our render loop heavily relies on memcpy() which is between 50 and 3000% faster
        // on Intel and AMD CPUs for allocations with an alignment of 32 or greater.
        _r.cells = aligned_buffer<cell>{ cellsSize, 32 };

        {
            D3D11_BUFFER_DESC cellBufferDesc{};
            cellBufferDesc.ByteWidth = _api.cellCount.x * _api.cellCount.y * sizeof(cell);
            cellBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
            cellBufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            cellBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            cellBufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            cellBufferDesc.StructureByteStride = sizeof(cell);
            THROW_IF_FAILED(_r.device->CreateBuffer(&cellBufferDesc, nullptr, _r.cellBuffer.put()));
            THROW_IF_FAILED(_r.device->CreateShaderResourceView(_r.cellBuffer.get(), nullptr, _r.cellView.put()));
        }

        {
            const_buffer data;
            data.cellSize.x = _api.cellSize.x;
            data.cellSize.y = _api.cellSize.y;
            data.cellCount.x = _api.cellCount.x;
            data.cellCount.y = _api.cellCount.y;
            _r.deviceContext->UpdateSubresource(_r.constantBuffer.get(), 0, nullptr, &data, 0, 0);
        }

        {
            const std::array resources{ _r.cellView.get(), _r.glyphView.get() };
            _r.deviceContext->PSSetShaderResources(0, gsl::narrow_cast<UINT>(resources.size()), resources.data());
        }

        _recreateDependentResourcesCommon();
    }
}

void AtlasEngine::_recreateFontDependentResources()
{
    {
        D3D11_TEXTURE2D_DESC glyphTextureDesc{};
        glyphTextureDesc.Width = 2048;
        glyphTextureDesc.Height = 2048;
        glyphTextureDesc.MipLevels = 1;
        glyphTextureDesc.ArraySize = 1;
        glyphTextureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        glyphTextureDesc.SampleDesc = { 1, 0 };
        glyphTextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        THROW_IF_FAILED(_r.device->CreateTexture2D(&glyphTextureDesc, nullptr, _r.glyphBuffer.put()));
        THROW_IF_FAILED(_r.device->CreateShaderResourceView(_r.glyphBuffer.get(), nullptr, _r.glyphView.put()));

        const auto data = calloc(static_cast<size_t>(glyphTextureDesc.Width) * glyphTextureDesc.Width, 4);
        _r.deviceContext->UpdateSubresource(_r.glyphBuffer.get(), 0, nullptr, data, 0, 0);
        free(data);
    }

    _recreateDependentResourcesCommon();

    // D2D resources
    {
        D2D1_RENDER_TARGET_PROPERTIES properties{};
        properties.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
        properties.pixelFormat = { DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED };
        properties.dpiX = static_cast<float>(_api.dpi);
        properties.dpiY = static_cast<float>(_api.dpi);
        const auto surface = _r.glyphBuffer.query<IDXGISurface>();
        THROW_IF_FAILED(_sr.d2dFactory->CreateDxgiSurfaceRenderTarget(surface.get(), &properties, _r.renderTarget.put()));
        _r.renderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        _r.renderTarget->SetTextAntialiasMode(_api.antialiasingMode);
    }
    {
        D2D1_COLOR_F color{ 1, 1, 1, 1 };
        wil::com_ptr<ID2D1SolidColorBrush> brush;
        THROW_IF_FAILED(_r.renderTarget->CreateSolidColorBrush(&color, nullptr, brush.put()));
        _r.brush = brush.query<ID2D1Brush>();
    }
    {
        getLocaleName(localeName);
        for (auto style = 0; style < 2; ++style)
        {
            for (auto weight = 0; weight < 2; ++weight)
            {
                _r.textFormats[weight][style] = _createTextFormat(
                    _api.fontName.c_str(),
                    weight ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                    style ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
                    static_cast<float>(_api.fontSize),
                    localeName);
            }
        }
    }
    {
        _r.renderTarget->BeginDraw();
        for (wchar_t ch = 0; ch < 128; ch++)
        {
            D2D1_RECT_F rect;
            rect.left = static_cast<float>(ch * _api.cellSize.x);
            rect.top = 0.0f;
            rect.right = static_cast<float>((ch + 1) * _api.cellSize.x);
            rect.bottom = static_cast<float>(_api.cellSize.y);
            _r.renderTarget->DrawTextW(&ch, 1, _getTextFormat(false, false), &rect, _r.brush.get(), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT, DWRITE_MEASURING_MODE_NATURAL);
        }
        _r.renderTarget->EndDraw();
    }
}

void AtlasEngine::_recreateDependentResourcesCommon()
{
    {
        const_buffer data;
        data.cellSize = _api.cellSize;
        data.cellCount = _api.cellCount;
        _r.deviceContext->UpdateSubresource(_r.constantBuffer.get(), 0, nullptr, &data, 0, 0);
    }

    {
        const std::array resources{ _r.cellView.get(), _r.glyphView.get() };
        _r.deviceContext->PSSetShaderResources(0, gsl::narrow_cast<UINT>(resources.size()), resources.data());
    }
}

void AtlasEngine::_generateGlyph()
{
}
