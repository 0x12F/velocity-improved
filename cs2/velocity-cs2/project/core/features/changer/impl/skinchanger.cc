module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>
#include <external/xdraw/xdraw.hpp>

module velocity;

import <algorithm>;
import <atomic>;
import <chrono>;
import <cstddef>;
import <cstdint>;
import <filesystem>;
import <limits>;
import <string_view>;
import <type_traits>;
import <unordered_map>;
import <vector>;

namespace features::changer {
namespace {
constexpr int kForceUpdateFrames = 6;
std::atomic_int g_update_frames{kForceUpdateFrames};
std::vector<engine::ItemHandle> g_last_weapon_handles;
std::unordered_map<std::uintptr_t, std::uint64_t>
    g_last_applied_gun_config_hash;

using weapon_cosmetics::AppliedKeychainState;

std::unordered_map<std::uintptr_t, AppliedKeychainState>
    g_last_applied_keychain_state;

struct PendingKeychainOperation {
  engine::ItemHandle weapon_handle{};
  AppliedKeychainState desired{};
};

std::unordered_map<std::uintptr_t, PendingKeychainOperation>
    g_pending_keychain_operations;

// UI edits and FrameStageNotify run on different threads; use a short lease.
std::atomic_bool g_interactive_edit_active{false};
std::atomic<std::uint64_t> g_interactive_edit_heartbeat_ms{0};

constexpr std::uint64_t kInteractiveEditLeaseMs = 500;

// Keep the previous preview frame while live custom-material work settles.
constexpr std::uint64_t kPreviewPostUpdateSettleMs = 500;
std::atomic<std::uint64_t> g_preview_blocked_until_ms{0};

std::uint64_t NowMilliseconds() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

void ExtendPreviewUpdateBlock() {
  g_preview_blocked_until_ms.store(
      NowMilliseconds() + kPreviewPostUpdateSettleMs,
      std::memory_order_release);
}

bool HasLivePreviewUpdateTarget() {
  if (!addresses::globals::source2engine_to_client ||
      !memory::CallVfunc<bool>(addresses::globals::source2engine_to_client,
                               40) ||
      !memory::CallVfunc<bool>(addresses::globals::source2engine_to_client,
                               39)) {
    return false;
  }

  engine::PlayerItems *local_pawn = engine::LocalPawn();
  return local_pawn && local_pawn->MIHealth() > 0;
}

bool g_restore_knife_after_default = false;
std::uint16_t g_restore_knife_definition = 0;

void ResetKnifeDefaultRefresh() {
  g_restore_knife_after_default = false;
  g_restore_knife_definition = 0;
}

std::uint16_t GetTemporaryKnifeDefinition(std::uint16_t current_definition) {
  return current_definition == 500 ? 507 : 500;
}

engine::Weapon *GetWeaponFromHandle(const engine::ItemHandle &handle) {
  if (!handle.Valid() || !addresses::globals::game_entity_system)
    return nullptr;

  return reinterpret_cast<engine::Weapon *>(
      systems::g_entities.GetByIndex(handle.Index()));
}

engine::ItemView *GetWeaponItem(engine::Weapon *weapon) {
  if (!weapon) return nullptr;

  engine::AttributeContainer *attribute_manager = weapon->MAttributeManager();
  return attribute_manager ? attribute_manager->MItem() : nullptr;
}

void ProcessKnife(engine::Weapon *weapon, engine::ItemView *item,
                  engine::PlayerItems *local_pawn, bool force_update,
                  bool &did_update) {
  if (!weapon || !item || !local_pawn) return;

  if (g_restore_knife_after_default) {
    const std::uint16_t restore_definition = g_restore_knife_definition;

    ResetKnifeDefaultRefresh();

    if (restore_definition >= 500 &&
        knife_application::Apply(weapon, item, restore_definition)) {
      skin_application::ClearHudWeaponIconFor(weapon);
      g_update_frames.store(kForceUpdateFrames, std::memory_order_release);
      did_update = true;
    }

    return;
  }

  if (cosmetic_config::skin_changer.knife_definition < 500) return;

  const std::uint16_t target_definition =
      cosmetic_config::skin_changer.knife_definition;

  if (knife_application::Apply(weapon, item, target_definition)) {
    skin_application::ClearHudWeaponIconFor(weapon);
    g_update_frames.store(kForceUpdateFrames, std::memory_order_release);
    did_update = true;
    return;
  }

  const auto config_it =
      cosmetic_config::skin_changer.knives.find(target_definition);
  const cosmetic_config::ItemSkinT *config =
      config_it != cosmetic_config::skin_changer.knives.end()
          ? &config_it->second
          : nullptr;

  if (!config || !config->enabled || config->paint_kit <= 0) {
    const bool had_custom_paint = weapon->MNFallbackPaintKit() > 0;

    if (had_custom_paint) {
      skin_application::ApplyDefault(weapon, item, local_pawn,
                                     skin_application::ItemKind::kKnife);

      const std::uint16_t temporary_definition =
          GetTemporaryKnifeDefinition(target_definition);

      if (knife_application::Apply(weapon, item, temporary_definition)) {
        g_restore_knife_after_default = true;
        g_restore_knife_definition = target_definition;
        g_update_frames.store(kForceUpdateFrames, std::memory_order_release);
      }

      skin_application::ClearHudWeaponIconFor(weapon);
      did_update = true;
      return;
    }

    if (force_update || !skin_application::IsDefaultState(
                            weapon, item, skin_application::ItemKind::kKnife)) {
      skin_application::ApplyDefault(weapon, item, local_pawn,
                                     skin_application::ItemKind::kKnife);
      skin_application::ClearHudWeaponIconFor(weapon);
      did_update = true;
    }

    return;
  }

  if (!skin_application::NeedsUpdate(weapon, item, *config, force_update,
                                     skin_application::ItemKind::kKnife))
    return;

  skin_application::Apply(weapon, item, *config, local_pawn,
                          skin_application::ItemKind::kKnife);
  skin_application::ClearHudWeaponIconFor(weapon);

  did_update = true;
}

bool IsAppliedKeychainStateCurrent(std::uintptr_t weapon_key,
                                   const AppliedKeychainState &desired) {
  const auto it = g_last_applied_keychain_state.find(weapon_key);

  if (it == g_last_applied_keychain_state.end()) return !desired.effective;

  return weapon_cosmetics::KeychainStatesEqual(it->second, desired);
}

void QueueKeychainState(const engine::ItemHandle &weapon_handle,
                        engine::Weapon *weapon, std::uintptr_t weapon_key,
                        const cosmetic_config::ItemSkinT &config) {
  if (!weapon || !weapon_handle.Valid()) return;

  PendingKeychainOperation pending{};
  pending.weapon_handle = weapon_handle;
  pending.desired = weapon_cosmetics::GetDesiredKeychainState(config);

  // Rapid committed edits collapse to the newest desired state. Native
  // C_KeychainModule creation/destruction is deliberately not performed from
  // the pre-original FRAME_NET_UPDATE_END skin pass.
  g_pending_keychain_operations[weapon_key] = pending;
}

void PruneAppliedKeychainState(
    const std::vector<engine::ItemHandle> &current_weapon_handles) {
  const auto no_longer_owned = [&](const auto &entry) {
    return std::none_of(
        current_weapon_handles.begin(), current_weapon_handles.end(),
        [&](const engine::ItemHandle &handle) {
          return reinterpret_cast<std::uintptr_t>(
                     GetWeaponFromHandle(handle)) == entry.first;
        });
  };
  std::erase_if(g_last_applied_keychain_state, no_longer_owned);
  std::erase_if(g_pending_keychain_operations, no_longer_owned);
}

void ResetWeaponRuntime() {
  g_update_frames.store(kForceUpdateFrames, std::memory_order_release);
  g_last_weapon_handles.clear();
  g_last_applied_gun_config_hash.clear();
  g_last_applied_keychain_state.clear();
  g_pending_keychain_operations.clear();
  ResetKnifeDefaultRefresh();
}

bool HasAppliedKeychain(std::uintptr_t weapon_key) {
  const auto it = g_last_applied_keychain_state.find(weapon_key);
  return it != g_last_applied_keychain_state.end() && it->second.effective;
}

void QueueKeychainRemoval(std::uintptr_t weapon_key,
                          const engine::ItemHandle &weapon_handle) {
  g_pending_keychain_operations[weapon_key] = {weapon_handle, {}};
}

void ProcessGun(const engine::ItemHandle &weapon_handle, engine::Weapon *weapon,
                engine::ItemView *item, engine::PlayerItems *local_pawn,
                std::uint16_t definition_index, bool force_update,
                bool &did_update) {
  if (!weapon || !item || !local_pawn) return;

  const std::uintptr_t weapon_key = reinterpret_cast<std::uintptr_t>(weapon);
  const auto config_it =
      cosmetic_config::skin_changer.weapons.find(definition_index);

  if (config_it == cosmetic_config::skin_changer.weapons.end()) {
    const bool had_keychain = HasAppliedKeychain(weapon_key);

    if (had_keychain) {
      QueueKeychainRemoval(weapon_key, weapon_handle);

      skin_application::ApplyDefault(weapon, item, local_pawn,
                                     skin_application::ItemKind::kWeapon);
      skin_application::ClearHudWeaponIconFor(weapon);
      did_update = true;
    }

    g_last_applied_gun_config_hash.erase(weapon_key);
    return;
  }

  const cosmetic_config::ItemSkinT &config = config_it->second;
  const AppliedKeychainState desired_keychain =
      weapon_cosmetics::GetDesiredKeychainState(config);
  const bool keychain_state_changed =
      !IsAppliedKeychainStateCurrent(weapon_key, desired_keychain);

  if (!config.enabled || !weapon_cosmetics::HasCustomConfiguration(config)) {
    const bool had_keychain = HasAppliedKeychain(weapon_key);

    if (had_keychain) {
      QueueKeychainRemoval(weapon_key, weapon_handle);
    }

    if (force_update || had_keychain ||
        !skin_application::IsDefaultState(
            weapon, item, skin_application::ItemKind::kWeapon)) {
      skin_application::ApplyDefault(weapon, item, local_pawn,
                                     skin_application::ItemKind::kWeapon);
      skin_application::ClearHudWeaponIconFor(weapon);
      did_update = true;
    }

    g_last_applied_gun_config_hash.erase(weapon_key);
    return;
  }

  const std::uint64_t config_hash = weapon_cosmetics::ComputeConfigHash(config);
  const auto hash_it = g_last_applied_gun_config_hash.find(weapon_key);
  const bool cosmetic_state_changed =
      hash_it == g_last_applied_gun_config_hash.end() ||
      hash_it->second != config_hash;

  if (!cosmetic_state_changed && !keychain_state_changed &&
      !skin_application::NeedsUpdate(weapon, item, config, force_update,
                                     skin_application::ItemKind::kWeapon)) {
    return;
  }

  // Apply keychains after the normal attribute/material refresh has finished.
  if (desired_keychain.effective || keychain_state_changed)
    QueueKeychainState(weapon_handle, weapon, weapon_key, config);

  skin_application::Apply(weapon, item, config, local_pawn,
                          skin_application::ItemKind::kWeapon);
  skin_application::ClearHudWeaponIconFor(weapon);

  g_last_applied_gun_config_hash[weapon_key] = config_hash;
  did_update = true;
}
}  // namespace

void application::Run() {
  engine::PlayerItems *local_pawn = engine::LocalPawn();

  // cosmetic_config values are intentionally allowed to change while the user
  // drags a menu control, but the actual held weapon is not regenerated until
  // the edit commits.
  if (IsInteractiveEditActive()) return;

  if (!cosmetic_config::skin_changer.enabled) {
    if (local_pawn && local_pawn->MIHealth() > 0)
      glove_application::Run(local_pawn);

    ResetWeaponRuntime();
    return;
  }

  if (!local_pawn || !addresses::globals::game_entity_system ||
      local_pawn->MIHealth() <= 0)
    return;

  glove_application::Run(local_pawn);

  engine::WeaponServices *weapon_services = local_pawn->MPWeaponServices();

  if (!weapon_services) return;

  auto &weapon_handles = weapon_services->MHMyWeapons();

  std::vector<engine::ItemHandle> current_weapon_handles;
  current_weapon_handles.reserve(weapon_handles.Count());

  for (int i = 0; i < weapon_handles.Count(); ++i) {
    const engine::ItemHandle handle = weapon_handles.Element(i);

    if (handle.Valid()) current_weapon_handles.emplace_back(handle);
  }

  if (current_weapon_handles != g_last_weapon_handles) {
    g_last_weapon_handles = current_weapon_handles;
    g_last_applied_gun_config_hash.clear();
    PruneAppliedKeychainState(current_weapon_handles);
    g_update_frames.store(kForceUpdateFrames, std::memory_order_release);
    ResetKnifeDefaultRefresh();
  }

  const bool force_update = g_update_frames.load(std::memory_order_acquire) > 0;

  // Each forced application frame extends the preview quiet period. This means
  // the standalone preview starts only after the last live-weapon refresh frame
  // rather than racing the same custom-material pipeline.
  if (force_update) ExtendPreviewUpdateBlock();

  bool did_update = false;

  for (const engine::ItemHandle &handle : current_weapon_handles) {
    engine::Weapon *weapon = GetWeaponFromHandle(handle);

    if (!weapon) continue;

    engine::ItemView *item = GetWeaponItem(weapon);

    if (!item) continue;

    const std::uint16_t definition_index = item->MIItemDefinitionIndex();

    if (knife_application::IsKnifeDefinition(definition_index)) {
      ProcessKnife(weapon, item, local_pawn, force_update, did_update);
      continue;
    }

    ProcessGun(handle, weapon, item, local_pawn, definition_index, force_update,
               did_update);
  }

  if (did_update) {
    skin_application::RegenerateSkins();
    ExtendPreviewUpdateBlock();
  }

  if (force_update) g_update_frames.fetch_sub(1, std::memory_order_acq_rel);
}

void application::ProcessPendingKeychains() {
  if (g_update_frames.load(std::memory_order_acquire) > 0 ||
      g_pending_keychain_operations.empty())
    return;

  for (auto it = g_pending_keychain_operations.begin();
       it != g_pending_keychain_operations.end();) {
    const std::uintptr_t weapon_key = it->first;
    const PendingKeychainOperation pending = it->second;
    engine::Weapon *weapon = GetWeaponFromHandle(pending.weapon_handle);

    if (!weapon || reinterpret_cast<std::uintptr_t>(weapon) != weapon_key) {
      it = g_pending_keychain_operations.erase(it);
      continue;
    }

    engine::ItemView *item = GetWeaponItem(weapon);

    if (!item) {
      it = g_pending_keychain_operations.erase(it);
      continue;
    }

    const bool had_keychain = HasAppliedKeychain(weapon_key);

    bool completed = true;

    if (!pending.desired.effective) {
      if (had_keychain) {
        weapon->AddKeyChainEntity();
        skin_application::RebuildHudKeychainAddon(weapon, engine::LocalPawn());
      }
    } else {
      if (had_keychain) weapon->AddKeyChainEntity();

      completed = attributes::SetKeychainId(item, pending.desired.id);

      if (completed) {
        weapon->AddKeyChainEntity();
        skin_application::RebuildHudKeychainAddon(weapon, engine::LocalPawn());
      }
    }

    if (completed) g_last_applied_keychain_state[weapon_key] = pending.desired;

    it = g_pending_keychain_operations.erase(it);
  }
}

void application::SetInteractiveEditActive(bool active) {
  if (active) {
    g_interactive_edit_heartbeat_ms.store(NowMilliseconds(),
                                          std::memory_order_release);
    g_interactive_edit_active.store(true, std::memory_order_release);
    return;
  }

  g_interactive_edit_active.store(false, std::memory_order_release);
}

bool application::IsInteractiveEditActive() {
  if (!g_interactive_edit_active.load(std::memory_order_acquire)) return false;

  const std::uint64_t heartbeat =
      g_interactive_edit_heartbeat_ms.load(std::memory_order_acquire);

  const std::uint64_t now = NowMilliseconds();

  if (heartbeat == 0 || now - heartbeat > kInteractiveEditLeaseMs) {
    g_interactive_edit_active.store(false, std::memory_order_release);
    return false;
  }

  return true;
}

bool application::IsUpdatePending() {
  // Main-menu previews do not have a live weapon update to wait for.
  if (!HasLivePreviewUpdateTarget()) return false;

  if (g_update_frames.load(std::memory_order_acquire) > 0) return true;

  return NowMilliseconds() <
         g_preview_blocked_until_ms.load(std::memory_order_acquire);
}

void application::ForceUpdate() {
  // Preserve the frame budget while an interactive edit is still active.
  g_update_frames.store(kForceUpdateFrames, std::memory_order_release);
}

void application::Reset() {
  g_interactive_edit_active.store(false, std::memory_order_release);
  g_interactive_edit_heartbeat_ms.store(0, std::memory_order_release);
  g_preview_blocked_until_ms.store(0, std::memory_order_release);

  ResetWeaponRuntime();
  ResetAgentRuntime();
  glove_application::Reset();
}

std::vector<std::uint16_t> application::GetOwnedWeaponDefinitionIndices() {
  std::vector<std::uint16_t> result;

  if (!engine::LocalPawn() || !addresses::globals::game_entity_system)
    return result;

  engine::WeaponServices *weapon_services =
      engine::LocalPawn()->MPWeaponServices();

  if (!weapon_services) return result;

  const auto append_weapon = [&](const engine::ItemHandle &handle) {
    engine::Weapon *weapon = GetWeaponFromHandle(handle);

    if (!weapon) return;

    engine::ItemView *item = GetWeaponItem(weapon);

    if (!item) return;

    const std::uint16_t definition_index = item->MIItemDefinitionIndex();

    if (knife_application::IsKnifeDefinition(definition_index)) return;

    const EconItemSystem::ItemDef *item_info =
        g_econ_item_system.FindDef(definition_index);

    if (!item_info || item_info->category != EconItemSystem::ItemCategory::kGun)
      return;

    if (std::find(result.begin(), result.end(), definition_index) ==
        result.end())
      result.emplace_back(definition_index);
  };

  const engine::ItemHandle active_weapon = weapon_services->MHActiveWeapon();

  if (active_weapon.Valid()) append_weapon(active_weapon);

  auto &weapons = weapon_services->MHMyWeapons();

  for (int i = 0; i < weapons.Count(); ++i) {
    const engine::ItemHandle handle = weapons.Element(i);

    if (handle.Valid() && handle != active_weapon) append_weapon(handle);
  }

  return result;
}

}  // namespace features::changer
