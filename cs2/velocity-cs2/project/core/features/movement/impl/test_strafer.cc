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

constexpr auto kMaxSubticks{16};
constexpr auto kMinStrafeSpeed{5.0f};

[[nodiscard]] float GetMaxSubtickWhen(proto::BaseUsercmdPb* base) {
  auto max_when{0.0f};

  for (auto i = 0; i < base->SubtickMovesSize(); ++i) {
    if (const auto step = base->MutableSubtickMoves(i)) {
      max_when = std::fmaxf(max_when, step->When());
    }
  }

  return max_when;
}

[[nodiscard]] float RefIdealAngle(float speed, float dt, float wishspeed,
                                  float air_accel, float air_max_wishspeed) {
  if (speed < 1.0f) {
    return 15.0f;
  }

  const auto accel_speed = wishspeed * air_accel * dt;
  float cos_theta{};

  if (accel_speed >= air_max_wishspeed) {
    cos_theta = air_max_wishspeed / (2.0f * speed);
  } else {
    cos_theta = (air_max_wishspeed - accel_speed) / speed;
  }

  cos_theta = std::clamp(cos_theta, -1.0f, 1.0f);
  return std::fmaxf(
      std::acosf(cos_theta) * (180.0f / std::numbers::pi_v<float>), 1.0f);
}

[[nodiscard]] float RefAirStrafer(float vel_x, float vel_y, float target_yaw,
                                  float dt, bool side_switch, float wishspeed,
                                  float air_accel, float air_max_wishspeed) {
  const auto speed = std::sqrtf(vel_x * vel_x + vel_y * vel_y);
  const auto theta =
      RefIdealAngle(speed, dt, wishspeed, air_accel, air_max_wishspeed);

  if (speed < 15.0f) {
    return target_yaw;
  }

  const auto vel_angle =
      std::atan2f(vel_y, vel_x) * (180.0f / std::numbers::pi_v<float>);
  auto vel_delta = target_yaw - vel_angle;
  math::helpers::NormalizeAngle(vel_delta);

  if (std::fabsf(vel_delta) > 2.0f) {
    if (vel_delta > 0.0f) {
      auto yaw = vel_angle + theta;
      math::helpers::NormalizeAngle(yaw);
      return yaw;
    }

    auto yaw = vel_angle - theta;
    math::helpers::NormalizeAngle(yaw);
    return yaw;
  }

  if (side_switch) {
    auto yaw = vel_angle + theta;
    math::helpers::NormalizeAngle(yaw);
    return yaw;
  }

  auto yaw = vel_angle - theta;
  math::helpers::NormalizeAngle(yaw);
  return yaw;
}

void RefAirAccelSim(float& vel_x, float& vel_y, float wishdir_yaw,
                    float frame_time, float friction, float wishspeed,
                    float air_accel, float air_max_wishspeed) {
  const auto yaw_rad = wishdir_yaw * (std::numbers::pi_v<float> / 180.0f);
  const auto wish_dir_x = std::cosf(yaw_rad);
  const auto wish_dir_y = std::sinf(yaw_rad);

  const auto capped = std::fminf(wishspeed, air_max_wishspeed);
  const auto dot = vel_x * wish_dir_x + vel_y * wish_dir_y;
  const auto add_speed = capped - dot;

  if (add_speed <= 0.0f) {
    return;
  }

  const auto accel_speed = wishspeed * air_accel * friction * frame_time;
  const auto step = std::fminf(accel_speed, add_speed);

  vel_x += wish_dir_x * step;
  vel_y += wish_dir_y * step;
}

}  // namespace

[[nodiscard]] bool TestStrafer::IsActive() const {
  if (!settings::g_movement.m_test_strafer.enabled.value) {
    return false;
  }

  return CONVAR("sv_quantize_movement_input")->Get<bool>();
}

math::Vector2 TestStrafer::MovementFromButtons(std::uintptr_t pressed) {
  auto forward_move{0.0f};
  auto left_move{0.0f};

  if (pressed & cstypes::command_buttons::kInForward) {
    forward_move = 1.0f;
  } else if (pressed & cstypes::command_buttons::kInBack) {
    forward_move = -1.0f;
  }

  if (pressed & cstypes::command_buttons::kInMoveleft) {
    left_move = -1.0f;
  } else if (pressed & cstypes::command_buttons::kInMoveright) {
    left_move = 1.0f;
  }

  return {forward_move, left_move};
}

void TestStrafer::OnCreateMove(systems::Input::Usercmd* cmd) {
  this->m_handled_this_tick_ = false;

  if (!this->IsActive()) {
    return;
  }

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

  const auto& prestate = systems::g_prediction.Pre();
  if (prestate.flags & cstypes::entity_flags::kOnGround) {
    return;
  }

  if (features::combat::g_rage.IsFiringThisTick()) {
    return;
  }

  this->QuantizedPath(cmd);
}

bool TestStrafer::ApplyYawSubtick(proto::BaseUsercmdPb* base, float when,
                                  float yaw_delta) const {
  math::helpers::NormalizeAngle(yaw_delta);

  if (std::fabsf(yaw_delta) <= 0.01f) {
    return false;
  }

  const auto subtick_moves = base->MutableSubtickMoves();
  if (!subtick_moves) {
    return false;
  }

  const auto step = systems::g_input.AcquireSubtickStep(subtick_moves);
  if (!step) {
    return false;
  }

  step->SetWhen(when);
  step->SetButton(0);
  step->SetPressed(false);
  step->SetAnalogForwardDelta(0.0f);
  step->SetAnalogLeftDelta(0.0f);
  step->SetYawDelta(yaw_delta);
  step->SetPitchDelta(0.0f);
  return true;
}

void TestStrafer::QuantizedPath(systems::Input::Usercmd* cmd) {
  const auto current_buttons = cmd->buttons.value;
  if (current_buttons &
      static_cast<std::uintptr_t>(cstypes::command_buttons::kInSprint)) {
    return;
  }

  const auto base = cmd->csgo_user_cmd.MutableBase();
  if (!base) {
    return;
  }

  this->CheckButton(current_buttons, cstypes::command_buttons::kInMoveleft);
  this->CheckButton(current_buttons, cstypes::command_buttons::kInMoveright);
  this->CheckButton(current_buttons, cstypes::command_buttons::kInForward);
  this->CheckButton(current_buttons, cstypes::command_buttons::kInBack);
  this->m_last_buttons_ = current_buttons;

  const auto& prestate = systems::g_prediction.Pre();
  const auto velocity = prestate.networked_velocity;
  const auto speed_2d = velocity.Length2d();
  const auto command_yaw = base->Viewangles()->Y();

  const auto player_move = MovementFromButtons(this->m_last_pressed_);
  if (player_move.x == 0.0f && player_move.y == 0.0f) {
    return;
  }

  if (speed_2d < kMinStrafeSpeed) {
    return;
  }

  const auto start_when = GetMaxSubtickWhen(base);
  if (start_when >= 0.99f) {
    return;
  }

  const auto sv_airaccelerate = CONVAR("sv_airaccelerate")->Get<float>();
  const auto sv_maxspeed = CONVAR("sv_maxspeed")->Get<float>();
  const auto sv_air_max_wishspeed =
      CONVAR("sv_air_max_wishspeed")->Get<float>();
  const auto surface_friction = prestate.surface_friction;

  const auto base_yaw_offset = std::atan2f(-player_move.y, player_move.x) *
                               (180.0f / std::numbers::pi_v<float>);
  auto target_yaw = command_yaw + base_yaw_offset;
  math::helpers::NormalizeAngle(target_yaw);

  const auto sub_frame =
      cstypes::kTickInterval / static_cast<float>(kMaxSubticks);
  const auto when_step =
      (1.0f - start_when) / static_cast<float>(kMaxSubticks + 1);

  auto acc_yaw = command_yaw;
  auto sim_vx = velocity.x;
  auto sim_vy = velocity.y;
  auto injected = 0;

  for (auto i = 1; i <= kMaxSubticks; ++i) {
    const auto entry_side = ((this->m_substep_counter_ + i) % 2) == 0;
    const auto wishdir_yaw =
        RefAirStrafer(sim_vx, sim_vy, target_yaw, sub_frame, entry_side,
                      sv_maxspeed, sv_airaccelerate, sv_air_max_wishspeed);

    auto target_view_yaw = wishdir_yaw - base_yaw_offset;
    math::helpers::NormalizeAngle(target_view_yaw);

    auto yaw_delta = target_view_yaw - acc_yaw;
    math::helpers::NormalizeAngle(yaw_delta);

    const auto when_frac = start_when + static_cast<float>(i) * when_step;

    if (!this->ApplyYawSubtick(base, when_frac, yaw_delta)) {
      break;
    }

    acc_yaw = target_view_yaw;
    RefAirAccelSim(sim_vx, sim_vy, wishdir_yaw, sub_frame, surface_friction,
                   sv_maxspeed, sv_airaccelerate, sv_air_max_wishspeed);
    ++injected;
  }

  if (injected > 0) {
    this->m_handled_this_tick_ = true;
    ++this->m_substep_counter_;
  }
}

void TestStrafer::CheckButton(std::uintptr_t current_buttons,
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

}  // namespace features::movement
