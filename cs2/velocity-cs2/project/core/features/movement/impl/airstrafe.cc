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

void Airstrafe::OnCreateMove(systems::Input::Usercmd* cmd) {
  if (features::movement::g_jumpbug.ActiveThisTick()) {
    return;
  }

  if (features::movement::g_bhop.LandingJumpThisTick()) {
    return;
  }

  const auto base = cmd->csgo_user_cmd.MutableBase();
  if (!base) {
    return;
  }

  const auto current_buttons = cmd->buttons.value;
  const bool shift_held =
      current_buttons &
      static_cast<std::uintptr_t>(cstypes::command_buttons::kInSprint);

  const auto& prestate = systems::g_prediction.Pre();
  const bool in_air = !(prestate.flags & cstypes::entity_flags::kOnGround);

  if (shift_held && in_air) {
    const auto& vel = prestate.networked_velocity;
    float forward_move = 0.0f;
    float left_move = 0.0f;

    if (vel.Length2d() > 10.0f) {
      const auto vel_yaw =
          std::atan2f(vel.y, vel.x) * (180.0f / std::numbers::pi_v<float>);

      const auto view_yaw = base->Viewangles()->Y();
      const auto relative_yaw =
          (vel_yaw - view_yaw) * (std::numbers::pi_v<float> / 180.0f);

      const auto fwd_x = std::cosf(relative_yaw);
      const auto fwd_y = std::sinf(relative_yaw);

      forward_move = std::clamp(-fwd_x, -1.0f, 1.0f);
      left_move = std::clamp(-fwd_y, -1.0f, 1.0f);  // negated
    }

    base->SetForwardmove(forward_move);
    base->SetLeftmove(left_move);

    // write subtick entries so the engine respects the input precisely
    const auto subtick_moves = base->MutableSubtickMoves();
    if (subtick_moves) {
      const auto step = systems::g_input.AcquireSubtickStep(subtick_moves);
      if (step) {
        step->SetButton(0);
        step->SetPressed(false);
        step->SetWhen(0.0f);
        step->SetAnalogForwardDelta(forward_move -
                                    prestate.last_movement_impulses.x);
        step->SetAnalogLeftDelta(left_move - prestate.last_movement_impulses.y);
      }
    }
    return;
  }

  const auto wants_stop = features::combat::g_rage.ShouldStop() ||
                          features::misc::g_projectile_trajectory.ShouldStop();

  if (!settings::g_movement.airstrafe.value && !wants_stop ||
      features::combat::g_rage.IsFiringThisTick()) {
    return;
  }

  const auto local = systems::g_local.Get();
  if (!local.pawn) {
    return;
  }

  const auto move_type = memory::Read<std::uint8_t>(
      local.pawn + SCHEMA("CBaseEntity", "m_nActualMoveType"_hash));
  if (move_type == cstypes::move_type::kLadder ||
      move_type == cstypes::move_type::kNoclip) {
    return;
  }

  if (settings::g_movement.m_test_strafer.enabled.value &&
      features::movement::g_test_strafer.HandledThisTick()) {
    return;
  }

  if (prestate.flags & cstypes::entity_flags::kOnGround) {
    return;
  }

  if (current_buttons &
      static_cast<std::uintptr_t>(cstypes::command_buttons::kInSprint)) {
    return;
  }

  const auto subtick_moves = base->MutableSubtickMoves();
  if (!subtick_moves) {
    return;
  }

  if (!wants_stop) {
    this->CheckButton(current_buttons, cstypes::command_buttons::kInMoveleft);
    this->CheckButton(current_buttons, cstypes::command_buttons::kInMoveright);
    this->CheckButton(current_buttons, cstypes::command_buttons::kInForward);
    this->CheckButton(current_buttons, cstypes::command_buttons::kInBack);
    this->m_last_buttons_ = current_buttons;
  }

  const auto movement_services = memory::Read<std::uintptr_t>(
      local.pawn + SCHEMA("C_BasePlayerPawn", "m_pMovementServices"_hash));
  if (!movement_services) {
    return;
  }

  const auto sv_airaccelerate = CONVAR("sv_airaccelerate")->Get<float>();
  const auto sv_air_max_wishspeed =
      CONVAR("sv_air_max_wishspeed")->Get<float>();
  const auto sv_gravity = CONVAR("sv_gravity")->Get<float>();
  const auto sv_staminarecoveryrate =
      CONVAR("sv_staminarecoveryrate")->Get<float>();

  const auto view_angles = this->m_angles_;
  const auto cmd_move_backup =
      math::Vector3{base->Forwardmove(), base->Leftmove(), 0.0f};
  const auto effective_maxspeed =
      memory::Read<float>(movement_services + SCHEMA("CPlayer_MovementServices",
                                                     "m_flMaxspeed"_hash));

  constexpr auto kSubtickCount{32};
  constexpr auto kFrameTime{cstypes::kTickInterval /
                            static_cast<float>(kSubtickCount)};

  auto yaw_offset{0.0f};

  if (!wants_stop) {
    if (this->m_last_pressed_ & cstypes::command_buttons::kInMoveleft) {
      yaw_offset += 90.0f;
    }

    if (this->m_last_pressed_ & cstypes::command_buttons::kInMoveright) {
      yaw_offset -= 90.0f;
    }

    if (this->m_last_pressed_ & cstypes::command_buttons::kInForward) {
      yaw_offset *= 0.5f;
    } else if (this->m_last_pressed_ & cstypes::command_buttons::kInBack) {
      yaw_offset = -yaw_offset * 0.5f + 180.0f;
    }
  }

  const auto has_direction_input =
      (this->m_last_pressed_ & cstypes::command_buttons::kInMoveleft) ||
      (this->m_last_pressed_ & cstypes::command_buttons::kInMoveright) ||
      (this->m_last_pressed_ & cstypes::command_buttons::kInForward) ||
      (this->m_last_pressed_ & cstypes::command_buttons::kInBack);
  const auto effective_wants_stop =
      wants_stop || (settings::g_movement.airstrafe_fully_directional.value &&
                     !has_direction_input);

  auto velocity = prestate.networked_velocity;
  auto last_impulses = prestate.last_movement_impulses;
  auto stamina = prestate.stamina;
  const auto surface_friction = prestate.surface_friction;

  if (effective_wants_stop && velocity.Length2d() <= 10.0f) {
    this->RotateToStop(base, velocity);
    return;
  }

  if (last_impulses.y < 0.0f) {
    this->m_side_switch_ = false;
  } else if (last_impulses.y > 0.0f) {
    this->m_side_switch_ = true;
  }

  for (auto i = 0; i < kSubtickCount; ++i) {
    base->SetForwardmove(cmd_move_backup.x);
    base->SetLeftmove(cmd_move_backup.y);

    auto speed_2d =
        std::sqrtf(velocity.x * velocity.x + velocity.y * velocity.y);

    if (stamina > 0.0f) {
      const auto speed_scale =
          std::clamp(1.0f - (stamina / 100.0f), 0.0f, 1.0f);
      speed_2d *= speed_scale * speed_scale;
      stamina =
          std::fmaxf(stamina - (kFrameTime * sv_staminarecoveryrate), 0.0f);
    }

    velocity.z -= (sv_gravity * kFrameTime);

    if (speed_2d > 0.0001f) {
      math::Vector3 forward_dir{}, right_dir{};
      math::helpers::AngleVectors2d(base->Viewangles()->Y(), forward_dir,
                                    right_dir);

      math::Vector3 wish_dir{
          (forward_dir.x * last_impulses.x * effective_maxspeed) +
              (right_dir.x * last_impulses.y * effective_maxspeed),
          (forward_dir.y * last_impulses.x * effective_maxspeed) +
              (right_dir.y * last_impulses.y * effective_maxspeed),
          0.0f};

      auto wish_speed =
          std::sqrtf(wish_dir.x * wish_dir.x + wish_dir.y * wish_dir.y);
      if (wish_speed > 0.0001f) {
        wish_dir.x /= wish_speed;
        wish_dir.y /= wish_speed;
        wish_dir.z = 0.0f;
      }

      wish_speed = std::fminf(wish_speed, effective_maxspeed);

      const auto capped_wish = std::fminf(wish_speed, sv_air_max_wishspeed);
      const auto current_speed =
          velocity.x * wish_dir.x + velocity.y * wish_dir.y;
      const auto add_speed = capped_wish - current_speed;

      if (add_speed > 0.0f) {
        const auto accel_speed = sv_airaccelerate * effective_maxspeed *
                                 kFrameTime * surface_friction;
        const auto half_accel = accel_speed * 0.5f;
        const auto gain = std::fminf(half_accel, add_speed);

        velocity.x += wish_dir.x * gain;
        velocity.y += wish_dir.y * gain;
      }

      /*if ( add_speed > 0.0f )
      {
              const auto accel_speed = sv_airaccelerate * effective_maxspeed *
      frame_time * surface_friction; const auto half_accel = accel_speed * 0.5f;

              if ( half_accel <= add_speed )
              {
                      if ( accel_speed <= add_speed )
                      {
                              velocity.x += wish_dir.x * half_accel;
                              velocity.y += wish_dir.y * half_accel;
                      }
                      else
                      {
                              const auto remainder = add_speed - half_accel;
                              velocity.x += wish_dir.x * remainder;
                              velocity.y += wish_dir.y * remainder;
                      }
              }
      }*/
    }

    velocity.z += (sv_gravity * kFrameTime) * 0.5f;
    speed_2d = std::sqrtf(velocity.x * velocity.x + velocity.y * velocity.y);

    if (speed_2d >= 10.0f) {
      base->SetForwardmove(0.0f);
      base->SetLeftmove(0.0f);

      if (effective_wants_stop) {
        this->RotateToStop(base, velocity);
      } else {
        const auto velocity_angle = std::atan2f(velocity.y, velocity.x) *
                                    (180.0f / std::numbers::pi_v<float>);
        const auto accel_speed = sv_airaccelerate * effective_maxspeed *
                                 kFrameTime * surface_friction;
        const auto half_accel = accel_speed * 0.5f;
        const auto optimal_floor =
            std::fmaxf(half_accel, sv_air_max_wishspeed - half_accel);
        const auto ideal_angle =
            std::clamp(std::atanf(optimal_floor / speed_2d) *
                           (180.0f / std::numbers::pi_v<float>),
                       0.0f, 45.0f);

        auto target_yaw = view_angles.y + yaw_offset;
        math::helpers::NormalizeAngle(target_yaw);

        auto velocity_delta = target_yaw - velocity_angle;
        math::helpers::NormalizeAngle(velocity_delta);

        if ((std::fabsf(velocity_delta) > 170.0f && speed_2d > 80.0f) ||
            (velocity_delta > ideal_angle && speed_2d > 80.0f)) {
          target_yaw = velocity_angle + ideal_angle;
          base->SetLeftmove(-1.0f);
        } else if (-ideal_angle <= velocity_delta || speed_2d <= 80.0f) {
          if (this->m_side_switch_) {
            target_yaw = target_yaw - ideal_angle;
            base->SetLeftmove(-1.0f);
          } else {
            target_yaw = target_yaw + ideal_angle;
            base->SetLeftmove(1.0f);
          }
        } else {
          target_yaw = velocity_angle - ideal_angle;
          base->SetLeftmove(1.0f);
        }

        math::helpers::NormalizeAngle(target_yaw);

        this->RotateMovement(base, target_yaw, base->Viewangles()->Y());
      }
    }

    const auto step = systems::g_input.AcquireSubtickStep(subtick_moves);
    if (!step) {
      continue;
    }

    step->SetButton(0);
    step->SetPressed(false);
    step->SetWhen(static_cast<float>(i) / static_cast<float>(kSubtickCount));
    step->SetAnalogForwardDelta(base->Forwardmove() - last_impulses.x);
    step->SetAnalogLeftDelta(base->Leftmove() - last_impulses.y);

    last_impulses.x += base->Forwardmove() - last_impulses.x;
    last_impulses.y += base->Leftmove() - last_impulses.y;

    if (!effective_wants_stop) {
      this->m_side_switch_ = !this->m_side_switch_;
    }
  }
}

void Airstrafe::StoreAngles() {
  this->m_angles_ = systems::g_input.GetViewAngles();
}

void Airstrafe::CheckButton(std::uintptr_t current_buttons,
                            std::uintptr_t button) {
  constexpr auto kMoveleft =
      static_cast<std::uintptr_t>(cstypes::command_buttons::kInMoveleft);
  constexpr auto kMoveright =
      static_cast<std::uintptr_t>(cstypes::command_buttons::kInMoveright);
  constexpr auto kForward =
      static_cast<std::uintptr_t>(cstypes::command_buttons::kInForward);
  constexpr auto kBack =
      static_cast<std::uintptr_t>(cstypes::command_buttons::kInBack);

  if (current_buttons & button &&
      (!(this->m_last_buttons_ & button) ||
       (button & kMoveleft && !(this->m_last_pressed_ & kMoveright)) ||
       (button & kMoveright && !(this->m_last_pressed_ & kMoveleft)) ||
       (button & kForward && !(this->m_last_pressed_ & kBack)) ||
       (button & kBack && !(this->m_last_pressed_ & kForward)))) {
    if (button & kMoveleft) {
      this->m_last_pressed_ &= ~kMoveright;
    } else if (button & kMoveright) {
      this->m_last_pressed_ &= ~kMoveleft;
    } else if (button & kForward) {
      this->m_last_pressed_ &= ~kBack;
    } else if (button & kBack) {
      this->m_last_pressed_ &= ~kForward;
    }

    this->m_last_pressed_ |= button;
  } else if (!(current_buttons & button)) {
    this->m_last_pressed_ &= ~button;
  }
}

void Airstrafe::RotateMovement(proto::BaseUsercmdPb* base, float target_yaw,
                               float view_yaw) const {
  const auto forward_move = base->Forwardmove();
  const auto side_move = base->Leftmove();

  math::Vector3 target_forward{}, target_right{};
  math::helpers::AngleVectors2d(target_yaw, target_forward, target_right);

  math::Vector3 view_forward{}, view_right{};
  math::helpers::AngleVectors2d(view_yaw, view_forward, view_right);

  const auto tf = target_forward * forward_move;
  const auto tr = target_right * side_move;

  const auto corrected_forward = view_forward.Dot(tf) + view_forward.Dot(tr);
  const auto corrected_side = view_right.Dot(tf) + view_right.Dot(tr);

  base->SetForwardmove(std::clamp(-corrected_forward, -1.0f, 1.0f));
  base->SetLeftmove(std::clamp(-corrected_side, -1.0f, 1.0f));
}

void Airstrafe::RotateToStop(proto::BaseUsercmdPb* base,
                             const math::Vector3& velocity) const {
  const auto speed = velocity.Length2d();
  const auto wish_yaw = std::atan2f(velocity.y, velocity.x) *
                        (180.0f / std::numbers::pi_v<float>)+180.0f;

  {
    const auto& ctx = features::combat::g_shared.Ctx();
    const auto max_speed =
        (ctx.valid && ctx.weapon_vdata)
            ? memory::Read<float>(
                  ctx.weapon_vdata +
                  SCHEMA("CCSWeaponBaseVData", "m_flMaxSpeed"_hash))
            : 250.0f;

    base->SetForwardmove(std::clamp(speed / max_speed, 0.0f, 1.0f));
    base->SetLeftmove(0.0f);
  }

  const auto rotation = (base->Viewangles()->Y() - wish_yaw) *
                        (std::numbers::pi_v<float> / 180.0f);
  const auto fwd = base->Forwardmove();
  const auto side = base->Leftmove();

  base->SetForwardmove(std::clamp(
      std::cosf(rotation) * fwd - std::sinf(rotation) * side, -1.0f, 1.0f));
  base->SetLeftmove(std::clamp(
      (std::sinf(rotation) * fwd + std::cosf(rotation) * side) * -1.0f, -1.0f,
      1.0f));
}

}  // namespace features::movement
