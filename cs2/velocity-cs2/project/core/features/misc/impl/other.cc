module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>
#include <external/xdraw/xdraw.hpp>

#include "core/systems/schema_macros.h"
#include "protection/address_macros.h"

module velocity;

import <cstddef>;
import <filesystem>;
import <limits>;
import <string_view>;
import <type_traits>;

namespace features::misc {
namespace {

[[nodiscard]] std::string ControllerName(std::uintptr_t controller) {
  if (!controller) {
    return {};
  }

  const auto name_ptr = memory::SafeRead<std::uintptr_t>(
                            controller + SCHEMA("CCSPlayerController",
                                                "m_sSanitizedPlayerName"_hash))
                            .value_or(0);
  return memory::ReadString(name_ptr, 127);
}

void SubmitNameChange(const std::string& display_name) {
  if (display_name.empty()) {
    return;
  }

  Other::s_display_name_ = display_name;
  Other::s_name_change_pending_ = true;
  memory::Call<void>(PATTERN(patterns::engine_client_cmd),
                     addresses::globals::source2engine_to_client, 0,
                     xs("setinfo name x"), 0x7ffef001);
  Other::s_name_change_pending_ = false;
}

}  // namespace

void Other::OnRoundStart() { this->DoAutobuy(); }

void Other::OnPlayerDeath(std::uintptr_t event) {
  if (!event) {
    return;
  }

  const auto attacker_key = cstypes::EventHash{0, "attacker"};
  const auto attacker = memory::Call<std::uintptr_t>(
      PATTERN(patterns::game_event_get_controller), event, &attacker_key);
}

void Other::OnFrameStageNotify() {
  this->DoPlayerAlphaChanging();
  this->DoRevealRadar();
  this->DoNameChanging();
  this->DoViewmodelAdjust();
}

void Other::DoRevealRadar() const {
  if (!settings::g_misc.reveal_radar.value) {
    return;
  }

  const auto local = systems::g_local.Get();
  if (!local.IsValid()) {
    return;
  }

  const auto spotted_state_offset =
      SCHEMA("C_CSPlayerPawn", "m_entitySpottedState"_hash);
  const auto spotted_offset = SCHEMA("EntitySpottedState_t", "m_bSpotted"_hash);

  for (const auto& player :
       systems::g_entities.GetByType(systems::Entities::Type::kPlayer)) {
    const auto controller = player.ptr;
    if (!controller ||
        !memory::Read<bool>(controller + SCHEMA("CCSPlayerController",
                                                "m_bPawnIsAlive"_hash))) {
      continue;
    }

    const auto pawn_handle = memory::Read<std::uint32_t>(
        controller + SCHEMA("CBasePlayerController", "m_hPawn"_hash));
    const auto pawn = systems::g_entities.Lookup(pawn_handle);
    if (!pawn || pawn == local.ViewPawn()) {
      continue;
    }

    const auto team =
        memory::Read<int>(pawn + SCHEMA("C_BaseEntity", "m_iTeamNum"_hash));
    if (!local.IsThisOtherTeam(team)) {
      continue;
    }

    memory::Write<bool>(pawn + spotted_state_offset + spotted_offset, true);
  }
}

void Other::DoAutobuy() const {
  if (!settings::g_misc.m_autobuy.enabled) {
    return;
  }

  std::string cmd{};

  switch (settings::g_misc.m_autobuy.primary_weapon) {
    case 1:
      cmd += xs("buy ak47; buy m4a1; ");
      break;
    case 2:
      cmd += xs("buy sg556; buy aug; ");
      break;
    case 3:
      cmd += xs("buy ssg08; ");
      break;
    case 4:
      cmd += xs("buy awp; ");
      break;
    case 5:
      cmd += xs("buy g3sg1; buy scar20; ");
      break;
  }

  if (settings::g_misc.m_autobuy.armor) {
    cmd += xs("buy vesthelm; buy vest; ");
  }

  if (settings::g_misc.m_autobuy.taser) {
    cmd += xs("buy taser; ");
  }

  if (settings::g_misc.m_autobuy.defuser) {
    cmd += xs("buy defuser; ");
  }

  switch (settings::g_misc.m_autobuy.secondary_weapon) {
    case 1:
      cmd += xs("buy elite; ");
      break;
    case 2:
      cmd += xs("buy fiveseven; buy tec9; ");
      break;
    case 3:
      cmd += xs("buy deagle; ");
      break;
    case 4:
      cmd += xs("buy revolver; ");
      break;
  }

  for (auto i = 0; i < 5; ++i) {
    if (!settings::g_misc.m_autobuy.grenades[i]) {
      continue;
    }

    switch (i) {
      case 0:
        cmd += xs("buy molotov; buy incgrenade; ");
        break;
      case 1:
        cmd += xs("buy hegrenade; ");
        break;
      case 2:
        cmd += xs("buy smokegrenade; ");
        break;
      case 3:
        cmd += xs("buy flashbang; ");
        break;
      case 4:
        cmd += xs("buy decoy; ");
        break;
    }
  }

  if (!cmd.empty()) {
    memory::Call<void>(PATTERN(patterns::engine_client_cmd),
                       addresses::globals::source2engine_to_client, 0,
                       cmd.c_str(), 0x7ffef001);
  }
}

void Other::DoPlayerAlphaChanging() {
  const auto local = systems::g_local.Get();

  if (!local.pawn || !local.is_alive) {
    if (this->m_is_alpha_changed_ && local.pawn) {
      memory::Call<void>(PATTERN(patterns::game_event_get_string), local.pawn,
                         255);
    }

    this->m_is_alpha_changed_ = false;
    return;
  }

  if (!settings::g_esp.m_local_alpha.enabled.value) {
    if (this->m_is_alpha_changed_) {
      this->m_is_alpha_changed_ = false;
      memory::Call<void>(PATTERN(patterns::game_event_get_string), local.pawn,
                         255);
    }

    return;
  }

  const auto is_scoped = memory::Read<bool>(
      local.pawn + SCHEMA("C_CSPlayerPawn", "m_bIsScoped"_hash));
  const auto should_apply =
      !settings::g_esp.m_local_alpha.only_scoped.value || is_scoped;

  if (should_apply) {
    this->m_is_alpha_changed_ = true;
    const auto alpha = static_cast<std::uint8_t>(
        settings::g_esp.m_local_alpha.opacity.value * 255.0f);
    memory::Call<void>(PATTERN(patterns::game_event_get_string), local.pawn,
                       alpha);
  } else {
    if (this->m_is_alpha_changed_) {
      this->m_is_alpha_changed_ = false;
      memory::Call<void>(PATTERN(patterns::game_event_get_string), local.pawn,
                         255);
    }
  }
}

void Other::DoNameChanging() {
  const auto local = systems::g_local.Get();
  const auto& cfg = settings::g_misc.m_name_changer;
  const auto enabled = cfg.clantag.value || cfg.override_name.value;

  if (!enabled) {
    if (this->m_name_changer_active_ && local.controller &&
        !this->m_original_name_.empty()) {
      SubmitNameChange(this->m_original_name_);
    }

    this->m_name_changer_active_ = false;
    this->m_name_changer_controller_ = 0;
    this->m_original_name_.clear();
    this->m_last_sent_name_.clear();
    Other::s_display_name_.clear();
    return;
  }

  if (!local.controller) {
    return;
  }

  if (!this->m_name_changer_active_) {
    this->m_original_name_ = ControllerName(local.controller);
    if (this->m_original_name_.empty()) {
      this->m_original_name_ = xs("Player");
    }

    this->m_name_changer_active_ = true;
    this->m_name_changer_controller_ = local.controller;
    this->m_last_sent_name_.clear();
  } else if (this->m_name_changer_controller_ != local.controller) {
    // Keep the captured real name across map loads, where the controller may be
    // recreated.
    this->m_name_changer_controller_ = local.controller;
    this->m_last_sent_name_.clear();
  }

  const auto& configured_name = cfg.name.value;
  const auto& base_name = cfg.override_name.value && !configured_name.empty()
                              ? configured_name
                              : this->m_original_name_;

  std::string display_name = base_name;
  if (cfg.clantag.value) {
    constexpr std::string_view kTag{"velocity"};
    constexpr auto kTicksPerStep{
        16};  // 0.25 seconds at CS2's 64-tick interval.
    constexpr auto kPhaseCount{static_cast<int>(kTag.size() * 2)};

    const auto global_vars =
        memory::SafeRead<std::uintptr_t>(addresses::globals::global_vars)
            .value_or(0);
    const auto current_tick =
        global_vars ? memory::SafeRead<int>(global_vars + 0x44).value_or(0) : 0;
    auto phase = current_tick / kTicksPerStep % kPhaseCount;
    if (phase < 0) {
      phase += kPhaseCount;
    }

    const auto reveal_index =
        phase <= static_cast<int>(kTag.size()) ? phase : kPhaseCount - phase;
    const auto visible_tag =
        kTag.substr(0, static_cast<std::size_t>(reveal_index));
    if (!visible_tag.empty()) {
      display_name.reserve(base_name.size() + visible_tag.size() + 3);
      display_name = "[";
      display_name += visible_tag;
      display_name += "] ";
      display_name += base_name;
    }
  }
  if (display_name == this->m_last_sent_name_) {
    return;
  }

  SubmitNameChange(display_name);
  this->m_last_sent_name_ = std::move(display_name);
}

void Other::DoKillFeedPreservation() {
  const auto local = systems::g_local.Get();

  if (!local.pawn || !local.is_alive) {
    return;
  }

  const auto hud_element = memory::Call<std::uintptr_t>(
      PATTERN(patterns::find_hud_element), xs("CCSGO_HudDeathNotice"));
  if (!hud_element) {
    return;
  }

  memory::Write<float>(hud_element + 0x58,
                       settings::g_misc.preserve_killfeed ? 1000.0f : 1.5f);

  float spawntime = memory::Read<float>(
      local.pawn + SCHEMA("C_CSPlayerPawnBase", "m_flLastSpawnTimeIndex"_hash));
  if (m_last_spawntime_ != spawntime) {
    const auto clear_death_notices = PATTERN(patterns::hud_death_notice_clear);
    if (clear_death_notices) {
      memory::Call<void>(clear_death_notices, hud_element - 0x20);
    }

    m_last_spawntime_ = spawntime;
  }
}

void Other::DoViewmodelAdjust() {
  const auto& cfg = settings::g_misc.m_viewmodel_adjust;
  if (!cfg.enabled.value) {
    this->m_cached_vm_x_ = std::numeric_limits<float>::quiet_NaN();
    this->m_cached_vm_y_ = std::numeric_limits<float>::quiet_NaN();
    this->m_cached_vm_z_ = std::numeric_limits<float>::quiet_NaN();
    this->m_cached_vm_fov_ = std::numeric_limits<float>::quiet_NaN();
    return;
  }

  const auto set_float_cvar = [](std::uint32_t hash, float value) {
    addresses::globals::cvar->Find(hash)->m_value.fl = value;
  };

  if (cfg.offset_x.value != this->m_cached_vm_x_) {
    set_float_cvar("viewmodel_offset_x"_hash, cfg.offset_x.value);
    this->m_cached_vm_x_ = cfg.offset_x.value;
  }

  if (cfg.offset_y.value != this->m_cached_vm_y_) {
    set_float_cvar("viewmodel_offset_y"_hash, cfg.offset_y.value);
    this->m_cached_vm_y_ = cfg.offset_y.value;
  }

  if (cfg.offset_z.value != this->m_cached_vm_z_) {
    set_float_cvar("viewmodel_offset_z"_hash, cfg.offset_z.value);
    this->m_cached_vm_z_ = cfg.offset_z.value;
  }

  if (cfg.fov.value != this->m_cached_vm_fov_) {
    set_float_cvar("viewmodel_fov"_hash, cfg.fov.value);
    this->m_cached_vm_fov_ = cfg.fov.value;
  }
}

}  // namespace features::misc
