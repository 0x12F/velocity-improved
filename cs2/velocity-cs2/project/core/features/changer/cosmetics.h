#ifndef VELOCITY_CORE_FEATURES_CHANGER_COSMETICS_H_
#define VELOCITY_CORE_FEATURES_CHANGER_COSMETICS_H_

#include <external/nlohmann/json.hpp>

#include "core/features/changer/cosmetic_configuration.h"

namespace features::changer::cosmetic_config {
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SkinStickerT, enabled, kit,
                                                wear, scale, rotation, offset_x,
                                                offset_y)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SkinKeychainT, enabled, id,
                                                seed, offset_x, offset_y,
                                                offset_z)
}  // namespace features::changer::cosmetic_config

#endif  // VELOCITY_CORE_FEATURES_CHANGER_COSMETICS_H_
