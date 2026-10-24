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

namespace {

[[nodiscard]] std::optional<float> PredictLandingFraction(
    std::uintptr_t local_pawn, std::uintptr_t movement_services,
    const systems::Prediction::State& prestate, bool holding_duck) {
  if (prestate.networked_velocity.z > 0.0f) {
    return std::nullopt;
  }

  const auto duck_amount = memory::Read<float>(
      movement_services +
      SCHEMA("CCSPlayer_MovementServices", "m_flDuckAmount"_hash));
  const auto mins = memory::Read<math::Vector3>(
      local_pawn + SCHEMA("C_BaseModelEntity", "m_Collision"_hash) +
      SCHEMA("CCollisionProperty", "m_vecMins"_hash));
  auto maxs = memory::Read<math::Vector3>(
      local_pawn + SCHEMA("C_BaseModelEntity", "m_Collision"_hash) +
      SCHEMA("CCollisionProperty", "m_vecMaxs"_hash));

  auto trace_origin = prestate.networked_origin;
  if (holding_duck && duck_amount > 0.0f) {
    const auto standing_height{72.0f};
    const auto duck_hull_diff = standing_height - maxs.z;
    trace_origin.z -= duck_hull_diff * 0.5f;
    maxs.z = standing_height;
  }

  const auto filter = systems::g_tracing.MakePlayerMovementFilter(local_pawn);
  const auto sv_gravity = CONVAR("sv_gravity")->Get<float>();
  const auto sv_standable_normal = CONVAR("sv_standable_normal")->Get<float>();
  const auto gravity_scale = memory::Read<float>(
      local_pawn + SCHEMA("C_BaseEntity", "m_flGravityScale"_hash));

  auto velocity = prestate.networked_velocity;
  velocity.z -= (gravity_scale * sv_gravity * cstypes::kTickInterval) * 0.5f;

  const math::Vector3 trace_start = trace_origin;
  math::Vector3 trace_end{};

  trace_end.x = trace_origin.x + velocity.x * cstypes::kTickInterval;
  trace_end.y = trace_origin.y + velocity.y * cstypes::kTickInterval;
  trace_end.z = trace_origin.z + velocity.z * cstypes::kTickInterval;
  trace_end.z -= 2.0f;

  const auto result = systems::g_tracing.TracePlayerBbox(
      trace_start, trace_end, {mins, maxs}, filter, movement_services);
  if (result.fraction <= 0.0f || result.fraction >= 1.0f ||
      result.normal.z < sv_standable_normal) {
    return std::nullopt;
  }

  return std::clamp(std::round(result.fraction * 64.0f) / 64.0f, 1.0f / 64.0f,
                    63.0f / 64.0f);
}

void ApplyLandingJump(proto::BaseUsercmdPb* base, float when) {
  const auto subtick_moves = base->MutableSubtickMoves();
  const auto release_when =
      std::clamp(when - 1.0f / 64.0f, 1.0f / 64.0f, 63.0f / 64.0f);

  if (release_when < when) {
    if (const auto jump_up =
            systems::g_input.AcquireSubtickStep(subtick_moves)) {
      jump_up->SetButton(cstypes::command_buttons::kInJump);
      jump_up->SetPressed(false);
      jump_up->SetWhen(release_when);
      jump_up->SetAnalogForwardDelta(0.0f);
      jump_up->SetAnalogLeftDelta(0.0f);
    }
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

}  // namespace

void Bhop::OnCreateMove(systems::Input::Usercmd* cmd) {
  this->m_landing_jump_this_tick_ = false;
  this->m_landing_fraction_ = 1.0f;

  if (!settings::g_movement.bhop.value) {
    return;
  }

  if (CONVAR("sv_autobunnyhopping")->Get<bool>()) {
    return;
  }

  if (!(cmd->buttons.value & cstypes::command_buttons::kInJump)) {
    return;
  }

  if (features::movement::g_jumpbug.ActiveThisTick()) {
    return;
  }

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

  cmd->buttons.value &= ~cstypes::command_buttons::kInJump;

  const auto movement_services = memory::Read<std::uintptr_t>(
      local.pawn + SCHEMA("C_BasePlayerPawn", "m_pMovementServices"_hash));
  if (!movement_services) {
    return;
  }

  const auto holding_duck =
      (cmd->buttons.value & cstypes::command_buttons::kInDuck) != 0;
  const auto landing = PredictLandingFraction(local.pawn, movement_services,
                                              prestate, holding_duck);
  if (!landing) {
    return;
  }

  const auto base = cmd->csgo_user_cmd.MutableBase();
  if (!base) {
    return;
  }

  ApplyLandingJump(base, *landing);
  this->m_landing_jump_this_tick_ = true;
  this->m_landing_fraction_ = *landing;
}

}  // namespace features::movement
