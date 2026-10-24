module;

#include "platform/platform.h"

#include <d3d11.h>

export module velocity:core.systems.preview.layouts;

import <cstddef>;
import <cstdint>;

export {
struct ResourceBindingT {
  void* p_data;
  std::byte unknown08[0x18];
  std::int32_t strong_references;
};

namespace panorama {
// Read-only views of the private Panorama/DX11 fields used by preview capture.
struct RenderTargetName {
  std::uint32_t length;
  std::uint32_t flags;
  union {
    const char* heap;
    char inline_text[sizeof(const char*)];
  };

  [[nodiscard]] const char* Get() const {
    if (flags & 0x40000000u) return inline_text;
    return (flags & 0x3FFFFFFFu) ? heap : nullptr;
  }
};

struct RenderTarget {
  void* vtable;
  RenderTargetName name;
  std::uint32_t flags;
  std::byte unknown1_c[0x7C];
  ResourceBindingT* color_binding;
  ResourceBindingT* post_process_binding;
};

struct TextureDx11 {
  std::byte unknown00[0x10];
  ID3D11ShaderResourceView* linear_view;
  ID3D11ShaderResourceView* srgb_view;
};

struct Item3dPanel {
  std::byte unknown000[0x8D0];
  std::int32_t render_entity_handle;
};

}  // namespace panorama
}  // export
