module;

#include "platform/platform.h"

#include <Windows.h>

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>
#include <external/xdraw/xdraw.hpp>

#include "protection/address_macros.h"

module velocity;

import <algorithm>;
import <atomic>;
import <cstddef>;
import <cstdint>;
import <filesystem>;
import <limits>;
import <string_view>;
import <type_traits>;

namespace features::changer {
namespace {
constexpr std::uint16_t kDefaultTGloves = 5028;
constexpr std::uint16_t kDefaultCtGloves = 5029;

constexpr std::uint8_t kTeamT = 2;
constexpr std::uint8_t kTeamCt = 3;
using CreatePaintKitFn = engine::GeneratedPaintKit *(__fastcall *)(void *);
using SetBodyGroupFn = void(__fastcall *)(void *, const char *, unsigned int);

std::atomic_bool g_force_update{true};

struct GloveState {
  engine::PlayerItems *pawn = nullptr;
  std::uint16_t definition = 0;
  int paint_kit = -1;
  int seed = -1;
  float wear = -1.0f;
  float spawn_time = -1.0f;
  std::uint8_t team = 0xFF;

  bool operator==(const GloveState &) const = default;
};

GloveState g_last_applied;
bool g_applied_custom_gloves = false;

CreatePaintKitFn GetCreatePaintKit() {
  return engine::CachedAddress([] {
    return reinterpret_cast<CreatePaintKitFn>(
        PATTERN(patterns::glove_create_paint_kit));
  });
}

SetBodyGroupFn GetSetBodyGroup() {
  return engine::CachedAddress([] {
    return reinterpret_cast<SetBodyGroupFn>(
        engine::RelativeAddress(reinterpret_cast<std::uint8_t *>(
                                    PATTERN(patterns::glove_set_body_group)),
                                1));
  });
}

std::uint16_t GetDefaultGloveDefinition(std::uint8_t team) {
  if (team == kTeamT) return kDefaultTGloves;

  if (team == kTeamCt) return kDefaultCtGloves;

  return 0;
}

bool IsDefaultGloveDefinition(std::uint16_t definition_index) {
  return definition_index == kDefaultTGloves ||
         definition_index == kDefaultCtGloves;
}

bool UpdateGloveVisuals(engine::PlayerItems *pawn, engine::ItemView *item,
                        CreatePaintKitFn create_paint_kit,
                        SetBodyGroupFn set_body_group, int paint_kit) {
  // Preserve recovery for native model/material calls in one boundary;
  // ApplyGloves restores item initialization on either success or failure.
  __try {
    if (create_paint_kit) {
      if (auto *generated = create_paint_kit(item)) {
        if (const auto *info = g_econ_item_system.FindPaintKit(paint_kit))
          generated->m_name = info->name.c_str();
      }
    }

    set_body_group(pawn, "first_or_third_person", 1);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool ApplyGloves(engine::PlayerItems *pawn, std::uint16_t definition_index,
                 int paint_kit, int seed, float wear) {
  if (!pawn || definition_index == 0) return false;

  engine::ItemView *item = pawn->MEconGloves();

  if (!item) return false;

  const SetBodyGroupFn set_body_group = GetSetBodyGroup();

  if (!set_body_group) return false;

  CreatePaintKitFn create_paint_kit = nullptr;

  if (paint_kit > 0) {
    create_paint_kit = GetCreatePaintKit();

    if (!create_paint_kit) return false;
  }

  item->MBInitialized() = false;

  attributes::Remove(item);

  item->MIItemDefinitionIndex() = definition_index;
  item->MBDisallowSoc() = false;
  item->MBRestoreCustomMaterialAfterPrecache() = true;
  item->MIItemIdHigh() =
      IsDefaultGloveDefinition(definition_index) ? 0u : 0xFFFFFFFFu;

  if (paint_kit > 0) attributes::Create(item, paint_kit, wear, seed, -1, 0);

  const bool updated = UpdateGloveVisuals(pawn, item, create_paint_kit,
                                          set_body_group, paint_kit);
  item->MBInitialized() = true;
  if (!updated) return false;

  pawn->MBNeedToReApplyGloves() = true;

  return true;
}

}  // namespace

void glove_application::Run(engine::PlayerItems *local_pawn) {
  if (!local_pawn || local_pawn->MIHealth() <= 0) return;

  // Keep glove/model material regeneration consistent with weapons: continuous
  // menu drags commit once on release instead of rebuilding every frame.
  if (application::IsInteractiveEditActive()) return;

  const std::uint8_t team = local_pawn->MITeamNum();
  const float spawn_time = local_pawn->MFlLastSpawnTimeIndex();

  std::uint16_t definition_index =
      cosmetic_config::skin_changer.glove_definition;

  int paint_kit = 0;
  int seed = 0;
  float wear = 0.0001f;

  bool custom_selection =
      cosmetic_config::skin_changer.enabled && definition_index != 0;

  if (custom_selection) {
    const EconItemSystem::ItemDef *item_info =
        g_econ_item_system.FindDef(definition_index);

    if (!item_info ||
        item_info->category != EconItemSystem::ItemCategory::kGlove)
      custom_selection = false;
  }

  if (custom_selection) {
    const auto config_it =
        cosmetic_config::skin_changer.gloves.find(definition_index);

    if (config_it != cosmetic_config::skin_changer.gloves.end()) {
      const cosmetic_config::ItemSkinT &config = config_it->second;

      if (config.enabled && config.paint_kit > 0) {
        paint_kit = config.paint_kit;
        seed = (std::clamp)(config.seed, 0, 1000);
        wear = (std::clamp)(config.wear, 0.0001f, 1.0f);
      }
    }
  } else {
    if (!g_applied_custom_gloves) return;

    definition_index = GetDefaultGloveDefinition(team);

    if (definition_index == 0) return;
  }

  const GloveState desired{local_pawn, definition_index, paint_kit, seed,
                           wear,       spawn_time,       team};
  if (desired == g_last_applied &&
      !g_force_update.load(std::memory_order_acquire))
    return;

  if (!ApplyGloves(local_pawn, definition_index, paint_kit, seed, wear)) return;

  g_last_applied = desired;
  g_applied_custom_gloves = custom_selection;

  g_force_update.store(false, std::memory_order_release);
}

void glove_application::ForceUpdate() {
  g_force_update.store(true, std::memory_order_release);
}

void glove_application::Reset() {
  g_force_update.store(true, std::memory_order_release);
  g_last_applied = {};
  g_applied_custom_gloves = false;
}

}  // namespace features::changer
