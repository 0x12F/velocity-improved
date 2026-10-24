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

void Edgejump::OnCreateMove(systems::Input::Usercmd* cmd) const {
  if (!settings::g_movement.edgejump.value) {
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

  const auto move_type = memory::Read<std::uint8_t>(
      local.pawn + SCHEMA("C_BaseEntity", "m_nActualMoveType"_hash));
  if (move_type == cstypes::move_type::kLadder ||
      move_type == cstypes::move_type::kNoclip) {
    return;
  }

  const auto velocity = prestate.networked_velocity;
  if (velocity.Length2d() < 1.0f) {
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

  const auto current_on_edge = check_edge(1);
  const auto next_on_edge = check_edge(2);

  if (next_on_edge && !current_on_edge) {
    const auto vel_xy = math::Vector2{velocity.x, velocity.y};
    const auto speed_xy = vel_xy.Length();

    math::Vector2 perp{0.0f, 0.0f};
    if (speed_xy >= 0.01f) {
      perp = math::Vector2{-vel_xy.y / speed_xy, vel_xy.x / speed_xy};
    }

    static constexpr float kLateral[]{0.0f,  7.0f,   -7.0f, 14.0f, -14.0f,
                                      22.0f, -22.0f, 34.0f, -34.0f};

    const auto ground_ahead_has_stair_step = [&](int ticks_ahead) -> bool {
      const auto bt = cstypes::kTickInterval * static_cast<float>(ticks_ahead);
      const auto base_x = origin.x + velocity.x * bt;
      const auto base_y = origin.y + velocity.y * bt;

      for (const auto lat : kLateral) {
        const auto wx = base_x + perp.x * lat;
        const auto wy = base_y + perp.y * lat;

        const auto stair_scan_start = math::Vector3{wx, wy, origin.z + 2.0f};
        const auto stair_scan_end = math::Vector3{wx, wy, origin.z - 110.0f};
        const auto stair_trace = systems::g_tracing.TracePlayerBbox(
            stair_scan_start, stair_scan_end, {mins, maxs}, filter,
            movement_services);

        if (stair_trace.fraction >= 1.0f ||
            stair_trace.normal.z < sv_standable_normal) {
          continue;
        }

        const auto step_down = origin.z - stair_trace.position.z;
        constexpr auto kMinStep = 0.12f;
        constexpr auto kMaxStep = 48.0f;

        if (step_down >= kMinStep && step_down <= kMaxStep) {
          return true;
        }
      }

      return false;
    };

    if (ground_ahead_has_stair_step(1) || ground_ahead_has_stair_step(2)) {
      return;
    }

    cmd->buttons.value |= cstypes::command_buttons::kInJump;
    cmd->buttons.value_changed |= cstypes::command_buttons::kInJump;
  }
}

}  // namespace features::movement
