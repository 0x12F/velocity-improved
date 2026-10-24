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

bool Chams::OnGeneratePrimitives(
    std::uintptr_t owner_entity, std::uint32_t owner_hash,
    std::uintptr_t scene_object, std::uintptr_t primitive_buffer,
    void(__fastcall* original_fn)(std::uintptr_t, std::uintptr_t,
                                  std::uintptr_t, std::uintptr_t),
    std::uintptr_t a1, std::uintptr_t scene_view) {
  const auto& chams_cfg = settings::g_esp.m_item.m_chams;
  if (!chams_cfg.enabled.value) {
    return false;
  }

  const auto group_id = this->GetItemGroup(owner_hash);
  if (group_id == UINT32_MAX) {
    return false;
  }

  const auto owner_handle = memory::Read<std::uint32_t>(
      owner_entity + SCHEMA("C_BaseEntity", "m_hOwnerEntity"_hash));
  if (owner_handle && owner_handle != 0xffffffff) {
    return false;
  }

  const auto game_scene_node = memory::Read<std::uintptr_t>(
      owner_entity + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
  if (game_scene_node) {
    const auto parent_node = memory::Read<std::uintptr_t>(
        game_scene_node + SCHEMA("CGameSceneNode", "m_pParent"_hash));
    if (parent_node) {
      return false;
    }
  }

  const auto& cfg = chams_cfg.GetGroup(group_id);
  if (!cfg.enabled.value) {
    return false;
  }

  if (!cfg.primary.enabled.value && !cfg.secondary.enabled.value) {
    return false;
  }

  {
    const auto flags = memory::SafeRead<std::uint8_t>(scene_object + 0x78);
    if (flags) {
      (void)memory::SafeWrite<std::uint8_t>(
          scene_object + 0x78, static_cast<std::uint8_t>(*flags & ~(1u << 3)));
    }
  }

  if (cfg.secondary.enabled.value) {
    this->ApplyLayer(primitive_buffer, original_fn, a1, scene_object,
                     scene_view, cfg.secondary.color, cfg.secondary.material);
  }

  if (cfg.primary.enabled.value) {
    this->ApplyLayer(primitive_buffer, original_fn, a1, scene_object,
                     scene_view, cfg.primary.color, cfg.primary.material);
  }

  return true;
}

void Chams::ApplyLayer(
    std::uintptr_t primitive_buffer,
    void(__fastcall* original_fn)(std::uintptr_t, std::uintptr_t,
                                  std::uintptr_t, std::uintptr_t),
    std::uintptr_t a1, std::uintptr_t scene_object, std::uintptr_t scene_view,
    const xdraw::color& color, settings::Esp::ChamIds material_id) {
  const auto before = detail::ReadPrimitiveBuffer(primitive_buffer);
  const auto prev_count = before ? before->Count() : -1;

  original_fn(a1, scene_object, scene_view, primitive_buffer);

  const auto after = detail::ReadPrimitiveBuffer(primitive_buffer);
  const auto new_count = after ? after->Count() : -1;
  if (!after || prev_count < 0 || prev_count >= new_count) {
    return;
  }

  const auto material = systems::Materials::Find(material_id);
  if (!material) {
    return;
  }

  for (auto i = prev_count; i < new_count; ++i) {
    detail::ReplacePrimitive(after->At(i), material, color);
  }
}

std::uint32_t Chams::GetItemGroup(std::uint32_t schema_hash) {
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
