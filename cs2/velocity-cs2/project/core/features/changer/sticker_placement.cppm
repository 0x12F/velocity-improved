module;

#include "platform/platform.h"

export module velocity:core.features.changer.sticker_placement;

import <algorithm>;

export import :core.features.changer.cosmetic_configuration;

export {
namespace features::changer::sticker_placement {
// CS2's free-placement X/Y values are stored directly in the econ attributes
// and preview protobuf. Keep the UI/config in that native coordinate space so
// placement does not depend on a particular weapon model.
inline constexpr float kMinOffset = -0.5f;
inline constexpr float kMaxOffset = 0.5f;

struct Vec2 {
  float x = 0.0f;
  float y = 0.0f;
};

inline Vec2 ClampOffsets(float offset_x, float offset_y) {
  return {std::clamp(offset_x, kMinOffset, kMaxOffset),
          std::clamp(offset_y, kMinOffset, kMaxOffset)};
}
}  // namespace features::changer::sticker_placement
}  // export
