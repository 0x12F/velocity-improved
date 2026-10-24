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
import <type_traits>;

namespace systems::model_preview {
bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context) {
  return systems::preview_panorama::Initialize(device, context);
}

void Shutdown() { systems::preview_panorama::Shutdown(); }

void ProcessMainThread() {
  systems::preview_item::ProcessPending();
  systems::preview_panorama::ProcessMainThread();
}

void TickRenderThread() { systems::preview_panorama::TickRenderThread(); }

void Submit(const Request& request) {
  systems::preview_panorama::Request(request);
}

void KeepAlive() { systems::preview_panorama::KeepAlive(); }

void Close() { systems::preview_panorama::Close(); }

void SetRotation(float rotation_x, float rotation_y) {
  systems::preview_panorama::SetRotation(rotation_x, rotation_y);
}

Frame GetFrame() { return systems::preview_panorama::GetFrame(); }

Status GetStatus() { return systems::preview_panorama::GetStatus(); }

const char* GetStatusText() {
  return systems::preview_panorama::GetStatusText();
}

Request MakeWeaponRequest(
    std::uint16_t definition_index,
    const features::changer::cosmetic_config::ItemSkinT& skin) {
  Request request{};
  request.presentation = Presentation::kWeapon;
  request.definition_index = definition_index;
  request.paint_kit = skin.paint_kit;
  request.seed = skin.seed;
  request.wear = skin.wear;
  request.stat_trak_enabled = skin.stat_trak_enabled;
  request.stat_trak_kills = skin.stat_trak_kills;
  request.custom_name = skin.custom_name;

  if (const features::changer::EconItemSystem::PaintKit* paint_kit =
          features::changer::g_econ_item_system.FindPaintKit(skin.paint_kit))
    request.rarity = paint_kit->rarity;

  for (std::size_t i = 0; i < request.stickers.size(); ++i) {
    const features::changer::cosmetic_config::SkinStickerT& source =
        skin.stickers[i];
    Sticker& destination = request.stickers[i];

    destination.enabled = source.enabled;
    destination.kit = source.kit;
    destination.wear = source.wear;
    destination.scale = source.scale;
    destination.rotation = source.rotation;
    destination.offset_x = source.offset_x;
    destination.offset_y = source.offset_y;
  }

  request.keychain.enabled = skin.keychain.enabled;
  request.keychain.id = skin.keychain.id;
  request.keychain.seed = skin.keychain.seed;
  request.keychain.offset_x = skin.keychain.offset_x;
  request.keychain.offset_y = skin.keychain.offset_y;
  request.keychain.offset_z = skin.keychain.offset_z;

  return request;
}

Request MakeAgentRequest(std::uint16_t definition_index) {
  Request request{};
  request.presentation = Presentation::kPlayer;
  request.definition_index = definition_index;

  if (const features::changer::EconItemSystem::ItemDef* item =
          features::changer::g_econ_item_system.FindDef(definition_index)) {
    request.model_path = item->model_player;
    request.counter_terrorist = item->Team() == 3;
  }

  return request;
}
}  // namespace systems::model_preview
