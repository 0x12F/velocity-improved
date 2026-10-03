#pragma once
#include <core/features/changer/cosmetic_configuration.hpp>

namespace features::changer::cosmetic_config
{
    struct item_skin_t;
}

namespace features::changer::weapon_cosmetics
{
    [[nodiscard]] bool HasAny(const features::changer::cosmetic_config::item_skin_t& config);
}
