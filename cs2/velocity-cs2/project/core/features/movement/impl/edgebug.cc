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
import <string_view>;
import <type_traits>;

namespace features::movement {

namespace {

[[nodiscard]] bool SideHasStandableGround(
    const math::Vector3& at_xy_z_reference, float offset_x, float offset_y,
    const systems::Tracing::BboxCollision& bbox,
    const systems::Tracing::PlayerMovementFilter& filter,
    std::uintptr_t movement_services, float sv_standable_normal) {
  const auto start =
      math::Vector3{at_xy_z_reference.x + offset_x,
                    at_xy_z_reference.y + offset_y, at_xy_z_reference.z + 2.0f};
  const auto end =
      math::Vector3{at_xy_z_reference.x + offset_x,
                    at_xy_z_reference.y + offset_y, at_xy_z_reference.z - 4.0f};

  const auto r = systems::g_tracing.TracePlayerBbox(start, end, bbox, filter,
                                                    movement_services);
  return r.fraction > 0.0f && r.fraction < 1.0f &&
         r.normal.z >= sv_standable_normal;
}

[[nodiscard]] bool ModeAllows(int mode, bool holding_jump, float vel2d,
                              float vz) {
  switch (mode) {
    case 0:
      return true;
    case 1:
      return true;
    case 2:
      return !holding_jump;
    case 3:
      return vel2d > 15.0f;
    case 4:
      return vel2d > 25.0f && vz < -100.0f;
    default:
      return true;
  }
}

[[nodiscard]] bool EdgeGeometryOk(int mode, bool left_ok, bool right_ok) {
  if (mode == 0) {
    return true;
  }

  if (left_ok && right_ok) {
    return false;
  }

  return true;
}

}  // namespace

void Edgebug::OnCreateMove(systems::Input::Usercmd* cmd) {
  this->m_active_this_tick_ = false;

  if (!settings::g_movement.edgebug.value) {
    return;
  }

  const auto mode = std::clamp(settings::g_movement.edgebug_mode.value, 0, 4);
  const auto passes =
      std::clamp(settings::g_movement.edgebug_passes.value, 1, 5);

  const auto local = systems::g_local.Get();
  if (!local.pawn) {
    return;
  }

  const auto move_type = memory::Read<std::uint8_t>(
      local.pawn + SCHEMA("C_BaseEntity", "m_nActualMoveType"_hash));
  if (move_type == cstypes::move_type::kLadder ||
      move_type == cstypes::move_type::kNoclip) {
    return;
  }

  const auto& prestate = systems::g_prediction.Pre();
  if (prestate.flags & cstypes::entity_flags::kOnGround) {
    return;
  }

  if (prestate.networked_velocity.z > 0.0f) {
    return;
  }

  const auto holding_jump =
      (cmd->buttons.value & cstypes::command_buttons::kInJump) != 0;
  const auto vel2d = prestate.networked_velocity.Length2d();

  if (!ModeAllows(mode, holding_jump, vel2d, prestate.networked_velocity.z)) {
    return;
  }

  const auto movement_services = memory::Read<std::uintptr_t>(
      local.pawn + SCHEMA("C_BasePlayerPawn", "m_pMovementServices"_hash));
  if (!movement_services) {
    return;
  }

  const auto duck_amount = memory::Read<float>(
      movement_services +
      SCHEMA("CCSPlayer_MovementServices", "m_flDuckAmount"_hash));
  const auto holding_duck =
      (cmd->buttons.value & cstypes::command_buttons::kInDuck) != 0;
  const auto mins = memory::Read<math::Vector3>(
      local.pawn + SCHEMA("C_BaseModelEntity", "m_Collision"_hash) +
      SCHEMA("CCollisionProperty", "m_vecMins"_hash));
  auto maxs = memory::Read<math::Vector3>(
      local.pawn + SCHEMA("C_BaseModelEntity", "m_Collision"_hash) +
      SCHEMA("CCollisionProperty", "m_vecMaxs"_hash));

  auto trace_origin = prestate.networked_origin;
  {
    if (holding_duck && duck_amount > 0.0f) {
      const auto standing_height{72.0f};
      const auto duck_hull_diff = standing_height - maxs.z;
      trace_origin.z -= duck_hull_diff * 0.5f;
      maxs.z = standing_height;
    }
  }

  const auto filter = systems::g_tracing.MakePlayerMovementFilter(local.pawn);
  const auto sv_gravity = CONVAR("sv_gravity")->Get<float>();
  const auto sv_standable_normal = CONVAR("sv_standable_normal")->Get<float>();
  const auto gravity_scale = memory::Read<float>(
      local.pawn + SCHEMA("C_BaseEntity", "m_flGravityScale"_hash));

  auto velocity = prestate.networked_velocity;
  velocity.z -= (gravity_scale * sv_gravity * cstypes::kTickInterval) * 0.5f;

  math::Vector3 trace_start = trace_origin;
  math::Vector3 trace_end{};

  trace_end.x = trace_origin.x + velocity.x * cstypes::kTickInterval;
  trace_end.y = trace_origin.y + velocity.y * cstypes::kTickInterval;
  trace_end.z = trace_origin.z + velocity.z * cstypes::kTickInterval;

  trace_end.z -= 2.0f;

  const auto bbox = systems::Tracing::BboxCollision{mins, maxs};
  const auto result = systems::g_tracing.TracePlayerBbox(
      trace_start, trace_end, bbox, filter, movement_services);
  const auto valid_trace = (result.fraction > 0.0f && result.fraction < 1.0f);

  if (!valid_trace || result.normal.z < sv_standable_normal) {
    return;
  }

  const auto dot = velocity.x * result.normal.x + velocity.y * result.normal.y;
  if (!(result.normal.z >= 0.98f || dot >= 0.0f)) {
    return;
  }

  const auto vel2d_len =
      std::sqrtf(velocity.x * velocity.x + velocity.y * velocity.y);
  float perp_x{};
  float perp_y{};
  if (vel2d_len > 0.01f) {
    perp_x = -velocity.y / vel2d_len;
    perp_y = velocity.x / vel2d_len;
  } else {
    perp_x = 1.0f;
    perp_y = 0.0f;
  }

  const auto side_dist =
      std::max(14.0f, std::max(std::fabsf(maxs.x), std::fabsf(maxs.y)) * 1.25f);
  const math::Vector3 ground_ref{result.position.x, result.position.y,
                                 trace_origin.z};

  const auto left_ok = SideHasStandableGround(
      ground_ref, -perp_x * side_dist, -perp_y * side_dist, bbox, filter,
      movement_services, sv_standable_normal);
  const auto right_ok = SideHasStandableGround(
      ground_ref, perp_x * side_dist, perp_y * side_dist, bbox, filter,
      movement_services, sv_standable_normal);

  if (!EdgeGeometryOk(mode, left_ok, right_ok)) {
    return;
  }

  const auto when = std::clamp(result.fraction, 0.001f, 0.99f);

  const auto base = cmd->csgo_user_cmd.MutableBase();
  if (!base) {
    return;
  }

  this->m_active_this_tick_ = true;

  const auto subtick_moves = base->MutableSubtickMoves();

  // Multiple passes = chained duck tap windows across [0, when] (not duplicate
  // pulses at one time).
  const float seg = when / static_cast<float>(passes);
  for (int p = 0; p < passes; ++p) {
    const float t_down = seg * static_cast<float>(p);
    const float t_up = seg * static_cast<float>(p + 1);

    if (const auto duck_down =
            systems::g_input.AcquireSubtickStep(subtick_moves)) {
      duck_down->SetButton(cstypes::command_buttons::kInDuck);
      duck_down->SetPressed(true);
      duck_down->SetWhen(t_down);
      duck_down->SetAnalogForwardDelta(0.0f);
      duck_down->SetAnalogLeftDelta(0.0f);
    }

    if (const auto duck_up =
            systems::g_input.AcquireSubtickStep(subtick_moves)) {
      duck_up->SetButton(cstypes::command_buttons::kInDuck);
      duck_up->SetPressed(false);
      duck_up->SetWhen(t_up);
      duck_up->SetAnalogForwardDelta(0.0f);
      duck_up->SetAnalogLeftDelta(0.0f);
    }
  }

  if (settings::g_movement.edgebug_include_jump_steps.value && holding_jump) {
    if (const auto jump_up =
            systems::g_input.AcquireSubtickStep(subtick_moves)) {
      jump_up->SetButton(cstypes::command_buttons::kInJump);
      jump_up->SetPressed(false);
      jump_up->SetWhen(when);
      jump_up->SetAnalogForwardDelta(0.0f);
      jump_up->SetAnalogLeftDelta(0.0f);
    }

    if (const auto jump_down =
            systems::g_input.AcquireSubtickStep(subtick_moves)) {
      jump_down->SetButton(cstypes::command_buttons::kInJump);
      jump_down->SetPressed(true);
      jump_down->SetWhen(when);
      jump_down->SetAnalogForwardDelta(0.0f);
      jump_down->SetAnalogLeftDelta(0.0f);
    }
  }
}

void Edgebug::OnRender(xdraw::draw_list& draw_list) {
  (void)draw_list;

  if (!settings::g_movement.edgebug.value) {
    return;
  }
}

}  // namespace features::movement
