#pragma once

#include <cstddef>
#include <cstdint>
#include <d3d11.h>

struct ResourceBinding_t { void* pData; std::byte unknown08[0x18]; std::int32_t strongReferences; };

namespace Panorama
{
    // Read-only views of the private Panorama/DX11 fields used by preview capture.
    struct RenderTargetName
    {
        std::uint32_t length;
        std::uint32_t flags;
        union
        {
            const char* heap;
            char inlineText[sizeof(const char*)];
        };

        [[nodiscard]] const char* Get() const
        {
            if (flags & 0x40000000u)
                return inlineText;
            return (flags & 0x3FFFFFFFu) ? heap : nullptr;
        }
    };

    struct RenderTarget
    {
        void* vtable;
        RenderTargetName name;
        std::uint32_t flags;
        std::byte unknown1C[0x7C];
        ResourceBinding_t* colorBinding;
        ResourceBinding_t* postProcessBinding;
    };

    struct TextureDx11
    {
        std::byte unknown00[0x10];
        ID3D11ShaderResourceView* linearView;
        ID3D11ShaderResourceView* srgbView;
    };

    struct Item3dPanel
    {
        std::byte unknown000[0x8D0];
        std::int32_t renderEntityHandle;
    };

}
