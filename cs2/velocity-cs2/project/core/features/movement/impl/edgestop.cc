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

namespace features::movement {

void Edgestop::OnCreateMove(systems::Input::Usercmd* cmd) const {
  if (!settings::g_movement.edgestop.value ||
      settings::g_movement.edgejump.value) {
    return;
  }

  const auto local = systems::g_local.Get();
  if (!local.pawn) {
    return;
  }

  const auto& prestate = systems::g_prediction.Pre();
  if (!(prestate.flags & cstypes::entity_flags::kOnGround)) {
    return;
  }

  const auto velocity = prestate.networked_velocity;
  const auto speed = velocity.Length2d();

  const auto move_type = memory::Read<std::uint8_t>(
      local.pawn + SCHEMA("C_BaseEntity", "m_nActualMoveType"_hash));
  if (move_type == cstypes::move_type::kLadder ||
      move_type == cstypes::move_type::kNoclip) {
    return;
  }

  const auto game_scene_node = memory::Read<std::uintptr_t>(
      local.pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
  if (!game_scene_node) {
    return;
  }

  const auto origin = memory::Read<math::Vector3>(
      game_scene_node + SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_hash));
  const auto movement_services = memory::Read<std::uintptr_t>(
      local.pawn + SCHEMA("C_BasePlayerPawn", "m_pMovementServices"_hash));

  if (!movement_services) {
    return;
  }

  const auto collision =
      local.pawn + SCHEMA("C_BaseModelEntity", "m_Collision"_hash);
  const auto mins = memory::Read<math::Vector3>(
      collision + SCHEMA("CCollisionProperty", "m_vecMins"_hash));
  const auto maxs = memory::Read<math::Vector3>(
      collision + SCHEMA("CCollisionProperty", "m_vecMaxs"_hash));

  const auto filter = systems::g_tracing.MakePlayerMovementFilter(local.pawn);
  const auto sv_standable_normal = CONVAR("sv_standable_normal")->Get<float>();

  const auto check_edge = [&](int ticks_ahead) -> bool {
    auto predicted = origin;
    predicted.x +=
        velocity.x * cstypes::kTickInterval * static_cast<float>(ticks_ahead);
    predicted.y +=
        velocity.y * cstypes::kTickInterval * static_cast<float>(ticks_ahead);

    const auto start =
        math::Vector3{predicted.x, predicted.y, predicted.z + 2.0f};
    const auto end =
        math::Vector3{predicted.x, predicted.y, predicted.z - 4.0f};

    const auto result = systems::g_tracing.TracePlayerBbox(
        start, end, {mins, maxs}, filter, movement_services);
    return result.fraction >= 1.0f || result.normal.z < sv_standable_normal;
  };

  const auto at_edge = check_edge(1);

  auto approaching_edge{false};

  if (!at_edge && speed > 1.0f) {
    const auto sv_friction = CONVAR("sv_friction")->Get<float>();
    const auto sv_stopspeed = CONVAR("sv_stopspeed")->Get<float>();
    const auto ticks_needed = std::max(
        2, static_cast<int>(
               std::ceilf(speed / (sv_friction * prestate.surface_friction *
                                   std::fmaxf(speed, sv_stopspeed) *
                                   cstypes::kTickInterval))));

    for (auto i = 2; i <= ticks_needed + 2; ++i) {
      if (check_edge(i)) {
        approaching_edge = true;
        break;
      }
    }
  }

  if (!at_edge && !approaching_edge) {
    return;
  }

  const auto base = cmd->csgo_user_cmd.MutableBase();
  if (!base) {
    return;
  }

  const auto view_yaw = base->Viewangles()->Y();
  const auto view_yaw_rad = view_yaw * (std::numbers::pi_v<float> / 180.0f);
  const auto max_weapon_speed = combat::g_shared.Ctx().valid
                                    ? combat::g_shared.Ctx().weapon_max_speed
                                    : 250.0f;

  if (at_edge) {
    const auto forward_move = base->Forwardmove();
    const auto left_move = base->Leftmove();

    const auto wish_x = std::cosf(view_yaw_rad) * forward_move -
                        std::sinf(view_yaw_rad) * left_move;
    const auto wish_y = std::sinf(view_yaw_rad) * forward_move +
                        std::cosf(view_yaw_rad) * left_move;
    const auto wish_len = std::sqrtf(wish_x * wish_x + wish_y * wish_y);

    if (wish_len > 0.001f) {
      const auto wish_norm_x = wish_x / wish_len;
      const auto wish_norm_y = wish_y / wish_len;

      auto test_origin = origin;
      test_origin.x += wish_norm_x * 4.0f;
      test_origin.y += wish_norm_y * 4.0f;

      const auto start =
          math::Vector3{test_origin.x, test_origin.y, origin.z + 2.0f};
      const auto end =
          math::Vector3{test_origin.x, test_origin.y, origin.z - 4.0f};

      const auto result = systems::g_tracing.TracePlayerBbox(
          start, end, {mins, maxs}, filter, movement_services);
      const auto input_toward_edge =
          result.fraction >= 1.0f || result.normal.z < sv_standable_normal;

      if (input_toward_edge) {
        const auto pushback_yaw = std::atan2f(wish_norm_y, wish_norm_x) *
                                  (180.0f / std::numbers::pi_v<float>)+180.0f;
        const auto pushback_rotation =
            (view_yaw - pushback_yaw) * (std::numbers::pi_v<float> / 180.0f);

        base->SetForwardmove(
            std::clamp(std::cosf(pushback_rotation) * 0.3f, -1.0f, 1.0f));
        base->SetLeftmove(
            std::clamp(std::sinf(pushback_rotation) * -0.3f, -1.0f, 1.0f));

        auto buttons = cmd->buttons.value;
        buttons &= ~static_cast<std::uintptr_t>(
            cstypes::command_buttons::kInForward |
            cstypes::command_buttons::kInBack |
            cstypes::command_buttons::kInMoveleft |
            cstypes::command_buttons::kInMoveright);

        if (base->Forwardmove() > 0.0f) {
          buttons |= cstypes::command_buttons::kInForward;
        } else if (base->Forwardmove() < 0.0f) {
          buttons |= cstypes::command_buttons::kInBack;
        }

        if (base->Leftmove() > 0.0f) {
          buttons |= cstypes::command_buttons::kInMoveleft;
        } else if (base->Leftmove() < 0.0f) {
          buttons |= cstypes::command_buttons::kInMoveright;
        }

        cmd->buttons.value = buttons;

        if (speed <= 5.0f) {
          return;
        }
      }
    }

    if (speed > 1.0f) {
      const auto stop_yaw = std::atan2f(velocity.y, velocity.x) *
                            (180.0f / std::numbers::pi_v<float>)+180.0f;
      const auto rotation =
          (view_yaw - stop_yaw) * (std::numbers::pi_v<float> / 180.0f);

      const auto sv_accelerate = CONVAR("sv_accelerate")->Get<float>();
      const auto accel_speed = sv_accelerate * max_weapon_speed *
                               prestate.surface_friction *
                               cstypes::kTickInterval;
      const auto move_speed = (speed < accel_speed) ? speed : max_weapon_speed;

      const auto fwd = std::clamp(move_speed / max_weapon_speed, 0.0f, 1.0f);

      base->SetForwardmove(std::clamp(std::cosf(rotation) * fwd, -1.0f, 1.0f));
      base->SetLeftmove(
          std::clamp(std::sinf(rotation) * fwd * -1.0f, -1.0f, 1.0f));

      auto buttons = cmd->buttons.value;
      buttons &=
          ~static_cast<std::uintptr_t>(cstypes::command_buttons::kInForward |
                                       cstypes::command_buttons::kInBack |
                                       cstypes::command_buttons::kInMoveleft |
                                       cstypes::command_buttons::kInMoveright);

      if (base->Forwardmove() > 0.0f) {
        buttons |= cstypes::command_buttons::kInForward;
      } else if (base->Forwardmove() < 0.0f) {
        buttons |= cstypes::command_buttons::kInBack;
      }

      if (base->Leftmove() > 0.0f) {
        buttons |= cstypes::command_buttons::kInMoveleft;
      } else if (base->Leftmove() < 0.0f) {
        buttons |= cstypes::command_buttons::kInMoveright;
      }

      cmd->buttons.value = buttons;
    }

    return;
  }

  const auto stop_yaw = std::atan2f(velocity.y, velocity.x) *
                        (180.0f / std::numbers::pi_v<float>)+180.0f;
  const auto rotation =
      (view_yaw - stop_yaw) * (std::numbers::pi_v<float> / 180.0f);

  const auto sv_accelerate = CONVAR("sv_accelerate")->Get<float>();
  const auto accel_speed = sv_accelerate * max_weapon_speed *
                           prestate.surface_friction * cstypes::kTickInterval;
  const auto move_speed = (speed < accel_speed) ? speed : max_weapon_speed;

  const auto fwd = std::clamp(move_speed / max_weapon_speed, 0.0f, 1.0f);

  base->SetForwardmove(std::clamp(std::cosf(rotation) * fwd, -1.0f, 1.0f));
  base->SetLeftmove(std::clamp(std::sinf(rotation) * fwd * -1.0f, -1.0f, 1.0f));

  auto buttons = cmd->buttons.value;
  buttons &= ~static_cast<std::uintptr_t>(
      cstypes::command_buttons::kInForward | cstypes::command_buttons::kInBack |
      cstypes::command_buttons::kInMoveleft |
      cstypes::command_buttons::kInMoveright);

  if (base->Forwardmove() > 0.0f) {
    buttons |= cstypes::command_buttons::kInForward;
  } else if (base->Forwardmove() < 0.0f) {
    buttons |= cstypes::command_buttons::kInBack;
  }

  if (base->Leftmove() > 0.0f) {
    buttons |= cstypes::command_buttons::kInMoveleft;
  } else if (base->Leftmove() < 0.0f) {
    buttons |= cstypes::command_buttons::kInMoveright;
  }

  cmd->buttons.value = buttons;
}

}  // namespace features::movement
