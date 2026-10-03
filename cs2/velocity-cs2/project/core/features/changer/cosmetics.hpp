#pragma once

#include <core/features/changer/cosmetic_configuration.hpp>
#include <external/nlohmann/json.hpp>

namespace features::changer::cosmetic_config {
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(skin_sticker_t,
        enabled, kit, wear, scale, rotation, offsetX, offsetY)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(skin_keychain_t,
        enabled, id, seed, offsetX, offsetY, offsetZ)
}
