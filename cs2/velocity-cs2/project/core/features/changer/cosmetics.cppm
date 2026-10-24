module;

#include "platform/platform.h"

#include <external/nlohmann/json.hpp>

export module velocity:core.features.changer.cosmetics;

export import :core.features.changer.cosmetic_configuration;

export {
namespace features::changer::cosmetic_config {
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SkinStickerT, enabled, kit,
                                                wear, scale, rotation, offset_x,
                                                offset_y)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SkinKeychainT, enabled, id,
                                                seed, offset_x, offset_y,
                                                offset_z)
}  // namespace features::changer::cosmetic_config
}  // export
