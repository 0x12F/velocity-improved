module;

#include "platform/platform.h"

#include <excpt.h>

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>
#include <external/xdraw/xdraw.hpp>

#include "protection/address_macros.h"

module velocity;

import <algorithm>;
import <cstddef>;
import <cstdint>;
import <cstring>;
import <filesystem>;
import <limits>;
import <string>;
import <string_view>;
import <type_traits>;
import <unordered_map>;

namespace features::changer {
namespace {
constexpr int kItemQualityNormal = 0;
constexpr int kItemQualityUnusual = 3;
constexpr int kItemQualityStrange = 9;

using BuildModernWeaponSkinMaterialFn = void(__fastcall *)(engine::ItemView *,
                                                           void *, std::int64_t,
                                                           int, char, char,
                                                           void *);
using ReleaseCompositeMaterialsFn =
    void(__fastcall *)(engine::CompositeMaterialOwner *, bool);

struct WeaponStickerState {
  std::uint16_t definition_index = 0;
  int effective_count = 0;
};

// Track the previous count so the native release is only used for N -> 0.
std::unordered_map<std::uintptr_t, WeaponStickerState> g_weapon_sticker_states;

int GetEffectiveStickerCount(const cosmetic_config::ItemSkinT &config) {
  int count = 0;

  for (const auto &sticker : config.stickers) {
    if (sticker.enabled && sticker.kit > 0) ++count;
  }

  return count;
}

bool UpdateStickerState(engine::Weapon *weapon, engine::ItemView *item,
                        int effective_count) {
  if (!weapon || !item) return false;

  const std::uintptr_t key = reinterpret_cast<std::uintptr_t>(weapon);
  const std::uint16_t definition_index = item->MIItemDefinitionIndex();
  const auto it = g_weapon_sticker_states.find(key);

  bool final_sticker_removed = false;

  if (it != g_weapon_sticker_states.end() &&
      it->second.definition_index == definition_index)
    final_sticker_removed =
        it->second.effective_count > 0 && effective_count == 0;

  g_weapon_sticker_states[key] = {definition_index, effective_count};
  return final_sticker_removed;
}

ReleaseCompositeMaterialsFn GetReleaseCompositeMaterials() {
  return engine::CachedAddress([] {
    return reinterpret_cast<ReleaseCompositeMaterialsFn>(
        PATTERN(patterns::release_composite_materials));
  });
}

void ReleaseEntityCompositeMaterials(engine::BaseEntity *entity,
                                     bool stamp_generation) {
  engine::CompositeMaterialOwner *owner = engine::CompositeOwner(entity);
  if (!owner) return;

  const auto release = GetReleaseCompositeMaterials();

  if (!release) return;

  // Keep recovery around the native release; a null check cannot validate
  // the engine-owned composite material state.
  __try {
    release(owner, stamp_generation);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

int GetBaseQuality(skin_application::ItemKind kind) {
  return kind == skin_application::ItemKind::kKnife ? kItemQualityUnusual
                                                    : kItemQualityNormal;
}

int GetDesiredQuality(const cosmetic_config::ItemSkinT &config,
                      skin_application::ItemKind kind) {
  return config.stat_trak_enabled ? kItemQualityStrange : GetBaseQuality(kind);
}

int GetDesiredStatTrak(const cosmetic_config::ItemSkinT &config) {
  return config.stat_trak_enabled ? (std::max)(0, config.stat_trak_kills) : -1;
}

float GetDesiredWear(const cosmetic_config::ItemSkinT &config) {
  return std::clamp(config.wear, 0.0001f, 1.0f);
}

int GetDesiredSeed(const cosmetic_config::ItemSkinT &config) {
  return std::clamp(config.seed, 0, 1000);
}

engine::BaseEntity *FindHudWeapon(engine::BaseEntity *weapon,
                                  engine::PlayerItems *local_pawn) {
  if (!weapon || !local_pawn || !addresses::globals::game_entity_system)
    return nullptr;

  const engine::ItemHandle arms_handle = local_pawn->MHHudModelArms();

  if (!arms_handle.Valid()) return nullptr;

  engine::BaseEntity *hud_arms = reinterpret_cast<engine::BaseEntity *>(
      systems::g_entities.GetByIndex(arms_handle.Index()));

  if (!hud_arms) return nullptr;

  engine::SceneNode *arms_node = hud_arms->MPGameSceneNode();

  if (!arms_node) return nullptr;

  for (engine::SceneNode *node = arms_node->GetChild(); node;
       node = node->NextSibling()) {
    engine::Entity *node_owner = node->GetOwner();

    if (!node_owner) continue;

    engine::BaseEntity *hud_entity =
        reinterpret_cast<engine::BaseEntity *>(node_owner);
    const engine::ItemHandle owner_handle = hud_entity->MHOwnerEntity();

    if (!owner_handle.Valid()) continue;

    engine::BaseEntity *owner_entity = reinterpret_cast<engine::BaseEntity *>(
        systems::g_entities.GetByIndex(owner_handle.Index()));

    if (owner_entity == weapon) return hud_entity;
  }

  return nullptr;
}

std::uintptr_t FindHudElement(const char *name) {
  using Fn = std::uintptr_t(__fastcall *)(const char *);

  const auto fn = engine::CachedAddress(
      [] { return reinterpret_cast<Fn>(PATTERN(patterns::find_hud_element)); });
  return fn ? fn(name) : 0;
}

void ClearHudWeaponIconAtIndex(std::uintptr_t hud_weapons, std::int32_t index,
                               std::int64_t unknown) {
  const auto call_address = engine::CachedAddress([] {
    return reinterpret_cast<std::uint8_t *>(
        PATTERN(patterns::clear_hud_weapon));
  });

  if (!call_address) return;

  using Fn =
      std::int64_t(__fastcall *)(std::uintptr_t, std::int32_t, std::int64_t);

  const auto fn =
      reinterpret_cast<Fn>(reinterpret_cast<std::uintptr_t>(call_address) + 5 +
                           *reinterpret_cast<std::int32_t *>(call_address + 1));

  fn(hud_weapons, index, unknown);
}

BuildModernWeaponSkinMaterialFn GetBuildModernWeaponSkinMaterial() {
  return engine::CachedAddress([] {
    return reinterpret_cast<BuildModernWeaponSkinMaterialFn>(
        PATTERN(patterns::build_modern_skin));
  });
}

void BuildModernWeaponSkinMaterial(engine::ItemView *item, void *model) {
  if (!item || !model) return;

  const auto fn = GetBuildModernWeaponSkinMaterial();

  if (fn) fn(item, model, 0, 2048, 0, 0, nullptr);
}

void SetCustomName(engine::ItemView *item, const std::string &name) {
  if (!item) return;

  char *custom_name = item->MSzCustomName();

  if (!custom_name) return;

  if (name.empty()) {
    custom_name[0] = '\0';
    return;
  }

  strncpy_s(custom_name, 161, name.c_str(), _TRUNCATE);
}

std::uint64_t GetMeshMask(int paint_kit) {
  const EconItemSystem::PaintKit *paint_kit_info =
      g_econ_item_system.FindPaintKit(paint_kit);
  return paint_kit_info && paint_kit_info->legacy_model ? 2ULL : 1ULL;
}

void SetMeshGroup(engine::BaseEntity *entity, std::uint64_t mesh_mask) {
  if (!entity) return;

  if (engine::SceneNode *scene_node = entity->MPGameSceneNode())
    scene_node->SetMeshGroupMask(mesh_mask);
}

void ApplyWeaponMaterial(engine::Weapon *weapon, engine::ItemView *item,
                         engine::PlayerItems *local_pawn,
                         std::uint64_t mesh_mask, bool final_sticker_removed,
                         bool replacement_composite_expected) {
  engine::BaseEntity *hud_weapon = FindHudWeapon(weapon, local_pawn);

  if (final_sticker_removed) {
    // If a paint kit remains, clear the old composite ownership without running
    // the derived empty-material callback. The replacement paint-only composite
    // is built immediately below, avoiding a transient stock-material reapply.
    // The HUD model has its own owner and must be cleared separately on N -> 0.
    const bool stamp_generation = !replacement_composite_expected;
    ReleaseEntityCompositeMaterials(weapon, stamp_generation);
    ReleaseEntityCompositeMaterials(hud_weapon, stamp_generation);
  }

  SetMeshGroup(weapon, mesh_mask);
  SetMeshGroup(hud_weapon, mesh_mask);

  BuildModernWeaponSkinMaterial(item, weapon);

  if (hud_weapon) BuildModernWeaponSkinMaterial(item, hud_weapon);
}

void ApplyKnifeMaterial(engine::Weapon *weapon, engine::ItemView *item,
                        engine::PlayerItems *local_pawn,
                        std::uint64_t mesh_mask) {
  SetMeshGroup(weapon, mesh_mask);
  BuildModernWeaponSkinMaterial(item, weapon);

  engine::BaseEntity *hud_weapon = FindHudWeapon(weapon, local_pawn);

  SetMeshGroup(hud_weapon, mesh_mask);

  if (hud_weapon) BuildModernWeaponSkinMaterial(item, hud_weapon);
}
}  // namespace

bool skin_application::RebuildHudKeychainAddon(
    engine::BaseEntity *weapon, engine::PlayerItems *local_pawn) {
  engine::BaseEntity *hud_weapon = FindHudWeapon(weapon, local_pawn);

  if (!hud_weapon || !weapon || !addresses::globals::game_entity_system)
    return false;

  constexpr std::uint32_t kKeychainHash =
      0x0AA5272A;  // Native dictionary hash of "keychain".
  using AddonDictFindFn = std::uint32_t(__fastcall *)(
      engine::HudAddonDictionary *, const char *, std::uint32_t, bool *);
  using UtilRemoveFn = void(__fastcall *)(engine::BaseEntity *);

  const auto find_addon = engine::CachedAddress([] {
    return reinterpret_cast<AddonDictFindFn>(
        PATTERN(patterns::find_hud_keychain_addon));
  });

  const auto util_remove = engine::CachedAddress([] {
    return reinterpret_cast<UtilRemoveFn>(
        PATTERN(patterns::remove_hud_keychain_addon));
  });

  if (!find_addon || !util_remove) return false;

  const engine::ItemHandle world_weapon_handle = hud_weapon->MHOwnerEntity();
  if (!world_weapon_handle.Valid()) return false;

  engine::BaseEntity *resolved_world_weapon =
      reinterpret_cast<engine::BaseEntity *>(
          systems::g_entities.GetByIndex(world_weapon_handle.Index()));
  if (resolved_world_weapon != weapon) return false;

  auto *hud = reinterpret_cast<engine::HudWeaponAddonLayout *>(hud_weapon);
  engine::HudAddonDictionary &dictionary = hud->addons;
  const std::uint32_t addon_index =
      find_addon(&dictionary, "keychain", kKeychainHash, nullptr);

  if (addon_index != engine::kInvalidEhandleIndex) {
    if (addon_index >= dictionary.Capacity() || !dictionary.entries)
      return false;

    engine::ItemHandle &addon_handle =
        dictionary.entries[addon_index].entity_handle;
    if (addon_handle.Valid()) {
      if (engine::BaseEntity *addon = reinterpret_cast<engine::BaseEntity *>(
              systems::g_entities.GetByIndex(addon_handle.Index())))
        util_remove(addon);

      addon_handle = engine::ItemHandle{};
    }
  }

  hud->keychain_state = 0xFFu;
  return true;
}

bool skin_application::NeedsUpdate(engine::Weapon *weapon,
                                   engine::ItemView *item,
                                   const cosmetic_config::ItemSkinT &config,
                                   bool force_update, ItemKind kind) {
  if (!weapon || !item) return false;

  if (force_update) return true;

  if (weapon->MNFallbackPaintKit() != config.paint_kit) return true;

  if (weapon->MNFallbackSeed() != GetDesiredSeed(config)) return true;

  if (weapon->MFlFallbackWear() != GetDesiredWear(config)) return true;

  if (weapon->MNFallbackStatTrak() != GetDesiredStatTrak(config)) return true;

  if (item->MIEntityQuality() != GetDesiredQuality(config, kind)) return true;

  char *custom_name = item->MSzCustomName();

  if (!custom_name) return !config.custom_name.empty();

  return std::strncmp(custom_name, config.custom_name.c_str(), 160) != 0;
}

bool skin_application::IsDefaultState(engine::Weapon *weapon,
                                      engine::ItemView *item, ItemKind kind) {
  if (!weapon || !item) return true;

  if (weapon->MNFallbackPaintKit() != 0) return false;

  if (weapon->MNFallbackStatTrak() != -1) return false;

  if (item->MIEntityQuality() != GetBaseQuality(kind)) return false;

  char *custom_name = item->MSzCustomName();

  return !custom_name || custom_name[0] == '\0';
}

void skin_application::Apply(engine::Weapon *weapon, engine::ItemView *item,
                             const cosmetic_config::ItemSkinT &config,
                             engine::PlayerItems *local_pawn, ItemKind kind) {
  if (!weapon || !item || !local_pawn) return;

  const bool final_sticker_removed =
      kind == ItemKind::kWeapon &&
      UpdateStickerState(weapon, item, GetEffectiveStickerCount(config));

  const float wear = GetDesiredWear(config);
  const int seed = GetDesiredSeed(config);
  const int stat_trak = GetDesiredStatTrak(config);

  attributes::Remove(item);

  item->MIItemIdHigh() = 0xFFFFFFFFu;
  item->MBInitialized() = true;
  item->MBDisallowSoc() = false;
  item->MBRestoreCustomMaterialAfterPrecache() = true;
  item->MIEntityQuality() = GetDesiredQuality(config, kind);

  weapon->MNFallbackPaintKit() = config.paint_kit;
  weapon->MNFallbackSeed() = seed;
  weapon->MFlFallbackWear() = wear;
  weapon->MNFallbackStatTrak() = stat_trak;

  attributes::StickerArray stickers{};
  attributes::KeychainData keychain{};

  if (kind == ItemKind::kWeapon) {
    stickers = config.stickers;
    keychain = config.keychain;
    keychain.enabled = config.keychain.enabled && config.keychain.id > 0;
  }

  attributes::Create(item, config.paint_kit, wear, seed, stat_trak, 0,
                     kind == ItemKind::kWeapon ? &stickers : nullptr,
                     kind == ItemKind::kWeapon ? &keychain : nullptr);

  // Keep the live keychain hidden from the normal HUD/material refresh; the
  // deferred keychain pass restores the ID and rebuilds the native world/HUD
  // charm.
  if (kind == ItemKind::kWeapon && keychain.enabled)
    attributes::SetKeychainId(item, 0);

  SetCustomName(item, config.custom_name);
  item->MNameDescriptionPtr() = 0;

  const std::uint64_t mesh_mask = GetMeshMask(config.paint_kit);

  if (kind == ItemKind::kKnife) {
    ApplyKnifeMaterial(weapon, item, local_pawn, mesh_mask);
    return;
  }

  ApplyWeaponMaterial(weapon, item, local_pawn, mesh_mask,
                      final_sticker_removed, config.paint_kit > 0);
}

void skin_application::ApplyDefault(engine::Weapon *weapon,
                                    engine::ItemView *item,
                                    engine::PlayerItems *local_pawn,
                                    ItemKind kind) {
  if (!weapon || !item || !local_pawn) return;

  const bool final_sticker_removed =
      kind == ItemKind::kWeapon && UpdateStickerState(weapon, item, 0);

  attributes::Remove(item);

  item->MIEntityQuality() = GetBaseQuality(kind);
  item->MBInitialized() = true;
  item->MBDisallowSoc() = false;
  item->MBRestoreCustomMaterialAfterPrecache() = true;

  weapon->MNFallbackPaintKit() = 0;
  weapon->MNFallbackSeed() = 0;
  weapon->MFlFallbackWear() = 0.0f;
  weapon->MNFallbackStatTrak() = -1;

  SetCustomName(item, {});
  item->MNameDescriptionPtr() = 0;

  if (kind == ItemKind::kKnife) {
    SetMeshGroup(weapon, 1ULL);

    engine::BaseEntity *hud_weapon = FindHudWeapon(weapon, local_pawn);

    SetMeshGroup(hud_weapon, 1ULL);
    return;
  }

  engine::BaseEntity *hud_weapon = FindHudWeapon(weapon, local_pawn);

  if (final_sticker_removed) {
    // No custom paint composite will replace the sticker material in the
    // default state, so use the native stamped release to reapply the base
    // material now.
    ReleaseEntityCompositeMaterials(weapon, true);
    ReleaseEntityCompositeMaterials(hud_weapon, true);
  }

  weapon->PostDataUpdate(1);

  SetMeshGroup(weapon, 1ULL);
  SetMeshGroup(hud_weapon, 1ULL);
}

void skin_application::ClearHudWeaponIconFor(engine::BaseEntity *weapon) {
  if (!weapon || !addresses::globals::game_entity_system) return;

  // HUD selection owns this mutable array and invokes native removal code.
  // Retain its existing recovery boundary while refactoring the application
  // flow.
  __try {
    const std::uintptr_t hud = FindHudElement("HudWeaponSelection");

    if (!hud) return;

    auto *selection = engine::HudWeaponSelectionLayout::FromHudElement(hud);
    const engine::HudWeaponSelectionEntry *data = selection->entries;
    const std::int32_t count = selection->count;

    if (!data || count <= 0 || count > 64) return;

    for (std::int32_t i = count - 1; i >= 0; --i) {
      const std::int32_t handle = data[i].weapon_handle;

      if (handle < 0) continue;

      engine::BaseEntity *hud_weapon = reinterpret_cast<engine::BaseEntity *>(
          systems::g_entities.GetByIndex(handle & engine::kEntryMask));

      if (hud_weapon == weapon) {
        ClearHudWeaponIconAtIndex(reinterpret_cast<std::uintptr_t>(selection),
                                  i, 0);
        return;
      }
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

void skin_application::RegenerateSkins() {
  using Fn = void(__fastcall *)();

  const Fn fn = engine::CachedAddress(
      [] { return reinterpret_cast<Fn>(PATTERN(patterns::regenerate_skins)); });

  if (fn) fn();
}

}  // namespace features::changer
