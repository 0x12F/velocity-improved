#ifndef VELOCITY_CORE_FEATURES_CHANGER_SKIN_APPLICATION_H_
#define VELOCITY_CORE_FEATURES_CHANGER_SKIN_APPLICATION_H_
#include "core/features/changer/cosmetic_configuration.h"
#include "core/features/changer/engine.h"

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

#endif  // VELOCITY_CORE_FEATURES_CHANGER_SKIN_APPLICATION_H_
