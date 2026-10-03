#pragma once
#include <core/features/changer/cosmetic_configuration.hpp>

#include <algorithm>

namespace features::changer::sticker_placement
{
    // CS2's free-placement X/Y values are stored directly in the econ attributes
    // and preview protobuf. Keep the UI/config in that native coordinate space so
    // placement does not depend on a particular weapon model.
    inline constexpr float MIN_OFFSET = -0.5f;
    inline constexpr float MAX_OFFSET = 0.5f;

    struct Vec2
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    inline Vec2 ClampOffsets(float offsetX, float offsetY)
    {
        return
        {
            std::clamp(offsetX, MIN_OFFSET, MAX_OFFSET),
            std::clamp(offsetY, MIN_OFFSET, MAX_OFFSET)
        };
    }
}
