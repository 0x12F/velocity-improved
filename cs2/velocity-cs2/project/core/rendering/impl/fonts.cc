module;

#include "platform/platform.h"

#include <external/xdraw/xdraw.hpp>

module velocity;

import <cstddef>;

namespace rendering {

void Fonts::Initialize() {
  this->LoadFamily(this->inter_medium,
                   std::as_bytes(std::span{resources::fonts::inter::regular}),
                   {12.0f, 15.0f, 18.0f});
  this->LoadFamily(this->inter_bold,
                   std::as_bytes(std::span{resources::fonts::inter::bold}),
                   {12.0f, 15.0f, 18.0f});
  this->LoadFamily(this->smallest_pixel7,
                   std::as_bytes(std::span{resources::fonts::pixel7::smallest}),
                   {9.0f, 10.5f, 14.0f});
}

void Fonts::LoadFamily(
    FamilyT& family, std::span<const std::byte> data,
    const std::array<float, static_cast<std::size_t>(Size::kCount)>& sizes) {
  for (auto i = 0ull; i < sizes.size(); ++i) {
    family.sizes[i] = xdraw::load_font(data, sizes[i]);
  }
}

}  // namespace rendering
