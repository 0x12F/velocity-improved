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

namespace features::esp::player {

bool Glow::OnIsGlowing(std::uintptr_t owner_entity,
                       std::uint32_t owner_hash) const {
  if (owner_hash != "C_CSPlayerPawn"_hash) {
    return false;
  }

  const auto& cfg = settings::g_esp.m_player.m_glow;
  if (!cfg.enemy.enabled.value && !cfg.enemy_ragdoll.enabled.value &&
      !cfg.team.enabled.value && !cfg.team_ragdoll.enabled.value &&
      !cfg.local.enabled.value && !cfg.local_ragdoll.enabled.value) {
    return false;
  }

  const auto local = systems::g_local.Get();
  const auto team = memory::Read<int>(
      owner_entity + SCHEMA("C_BaseEntity", "m_iTeamNum"_hash));
  const auto health = memory::Read<int>(
      owner_entity + SCHEMA("C_BaseEntity", "m_iHealth"_hash));

  const auto is_other_team = local.IsThisOtherTeam(team);
  const auto is_local = owner_entity == local.ViewPawn();
  const auto is_dead = health <= 0;

  if (is_dead) {
    if (is_local) {
      return cfg.local_ragdoll.enabled.value;
    }

    if (is_other_team) {
      return cfg.enemy_ragdoll.enabled.value;
    }

    return cfg.team_ragdoll.enabled.value;
  }

  if (is_local && settings::g_misc.m_camera.thirdperson.value) {
    return cfg.local.enabled.value;
  }

  if (is_other_team) {
    return cfg.enemy.enabled.value;
  }

  return cfg.team.enabled.value;
}

bool Glow::OnGetGlowColor(std::uintptr_t owner_entity, std::uint32_t owner_hash,
                          float* color) const {
  if (owner_hash != "C_CSPlayerPawn"_hash) {
    return false;
  }

  const auto& cfg = settings::g_esp.m_player.m_glow;
  if (!cfg.enemy.enabled.value && !cfg.enemy_ragdoll.enabled.value &&
      !cfg.team.enabled.value && !cfg.team_ragdoll.enabled.value &&
      !cfg.local.enabled.value && !cfg.local_ragdoll.enabled.value) {
    return false;
  }

  const auto local = systems::g_local.Get();
  const auto team = memory::Read<int>(
      owner_entity + SCHEMA("C_BaseEntity", "m_iTeamNum"_hash));
  const auto health = memory::Read<int>(
      owner_entity + SCHEMA("C_BaseEntity", "m_iHealth"_hash));

  const auto is_other_team = local.IsThisOtherTeam(team);
  const auto is_local = owner_entity == local.ViewPawn();
  const auto is_dead = health <= 0;

  const settings::Esp::GlowTarget* target{nullptr};

  if (is_dead) {
    if (is_local) {
      target = &cfg.local_ragdoll;
    } else if (is_other_team) {
      target = &cfg.enemy_ragdoll;
    } else {
      target = &cfg.team_ragdoll;
    }
  } else {
    if (is_local) {
      if (settings::g_misc.m_camera.thirdperson.value) {
        target = &cfg.local;
      }
    } else if (is_other_team) {
      target = &cfg.enemy;
    } else {
      target = &cfg.team;
    }
  }

  if (!target || !target->enabled.value) {
    return false;
  }

  color[0] = target->color.value.r / 255.0f;
  color[1] = target->color.value.g / 255.0f;
  color[2] = target->color.value.b / 255.0f;
  color[3] = target->color.value.a / 255.0f;

  return true;
}

}  // namespace features::esp::player
