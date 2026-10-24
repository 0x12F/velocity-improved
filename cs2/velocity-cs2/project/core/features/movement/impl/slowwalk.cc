module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>
#include <external/xdraw/xdraw.hpp>

module velocity;

import <cstddef>;
import <filesystem>;
import <limits>;
import <string_view>;
import <type_traits>;

namespace features::movement {

void Slowwalk::OnCreateMove(systems::Input::Usercmd* cmd) const {
  if (!settings::g_movement.slowwalk.value) {
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

  const auto base = cmd->csgo_user_cmd.MutableBase();
  const auto forward_move = base->Forwardmove();
  const auto side_move = base->Leftmove();

  if (forward_move == 0.0f && side_move == 0.0f) {
    return;
  }

  const auto target_ratio = settings::g_movement.slowwalk_speed / 100.0f;
  const auto move_length =
      std::sqrtf(forward_move * forward_move + side_move * side_move);

  if (move_length < 0.001f) {
    return;
  }

  const auto scaled = std::fminf(move_length, target_ratio);

  base->SetForwardmove((forward_move / move_length) * scaled);
  base->SetLeftmove((side_move / move_length) * scaled);
}

}  // namespace features::movement
