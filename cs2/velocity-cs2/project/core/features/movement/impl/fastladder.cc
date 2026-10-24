module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>
#include <external/xdraw/xdraw.hpp>

#include "core/systems/schema_macros.h"

module velocity;

import <cstddef>;
import <filesystem>;
import <string_view>;
import <type_traits>;

namespace features::movement {

void Fastladder::OnCreateMove(systems::Input::Usercmd* cmd) const {
  if (!settings::g_movement.fastladder.value) {
    return;
  }

  const auto local = systems::g_local.Get();
  if (!local.pawn) {
    return;
  }

  const auto move_type = memory::Read<std::uint8_t>(
      local.pawn + SCHEMA("C_BaseEntity", "m_nActualMoveType"_hash));
  if (move_type != cstypes::move_type::kLadder) {
    return;
  }

  const auto base = cmd->csgo_user_cmd.MutableBase();
  if (!base) {
    return;
  }

  const auto forward_move = base->Forwardmove();
  const auto side_move = base->Leftmove();

  if (forward_move == 0.0f && side_move == 0.0f) {
    return;
  }

  bool going_up{};

  if (std::fabsf(forward_move) > 0.01f) {
    going_up = forward_move > 0.0f;
  } else {
    const auto view_angles = systems::g_input.GetViewAngles();
    going_up = view_angles.x < 0.0f;
  }

  const auto view_angles = systems::g_input.GetViewAngles();
  auto modified = view_angles;
  modified.x = 89.0f;
  modified.y += going_up ? -90.0f : 90.0f;
  math::helpers::NormalizeAngles(modified);

  base->SetForwardmove(-1.0f);
  base->SetLeftmove(going_up ? 1.0f : -1.0f);

  auto buttons = cmd->buttons.value;
  buttons &= ~(cstypes::command_buttons::kInForward |
               cstypes::command_buttons::kInBack |
               cstypes::command_buttons::kInMoveleft |
               cstypes::command_buttons::kInMoveright);
  buttons |= cstypes::command_buttons::kInBack;
  buttons |= going_up ? cstypes::command_buttons::kInMoveleft
                      : cstypes::command_buttons::kInMoveright;
  cmd->buttons.value = buttons;

  base->MutableViewangles()->SetX(modified.x);
  base->MutableViewangles()->SetY(modified.y);
  base->MutableViewangles()->SetZ(0.0f);
}

}  // namespace features::movement
