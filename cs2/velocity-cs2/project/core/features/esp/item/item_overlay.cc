module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>
#include <external/xdraw/xdraw.hpp>

#include "core/systems/schema_macros.h"

module velocity;

import <cstddef>;
import <filesystem>;
import <limits>;
import <string_view>;
import <type_traits>;

namespace features::esp::item {

void Overlay::OnRender(xdraw::draw_list& draw_list) {
  const auto& overlay_cfg = settings::g_esp.m_item.m_overlay;
  if (!overlay_cfg.enabled.value) {
    return;
  }

  for (const auto& item :
       systems::g_entities.GetByType(systems::Entities::Type::kItem)) {
    const auto info = this->GetInfo(item);
    if (!info.Valid()) {
      continue;
    }

    const auto group_id = this->GetItemGroup(info.schema_hash);

    if (!overlay_cfg.IsActive(group_id) ||
        info.distance > overlay_cfg.GetGroup(group_id).max_distance) {
      continue;
    }

    this->AddLabel(draw_list, info, overlay_cfg.GetGroup(group_id));
  }
}

void Overlay::AddLabel(xdraw::draw_list& draw_list, const Info& info,
                       const settings::Esp::Item::Overlay::Group& cfg) {
  const auto screen = systems::g_view.Project(info.origin);
  if (!systems::g_view.ProjectionValid(screen)) {
    return;
  }

  const auto show_icon =
      cfg.display == settings::Esp::Item::Overlay::Group::DisplayType::kIcon ||
      cfg.display ==
          settings::Esp::Item::Overlay::Group::DisplayType::kTextAndIcon;
  const auto show_text =
      cfg.display == settings::Esp::Item::Overlay::Group::DisplayType::kText ||
      cfg.display ==
          settings::Esp::Item::Overlay::Group::DisplayType::kTextAndIcon;
  auto y = screen.y;

  if (show_icon) {
    const auto ico = systems::g_icons.Get(info.schema_hash, 0.35f);
    if (ico && ico->texture) {
      const auto iw = static_cast<float>(ico->width);
      const auto ih = static_cast<float>(ico->height);
      const auto ix = std::floorf(screen.x - iw * 0.5f);
      const auto iy = std::floorf(y);

      constexpr auto kOutline{xdraw::color{0, 0, 0, 255}};

      draw_list.image(ix - 1.0f, iy, iw, ih, ico->texture.Get(), kOutline);
      draw_list.image(ix + 1.0f, iy, iw, ih, ico->texture.Get(), kOutline);
      draw_list.image(ix, iy - 1.0f, iw, ih, ico->texture.Get(), kOutline);
      draw_list.image(ix, iy + 1.0f, iw, ih, ico->texture.Get(), kOutline);
      draw_list.image(ix, iy, iw, ih, ico->texture.Get(), cfg.icon_color);

      y += ih + 1.0f;
    }
  }

  if (show_text) {
    xdraw::push_font(
        rendering::g_fonts.smallest_pixel7[rendering::Fonts::Size::kNormal]);

    const auto [w, h] = xdraw::measure_text(info.name);
    draw_list.text(std::floorf(screen.x - w * 0.5f), std::floorf(y), info.name,
                   cfg.text_color, xdraw::text_style::outlined);

    xdraw::pop_font();
  }
}

Overlay::Info Overlay::GetInfo(const systems::Entities::Cached& entity) {
  Info info{};
  info.entity = entity.ptr;
  info.schema_hash = entity.schema_hash;

  if (!info.entity) {
    return info;
  }

  const auto game_scene_node = memory::Read<std::uintptr_t>(
      info.entity + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
  if (!game_scene_node) {
    return info;
  }

  if (memory::Read<bool>(game_scene_node +
                         SCHEMA("CGameSceneNode", "m_bDormant"_hash))) {
    return info;
  }

  const auto owner_handle = memory::Read<std::uint32_t>(
      info.entity + SCHEMA("C_BaseEntity", "m_hOwnerEntity"_hash));
  if (owner_handle && owner_handle != 0xffffffff) {
    return info;
  }

  info.origin = memory::Read<math::Vector3>(
      game_scene_node + SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_hash));
  info.distance = systems::g_view.Origin().Distance(info.origin) * 0.01905f;
  info.vdata = memory::Read<std::uintptr_t>(
      info.entity + SCHEMA("C_BaseEntity", "m_nSubclassID"_hash) + 0x8);

  if (!info.vdata) {
    return info;
  }

  const auto name_ptr = memory::Read<const char*>(
      info.vdata + SCHEMA("CCSWeaponBaseVData", "m_szName"_hash));
  if (!name_ptr) {
    return info;
  }

  info.name =
      memory::ReadString(reinterpret_cast<std::uintptr_t>(name_ptr), 64);

  if (info.name.starts_with("weapon_")) {
    info.name.erase(0, 7);
  }

  return info;
}

std::uint32_t Overlay::GetItemGroup(std::uint32_t schema_hash) {
  switch (schema_hash) {
    case "C_DEagle"_hash:
    case "C_WeaponElite"_hash:
    case "C_WeaponFiveSeven"_hash:
    case "C_WeaponGlock"_hash:
    case "C_WeaponHKP2000"_hash:
    case "C_WeaponUSPSilencer"_hash:
    case "C_WeaponP250"_hash:
    case "C_WeaponCZ75a"_hash:
    case "C_WeaponTec9"_hash:
    case "C_WeaponRevolver"_hash:
      return 0;

    case "C_WeaponMAC10"_hash:
    case "C_WeaponMP5SD"_hash:
    case "C_WeaponMP7"_hash:
    case "C_WeaponMP9"_hash:
    case "C_WeaponBizon"_hash:
    case "C_WeaponP90"_hash:
    case "C_WeaponUMP45"_hash:
      return 1;

    case "C_AK47"_hash:
    case "C_WeaponM4A1"_hash:
    case "C_WeaponM4A1Silencer"_hash:
    case "C_WeaponAug"_hash:
    case "C_WeaponFamas"_hash:
    case "C_WeaponGalilAR"_hash:
    case "C_WeaponSG556"_hash:
      return 2;

    case "C_WeaponNOVA"_hash:
    case "C_WeaponSawedoff"_hash:
    case "C_WeaponXM1014"_hash:
    case "C_WeaponMag7"_hash:
      return 3;

    case "C_WeaponAWP"_hash:
    case "C_WeaponG3SG1"_hash:
    case "C_WeaponSCAR20"_hash:
    case "C_WeaponSSG08"_hash:
    case "C_WeaponM249"_hash:
    case "C_WeaponNegev"_hash:
      return 4;

    case "C_HEGrenade"_hash:
    case "C_Flashbang"_hash:
    case "C_SmokeGrenade"_hash:
    case "C_MolotovGrenade"_hash:
    case "C_IncendiaryGrenade"_hash:
    case "C_DecoyGrenade"_hash:
    case "C_C4"_hash:
    case "C_WeaponTaser"_hash:
    case "C_Item_Healthshot"_hash:
    case "C_Knife"_hash:
      return 5;

    default:
      return UINT32_MAX;
  }
}

}  // namespace features::esp::item
