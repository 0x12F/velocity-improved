module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>

#include "core/systems/schema_macros.h"
#include "protection/address_macros.h"

module velocity;

import <cstddef>;
import <filesystem>;
import <string_view>;
import <type_traits>;

namespace systems {

void Local::Update() {
  const auto local_player_controller =
      memory::Read<std::uintptr_t>(addresses::globals::local_player_controller);
  if (!local_player_controller) {
    this->Reset();
    return;
  }

  const auto pawn_handle = memory::Read<std::uint32_t>(
      local_player_controller +
      SCHEMA("CBasePlayerController", "m_hPawn"_hash));
  if (!pawn_handle) {
    this->Reset();
    return;
  }

  const auto pawn = g_entities.Lookup(pawn_handle);
  if (!pawn) {
    this->Reset();
    return;
  }

  Snapshot s{};
  s.controller = local_player_controller;
  s.pawn = pawn;
  s.team = memory::Read<int>(pawn + SCHEMA("C_BaseEntity", "m_iTeamNum"_hash));

  const auto health =
      memory::Read<int>(pawn + SCHEMA("C_BaseEntity", "m_iHealth"_hash));
  const auto life_state = memory::Read<std::uint8_t>(
      pawn + SCHEMA("C_BaseEntity", "m_lifeState"_hash));
  s.is_alive = health > 0 && life_state == 0;

  if (s.is_alive) {
    s.view_team = s.team;
  } else {
    const auto observer_pawn_handle = memory::Read<std::uint32_t>(
        local_player_controller +
        SCHEMA("CCSPlayerController", "m_hObserverPawn"_hash));
    if (observer_pawn_handle) {
      const auto observer_pawn = g_entities.Lookup(observer_pawn_handle);
      if (observer_pawn) {
        const auto observer_services = memory::Read<std::uintptr_t>(
            observer_pawn +
            SCHEMA("C_BasePlayerPawn", "m_pObserverServices"_hash));
        if (observer_services) {
          const auto observer_target_handle = memory::Read<std::uint32_t>(
              observer_services +
              SCHEMA("CPlayer_ObserverServices", "m_hObserverTarget"_hash));
          if (observer_target_handle) {
            const auto observer_target =
                g_entities.Lookup(observer_target_handle);
            if (observer_target) {
              s.observer_pawn = observer_target;
              s.view_team = memory::Read<int>(
                  observer_target + SCHEMA("C_BaseEntity", "m_iTeamNum"_hash));

              const auto observer_target_controller_handle =
                  memory::Read<std::uint32_t>(
                      observer_target +
                      SCHEMA("C_BasePlayerPawn", "m_hController"_hash));
              if (observer_target_controller_handle) {
                const auto observer_target_controller =
                    g_entities.Lookup(observer_target_controller_handle);
                if (observer_target_controller) {
                  s.observer_controller = observer_target_controller;
                }
              }
            }
          }
        }
      }
    }

    if (!s.observer_pawn) {
      s.view_team = s.team;
    }
  }

  {
    const auto game_type = CONVAR("game_type")->Get<int>();
    const auto game_mode = CONVAR("game_mode")->Get<int>();
    const auto is_ffa = (game_type == 1 && game_mode == 2) ||
                        (game_type == 2 && game_mode == 0);

    s.is_team_mode = !is_ffa;

    this->m_is_deathmatch_.store(game_type == 1 && game_mode == 2);
  }

  {
    const auto game_rules =
        memory::Read<std::uintptr_t>(addresses::globals::game_rules);
    const auto global_vars =
        memory::Read<std::uintptr_t>(addresses::globals::global_vars);

    auto cinematic{false};
    auto freezetime{false};

    if (game_rules && global_vars) {
      if (memory::Read<bool>(game_rules + SCHEMA("C_CSGameRules",
                                                 "m_bTeamIntroPeriod"_hash))) {
        cinematic = true;
      } else if (memory::Read<int>(game_rules + SCHEMA("C_CSGameRules",
                                                       "m_gamePhase"_hash)) >=
                 4) {
        cinematic = true;
      }

      if (memory::Read<bool>(game_rules +
                             SCHEMA("C_CSGameRules", "m_bFreezePeriod"_hash))) {
        freezetime = true;
      } else {
        const auto round_start_time = memory::Read<float>(
            game_rules + SCHEMA("C_CSGameRules", "m_fRoundStartTime"_hash));
        const auto current_time = memory::Read<float>(global_vars + 0x30);

        if (round_start_time > current_time) {
          freezetime = true;
        }
      }
    }

    this->m_is_in_cinematic_.store(cinematic);
    this->m_is_in_time_freeze_.store(freezetime);
  }

  {
    std::unique_lock lock(this->m_mtx_);
    this->m_snapshot_ = s;
  }
}

void Local::Reset() {
  {
    std::unique_lock lock(this->m_mtx_);
    this->m_snapshot_ = {};
  }

  this->m_is_deathmatch_.store(false);
  this->m_is_in_cinematic_.store(false);
  this->m_is_in_time_freeze_.store(false);
}

}  // namespace systems
