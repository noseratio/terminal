// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include <d2d1.h>
#include <d3d11_1.h>
#include <dwrite.h>
#include <dxgi.h>

#include "../../renderer/inc/IRenderEngine.hpp"

struct ID3D11Buffer;
struct ID3D11ComputeShader;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;
struct ID3D11UnorderedAccessView;
struct IDXGISwapChain2;

namespace Microsoft::Console::Render
{
    class AtlasEngine final : public IRenderEngine
    {
    public:
        explicit AtlasEngine();

        AtlasEngine(const AtlasEngine&) = default;
        AtlasEngine& operator=(const AtlasEngine&) = default;

        AtlasEngine(AtlasEngine&&) = default;
        AtlasEngine& operator=(AtlasEngine&&) = default;

        // IRenderEngine
        [[nodiscard]] HRESULT StartPaint() noexcept override;
        [[nodiscard]] HRESULT EndPaint() noexcept override;
        [[nodiscard]] bool RequiresContinuousRedraw() noexcept override;
        void WaitUntilCanRender() noexcept override;
        [[nodiscard]] HRESULT Present() noexcept override;
        [[nodiscard]] HRESULT PrepareForTeardown(_Out_ bool* const pForcePaint) noexcept override;
        [[nodiscard]] HRESULT ScrollFrame() noexcept override;
        [[nodiscard]] HRESULT Invalidate(const SMALL_RECT* const psrRegion) noexcept override;
        [[nodiscard]] HRESULT InvalidateCursor(const SMALL_RECT* const psrRegion) noexcept override;
        [[nodiscard]] HRESULT InvalidateSystem(const RECT* const prcDirtyClient) noexcept override;
        [[nodiscard]] HRESULT InvalidateSelection(const std::vector<SMALL_RECT>& rectangles) noexcept override;
        [[nodiscard]] HRESULT InvalidateScroll(const COORD* const pcoordDelta) noexcept override;
        [[nodiscard]] HRESULT InvalidateAll() noexcept override;
        [[nodiscard]] HRESULT InvalidateCircling(_Out_ bool* const pForcePaint) noexcept override;
        [[nodiscard]] HRESULT InvalidateTitle() noexcept override;
        [[nodiscard]] HRESULT PrepareRenderInfo(const RenderFrameInfo& info) noexcept override;
        [[nodiscard]] HRESULT ResetLineTransform() noexcept override;
        [[nodiscard]] HRESULT PrepareLineTransform(const LineRendition lineRendition, const size_t targetRow, const size_t viewportLeft) noexcept override;
        [[nodiscard]] HRESULT PaintBackground() noexcept override;
        [[nodiscard]] HRESULT PaintBufferLine(gsl::span<const Cluster> const clusters, const COORD coord, const bool fTrimLeft, const bool lineWrapped) noexcept override;
        [[nodiscard]] HRESULT PaintBufferGridLines(const GridLines lines, const COLORREF color, const size_t cchLine, const COORD coordTarget) noexcept override;
        [[nodiscard]] HRESULT PaintSelection(const SMALL_RECT rect) noexcept override;
        [[nodiscard]] HRESULT PaintCursor(const CursorOptions& options) noexcept override;
        [[nodiscard]] HRESULT UpdateDrawingBrushes(const TextAttribute& textAttributes, const gsl::not_null<IRenderData*> pData, const bool isSettingDefaultBrushes) noexcept override;
        [[nodiscard]] HRESULT UpdateFont(const FontInfoDesired& FontInfoDesired, _Out_ FontInfo& FontInfo) noexcept override;
        [[nodiscard]] HRESULT UpdateDpi(const int iDpi) noexcept override;
        [[nodiscard]] HRESULT UpdateViewport(const SMALL_RECT srNewViewport) noexcept override;
        [[nodiscard]] HRESULT GetProposedFont(const FontInfoDesired& FontInfoDesired, _Out_ FontInfo& FontInfo, const int iDpi) noexcept override;
        [[nodiscard]] HRESULT GetDirtyArea(gsl::span<const til::rectangle>& area) noexcept override;
        [[nodiscard]] HRESULT GetFontSize(_Out_ COORD* const pFontSize) noexcept override;
        [[nodiscard]] HRESULT IsGlyphWideByFont(const std::wstring_view& glyph, _Out_ bool* const pResult) noexcept override;
        [[nodiscard]] HRESULT UpdateTitle(const std::wstring_view newTitle) noexcept override;

        // DxRenderer - getter
        [[nodiscard]] bool GetRetroTerminalEffect() const noexcept;
        [[nodiscard]] float GetScaling() const noexcept;
        [[nodiscard]] HANDLE GetSwapChainHandle();
        [[nodiscard]] ::Microsoft::Console::Types::Viewport GetViewportInCharacters(const ::Microsoft::Console::Types::Viewport& viewInPixels) const noexcept;
        [[nodiscard]] ::Microsoft::Console::Types::Viewport GetViewportInPixels(const ::Microsoft::Console::Types::Viewport& viewInCharacters) const noexcept;
        // DxRenderer - setter
        void SetAntialiasingMode(const D2D1_TEXT_ANTIALIAS_MODE antialiasingMode) noexcept;
        void SetCallback(std::function<void()> pfn);
        void SetDefaultTextBackgroundOpacity(const float opacity) noexcept;
        void SetForceFullRepaintRendering(bool enable) noexcept;
        [[nodiscard]] HRESULT SetHwnd(const HWND hwnd) noexcept;
        void SetPixelShaderPath(std::wstring_view value) noexcept;
        void SetRetroTerminalEffect(bool enable) noexcept;
        void SetSelectionBackground(const COLORREF color, const float alpha = 0.5f) noexcept;
        void SetSoftwareRendering(bool enable) noexcept;
        void SetWarningCallback(std::function<void(const HRESULT)> pfn);
        [[nodiscard]] HRESULT SetWindowSize(const SIZE pixels) noexcept;
        void ToggleShaderEffects();
        [[nodiscard]] HRESULT UpdateFont(const FontInfoDesired& pfiFontInfoDesired, FontInfo& fiFontInfo, const std::unordered_map<std::wstring_view, uint32_t>& features, const std::unordered_map<std::wstring_view, float>& axes) noexcept;
        void UpdateHyperlinkHoveredId(const uint16_t hoveredId) noexcept;

    private:
        [[nodiscard]] HRESULT _handleException(const wil::ResultException& exception) noexcept;
        __declspec(noinline) void _createResources();
        __declspec(noinline) void _recreateSizeDependentResources();
        __declspec(noinline) void _recreateFontDependentResources();
        void _recreateDependentResourcesCommon();
        wil::com_ptr<IDWriteTextFormat> _createTextFormat(const wchar_t* fontFamilyName, DWRITE_FONT_WEIGHT fontWeight, DWRITE_FONT_STYLE fontStyle, float fontSize, const wchar_t* localeName) const
        {
            wil::com_ptr<IDWriteTextFormat> textFormat;
            THROW_IF_FAILED(_sr.dwriteFactory->CreateTextFormat(fontFamilyName, nullptr, fontWeight, fontStyle, DWRITE_FONT_STRETCH_NORMAL, fontSize, localeName, textFormat.addressof()));
            return textFormat;
        }
        void _generateGlyph();

        inline IDWriteTextFormat* _getTextFormat(bool bold, bool italic) {
            return _r.textFormats[bold][italic].get();
        }

        template<typename T>
        struct aligned_buffer
        {
            aligned_buffer() = default;

            explicit aligned_buffer(size_t size, size_t alignment) :
                _data{ THROW_IF_NULL_ALLOC(static_cast<T*>(_aligned_malloc(size * sizeof(T), alignment))) },
                _size{ size }
            {
            }

            ~aligned_buffer()
            {
                _aligned_free(_data);
            }

            aligned_buffer(aligned_buffer&& other) noexcept :
                _data{ std::exchange(other._data, nullptr) },
                _size{ std::exchange(other._size, 0) }
            {
            }

            aligned_buffer& operator=(aligned_buffer&& other) noexcept
            {
                _aligned_free(_data);
                _data = std::exchange(other._data, nullptr);
                _size = std::exchange(other._size, 0);
                return *this;
            }

            inline T* data()
            {
                return _data;
            }

            inline size_t size()
            {
                return _size;
            }

        private:
            T* _data = nullptr;
            size_t _size = 0;
        };

        template<typename T>
        struct vec2
        {
            T x = 0;
            T y = 0;

            inline bool operator==(const vec2& other) const noexcept
            {
                return memcmp(this, &other, sizeof(vec2));
            }

            inline bool operator!=(const vec2& other) const noexcept
            {
                return memcmp(this, &other, sizeof(vec2));
            }

            inline vec2 operator/(const vec2& other) const noexcept
            {
                return { x / other.x, y / other.y };
            }

            template<typename U = T>
            inline U area() const noexcept
            {
                return static_cast<U>(x) * static_cast<U>(y);
            }
        };

        using u8 = uint8_t;
        using u32 = uint32_t;
        using u32x2 = vec2<u32>;

        struct const_buffer
        {
            u32x2 cellSize;
            u32x2 cellCount;
        };

        struct cell
        {
            u32 glyphIndex;
            u32x2 color;
        };

        enum class invalidation_flags : u8
        {
            none = 0,
            device = 1 << 0,
            size = 1 << 1,
            font = 1 << 2,
            title = 1 << 3,
        };
        friend constexpr invalidation_flags operator~(invalidation_flags v) noexcept { return static_cast<invalidation_flags>(~static_cast<u8>(v)); }
        friend constexpr invalidation_flags operator|(invalidation_flags lhs, invalidation_flags rhs) noexcept { return static_cast<invalidation_flags>(static_cast<u8>(lhs) | static_cast<u8>(rhs)); }
        friend constexpr invalidation_flags operator&(invalidation_flags lhs, invalidation_flags rhs) noexcept { return static_cast<invalidation_flags>(static_cast<u8>(lhs) & static_cast<u8>(rhs)); }
        friend constexpr void operator|=(invalidation_flags& lhs, invalidation_flags rhs) noexcept { lhs = lhs | rhs; }
        friend constexpr void operator&=(invalidation_flags& lhs, invalidation_flags rhs) noexcept { lhs = lhs & rhs; }

        enum class font_weight : size_t
        {
            normal = 0,
            bold = 0,
        };

        enum class font_style : size_t
        {
            normal = 0,
            italic = 0,
        };

        struct static_resources
        {
            wil::com_ptr<ID2D1Factory> d2dFactory;
            wil::com_ptr<IDWriteFactory> dwriteFactory;
            bool isWindows10OrGreater = true;
        } _sr;

        struct resources
        {
            // D3D resources
            wil::com_ptr<ID3D11Device> device;
            wil::com_ptr<ID3D11DeviceContext1> deviceContext;
            wil::com_ptr<IDXGISwapChain1> swapChain;
            wil::unique_handle swapChainHandle;
            wil::unique_handle frameLatencyWaitableObject;
            wil::com_ptr<ID3D11RenderTargetView> renderTargetView;
            wil::com_ptr<ID3D11VertexShader> vertexShader;
            wil::com_ptr<ID3D11PixelShader> pixelShader;
            wil::com_ptr<ID3D11Buffer> constantBuffer;
            wil::com_ptr<ID3D11Buffer> cellBuffer;
            wil::com_ptr<ID3D11ShaderResourceView> cellView;
            wil::com_ptr<ID3D11Texture2D> glyphBuffer;
            wil::com_ptr<ID3D11ShaderResourceView> glyphView;

            // D2D resources
            wil::com_ptr<ID2D1RenderTarget> renderTarget;
            wil::com_ptr<ID2D1Brush> brush;
            wil::com_ptr<IDWriteTextFormat> textFormats[2][2];

            // Other resources (backing buffers)
            aligned_buffer<cell> cells;
            std::unordered_map<wchar_t, u32> glyphs;
        } _r;

        struct api_state
        {
            u32x2 sizeInPixel; // invalidation_flags::size
            u32x2 cellSize; // invalidation_flags::size
            u32x2 cellCount; // dependent value calculated from the prior 2

            std::wstring fontName; // invalidation_flags::font|size
            u32 fontSize = 0; // invalidation_flags::font|size
            u32 dpi = USER_DEFAULT_SCREEN_DPI; // invalidation_flags::font|size
            D2D1_TEXT_ANTIALIAS_MODE antialiasingMode = D2D1_TEXT_ANTIALIAS_MODE_DEFAULT; // invalidation_flags::font

            std::function<void()> swapChainChangedCallback;
            HWND hwnd = nullptr;
        } _api;

        struct render_api_state
        {
            til::rectangle dirtyArea;
            uint32_t currentForegroundColor = 0;
            uint32_t currentBackgroundColor = 0;
        } _rapi;

        invalidation_flags _invalidations = invalidation_flags::device;
    };
}
