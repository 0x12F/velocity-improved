#include <pch/pch.hpp>
#include <core/features/features.hpp>
#include <core/features/changer/engine.hpp>
#include <protection/game_addresses.hpp>
#include <core/features/changer/weapon_cosmetics.hpp>

bool features::changer::weapon_cosmetics::HasAny(const features::changer::cosmetic_config::item_skin_t& config)
{
    for (const auto& sticker : config.stickers)
    {
        if (sticker.enabled && sticker.kit > 0)
            return true;
    }

    return config.keychain.enabled && config.keychain.id > 0;
}
