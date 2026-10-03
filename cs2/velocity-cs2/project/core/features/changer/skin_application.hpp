#pragma once
#include <core/features/changer/engine.hpp>
#include <core/features/changer/cosmetic_configuration.hpp>

namespace features::changer::cosmetic_config
{
    struct item_skin_t;
}

namespace features::changer::skin_application
{
    enum class ItemKind
    {
        Weapon,
        Knife
    };

    bool NeedsUpdate(features::changer::engine::weapon* weapon, features::changer::engine::item_view* item, const features::changer::cosmetic_config::item_skin_t& config, bool forceUpdate, ItemKind kind);
    bool IsDefaultState(features::changer::engine::weapon* weapon, features::changer::engine::item_view* item, ItemKind kind);

    void Apply(features::changer::engine::weapon* weapon, features::changer::engine::item_view* item, const features::changer::cosmetic_config::item_skin_t& config, features::changer::engine::player_items* localPawn, ItemKind kind);
    void ApplyDefault(features::changer::engine::weapon* weapon, features::changer::engine::item_view* item, features::changer::engine::player_items* localPawn, ItemKind kind);

    bool RebuildHudKeychainAddon(features::changer::engine::base_entity* weapon, features::changer::engine::player_items* localPawn);

    void ClearHudWeaponIconFor(features::changer::engine::base_entity* weapon);
    void RegenerateSkins();
}
