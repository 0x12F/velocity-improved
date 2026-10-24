module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>

export module velocity:core.features.changer.skin_application;

import <cstddef>;
import <filesystem>;
import <string_view>;
import <type_traits>;

export import :core.features.changer.cosmetic_configuration;
export import :core.features.changer.engine;

export {
namespace features::changer::cosmetic_config {
struct ItemSkinT;
}

namespace features::changer::skin_application {
enum class ItemKind { kWeapon, kKnife };

bool NeedsUpdate(features::changer::engine::Weapon* weapon,
                 features::changer::engine::ItemView* item,
                 const features::changer::cosmetic_config::ItemSkinT& config,
                 bool force_update, ItemKind kind);
bool IsDefaultState(features::changer::engine::Weapon* weapon,
                    features::changer::engine::ItemView* item, ItemKind kind);

void Apply(features::changer::engine::Weapon* weapon,
           features::changer::engine::ItemView* item,
           const features::changer::cosmetic_config::ItemSkinT& config,
           features::changer::engine::PlayerItems* local_pawn, ItemKind kind);
void ApplyDefault(features::changer::engine::Weapon* weapon,
                  features::changer::engine::ItemView* item,
                  features::changer::engine::PlayerItems* local_pawn,
                  ItemKind kind);

bool RebuildHudKeychainAddon(
    features::changer::engine::BaseEntity* weapon,
    features::changer::engine::PlayerItems* local_pawn);

void ClearHudWeaponIconFor(features::changer::engine::BaseEntity* weapon);
void RegenerateSkins();
}  // namespace features::changer::skin_application
}  // export
