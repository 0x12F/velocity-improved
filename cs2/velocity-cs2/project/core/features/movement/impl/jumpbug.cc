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

void Jumpbug::OnCreateMove(systems::Input::Usercmd* cmd) {
  this->m_active_this_tick_ = false;

  if (!settings::g_movement.jumpbug.value) {
    return;
  }

  if (features::movement::g_edgebug.ActiveThisTick()) {
    return;
  }

  auto& buttons = cmd->buttons;
  if (!(buttons.value & cstypes::command_buttons::kInJump)) {
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

  if (prestate.networked_velocity.z > 0.0f) {
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
  const auto duck_speed = memory::Read<float>(
      movement_services +
      SCHEMA("CCSPlayer_MovementServices", "m_flDuckSpeed"_hash));
  const auto holding_duck =
      (buttons.value & cstypes::command_buttons::kInDuck) != 0;
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

  const auto result = systems::g_tracing.TracePlayerBbox(
      trace_start, trace_end, {mins, maxs}, filter, movement_services);
  const auto valid_trace = (result.fraction > 0.0f && result.fraction < 1.0f);

  if (!valid_trace || result.normal.z < sv_standable_normal) {
    return;
  }

  const auto dot = velocity.x * result.normal.x + velocity.y * result.normal.y;
  this->m_active_this_tick_ = result.normal.z >= 0.98f || dot >= 0.0f;

  const auto when = std::clamp(result.fraction, 0.001f, 0.99f);
  // const auto when = std::clamp( std::round( result.fraction * 64.0f )
  // / 64.0f, 1.0f / 64.0f, 63.0f / 64.0f );

  this->m_landing_fraction_ = when;

  // buttons.value &= ~cstypes::command_buttons::in_duck;
  // buttons.value_changed &= ~cstypes::command_buttons::in_duck;

  const auto base = cmd->csgo_user_cmd.MutableBase();
  if (!base) {
    return;
  }

  // i know this looks sketchy but i swear it works
  if (result.normal.z < 0.985f) {
    this->m_active_this_tick_ = false;
  }

  const auto subtick_moves = base->MutableSubtickMoves();

  if (const auto duck_down =
          systems::g_input.AcquireSubtickStep(subtick_moves)) {
    duck_down->SetButton(cstypes::command_buttons::kInDuck);
    duck_down->SetPressed(true);
    duck_down->SetWhen(0.0f);
    duck_down->SetAnalogForwardDelta(0.0f);
    duck_down->SetAnalogLeftDelta(0.0f);
  }

  if (const auto duck_up = systems::g_input.AcquireSubtickStep(subtick_moves)) {
    duck_up->SetButton(cstypes::command_buttons::kInDuck);
    duck_up->SetPressed(false);
    duck_up->SetWhen(when);
    duck_up->SetAnalogForwardDelta(0.0f);
    duck_up->SetAnalogLeftDelta(0.0f);
  }

  if (const auto jump_up = systems::g_input.AcquireSubtickStep(subtick_moves)) {
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

  // buttons.value &= ~cstypes::command_buttons::in_jump;
  // buttons.value_changed &= ~cstypes::command_buttons::in_jump;
}

float Jumpbug::GetImpulseMul(std::uintptr_t local_pawn) const {
  const auto movement_services = memory::Read<std::uintptr_t>(
      local_pawn + SCHEMA("C_BasePlayerPawn", "m_pMovementServices"_hash));
  if (!movement_services) {
    return 0.0f;
  }

  const auto stamina = memory::Read<float>(
      movement_services +
      SCHEMA("CCSPlayer_MovementServices", "m_flStamina"_hash));

  if (CONVAR("sv_legacy_jump")->Get<bool>()) {
    if (stamina <= 0.0f) {
      return 1.0f;
    }

    return std::clamp(1.0f - (stamina / 100.0f), 0.0f, 1.0f);
  }

  const auto current_tick = memory::Read<int>(
      memory::Read<std::uintptr_t>(addresses::globals::global_vars) + 0x44);
  const auto modern_jump =
      movement_services +
      SCHEMA("CCSPlayer_MovementServices", "m_ModernJump"_hash);
  const auto landing_vel_z =
      memory::Read<float>(modern_jump + SCHEMA("CCSPlayerModernJump",
                                               "m_flLastLandedVelocityZ"_hash));
  const auto landed_tick = memory::Read<std::uint32_t>(
      modern_jump + SCHEMA("CCSPlayerModernJump", "m_nLastLandedTick"_hash));
  const auto base = std::clamp((landing_vel_z * 0.0005f) + 1.0f, 0.02f, 1.0f);
  const auto ticks_since_landing =
      static_cast<float>(current_tick - landed_tick);

  auto result = std::clamp(base + (ticks_since_landing * 0.6f), 0.0f, 1.0f);

  if (stamina > 0.0f) {
    result *= std::clamp(1.0f - (stamina / 100.0f), 0.0f, 1.0f);
  }

  return result;
}

}  // namespace features::movement
