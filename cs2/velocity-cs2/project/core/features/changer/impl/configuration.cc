module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>
#include <external/xdraw/xdraw.hpp>

module velocity;

import <cstddef>;
import <filesystem>;
import <limits>;
import <string_view>;

namespace features::changer {

features::changer::cosmetic_config::ItemSkinT MakeSkinConfiguration(
    const settings::Changer::AppliedSkin& skin) {
  features::changer::cosmetic_config::ItemSkinT result;
  result.enabled = true;
  result.paint_kit = skin.paint_kit_id;
  result.seed = skin.seed;
  result.wear = skin.wear;
  result.stat_trak_enabled = skin.stattrak;
  result.stat_trak_kills = skin.stattrak_kills;
  result.custom_name = skin.custom_name;
  result.stickers = skin.stickers;
  result.keychain = skin.keychain;
  return result;
}

void PublishConfiguration() {
  // UI and preset loads live on the render thread. The backend never reads
  // these mutable maps directly from FrameStageNotify.
  static std::unordered_map<std::int16_t, settings::Changer::AppliedSkin>
      previous;
  static int previous_ct = -1;
  static int previous_t = -1;
  static bool previous_enabled{};
  const auto& settings = settings::g_changer;
  const auto& skins = settings.skins.data;
  if (previous == skins && previous_ct == settings.agents.ct_def &&
      previous_t == settings.agents.t_def &&
      previous_enabled == settings.enabled.value)
    return;

  features::changer::cosmetic_config::SkinChangerT next;
  next.enabled = settings.enabled.value;
  for (const auto& [definition, skin] : skins) {
    const auto* item = g_econ_item_system.FindDef(definition);
    if (!item) continue;
    auto converted = MakeSkinConfiguration(skin);
    switch (item->category) {
      case EconItemSystem::ItemCategory::kGun:
        next.weapons.emplace(definition, std::move(converted));
        break;
      case EconItemSystem::ItemCategory::kNife:
        next.knives.emplace(definition, std::move(converted));
        next.knife_definition = definition;
        break;
      case EconItemSystem::ItemCategory::kGlove:
        next.gloves.emplace(definition, std::move(converted));
        next.glove_definition = definition;
        break;
      default:
        break;
    }
  }
  runtime::Configure(std::move(next), settings.agents.ct_def,
                     settings.agents.t_def);
  previous = skins;
  previous_ct = settings.agents.ct_def;
  previous_t = settings.agents.t_def;
  previous_enabled = settings.enabled.value;
}
}  // namespace features::changer
