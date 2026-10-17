#ifndef VELOCITY_CORE_FEATURES_CHANGER_STICKER_PLACEMENT_H_
#define VELOCITY_CORE_FEATURES_CHANGER_STICKER_PLACEMENT_H_
#include <algorithm>

#include "core/features/changer/cosmetic_configuration.h"

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

#endif  // VELOCITY_CORE_FEATURES_CHANGER_STICKER_PLACEMENT_H_
