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

void Input::Update() {
  const auto local_controller =
      memory::Read<std::uintptr_t>(addresses::globals::local_player_controller);
  if (!local_controller) {
    this->m_current_cmd_ = nullptr;
    return;
  }

  this->m_current_cmd_ = this->GetCurrentCmd(local_controller);
}

void Input::Apply() {
  const auto local = systems::g_local.Get();

  if (!this->m_current_cmd_) {
    return;
  }

  const auto base = this->m_current_cmd_->csgo_user_cmd.MutableBase();
  if (!base) {
    return;
  }

  auto has_move_subticks = [](proto::BaseUsercmdPb* base_cmd) {
    for (auto i = 0; i < base_cmd->SubtickMovesSize(); ++i) {
      proto::SubtickMoveStep* step = base_cmd->MutableSubtickMoves(i);
      if (!step) continue;

      if (step->m_has_bits.Test(0x8) || step->m_has_bits.Test(0x10))
        return true;

      if (step->m_has_bits.Test(0x1)) return true;
    }

    return false;
  };

  diag::SetExceptionPhase("input apply: subtick movement");
  if (!has_move_subticks(base)) {
    const auto movement_services =
        local.pawn ? memory::Read<std::uintptr_t>(
                         local.pawn +
                         SCHEMA("C_BasePlayerPawn", "m_pMovementServices"_hash))
                   : 0;

    if (movement_services) {
      const auto forward_delta =
          base->Forwardmove() -
          memory::Read<float>(
              movement_services +
              SCHEMA("CPlayer_MovementServices", "m_flCmdForwardMove"_hash));
      const auto left_delta =
          base->Leftmove() -
          memory::Read<float>(
              movement_services +
              SCHEMA("CPlayer_MovementServices", "m_flCmdLeftMove"_hash));

      const auto has_meaningful_delta = std::isfinite(forward_delta) &&
                                        std::isfinite(left_delta) &&
                                        (std::fabsf(forward_delta) > 0.0001f ||
                                         std::fabsf(left_delta) > 0.0001f);

      if (has_meaningful_delta) {
        const auto step =
            systems::g_input.AcquireSubtickStep(base->MutableSubtickMoves());
        if (!step) return;

        step->SetButton(0);
        step->SetPressed(false);
        step->SetWhen(0.0f);
        step->SetAnalogForwardDelta(forward_delta);
        step->SetAnalogLeftDelta(left_delta);
      }
    }
  }

  diag::SetExceptionPhase("input apply: buttons");
  auto buttons = const_cast<proto::InButtonStatePb*>(base->ButtonsPb());
  if (!buttons) {
    const auto raw_base =
        reinterpret_cast<std::uintptr_t>(base) - proto::kMessageImplOffset;
    const auto arena_bits = memory::Read<std::uintptr_t>(raw_base + 0x08);
    auto arena = arena_bits & ~0x3ull;
    if (arena_bits & 1) {
      arena = memory::Read<std::uintptr_t>(arena);
    }

    const auto raw_buttons =
        memory::Call<void*>(PATTERN(patterns::button_state_alloc), arena);
    if (raw_buttons) {
      base->m_buttons_pb =
          reinterpret_cast<proto::InButtonStatePb*>(raw_buttons);
      buttons = proto::ImplPtr<proto::InButtonStatePb>(raw_buttons);
    }
  }

  if (buttons) {
    base->m_has_bits.Set(0x2u);
    buttons->SetButtonstate1(this->m_current_cmd_->buttons.value);
    buttons->SetButtonstate2(this->m_current_cmd_->buttons.value_changed);
    buttons->SetButtonstate3(this->m_current_cmd_->buttons.value_scroll);
  }

  diag::SetExceptionPhase("input apply: crc");
  this->CalculateCrc(base);
}

Input::Usercmd* Input::GetCurrentCmd(std::uintptr_t local_controller) const {
  const auto get_usercmd_base = PATTERN(patterns::get_usercmd_base);
  const auto get_usercmd = PATTERN(patterns::get_usercmd);
  if (!get_usercmd_base || !get_usercmd) {
    return nullptr;
  }

  const auto usercmd_base =
      memory::Call<std::uintptr_t>(get_usercmd_base, local_controller);
  if (!usercmd_base) {
    return nullptr;
  }

  const auto sequence = memory::SafeRead<int>(usercmd_base + 0x5910);
  if (!sequence) {
    return nullptr;
  }

  return memory::Call<Usercmd*>(get_usercmd, local_controller, *sequence);
}

proto::SubtickMoveStep* Input::AcquireSubtickStep(
    proto::RepeatedPtrField<proto::SubtickMoveStep>* subtick_moves) const {
  if (!subtick_moves) {
    return nullptr;
  }

  if (subtick_moves->m_rep) {
    if (subtick_moves->m_current_size < subtick_moves->m_rep->allocated_size) {
      const auto element =
          subtick_moves->m_rep->elements[subtick_moves->m_current_size];
      if (element) {
        subtick_moves->m_current_size++;
        const auto step = proto::ImplPtr<proto::SubtickMoveStep>(element);
        *step = {};
        return step;
      }
    }
  }

  const auto move_step = memory::Call<void*>(
      PATTERN(patterns::subtick_move_alloc), subtick_moves->m_arena);
  if (move_step) {
    memory::Call<std::uintptr_t>(
        PATTERN(patterns::utl_vector_push),
        reinterpret_cast<std::uintptr_t>(subtick_moves),
        reinterpret_cast<std::uintptr_t>(move_step));
    return proto::ImplPtr<proto::SubtickMoveStep>(move_step);
  }

  return nullptr;
}

math::Vector3 Input::GetViewAngles() const {
  return *memory::Call<math::Vector3*>(PATTERN(patterns::get_view_angles),
                                       addresses::globals::csgo_input, 0);
}

proto::InputHistoryEntry* Input::PushInputHistory(
    Usercmd* cmd, const InputHistoryParams& params) const {
  auto history_field = cmd->csgo_user_cmd.MutableInputHistory();
  if (!history_field) {
    return nullptr;
  }

  proto::InputHistoryEntry* entry{nullptr};

  if (history_field->m_rep) {
    if (history_field->m_current_size < history_field->m_rep->allocated_size) {
      auto element =
          history_field->m_rep->elements[history_field->m_current_size];
      if (element) {
        history_field->m_current_size++;
        entry = proto::ImplPtr<proto::InputHistoryEntry>(element);
      }
    }
  }

  if (!entry) {
    auto raw = memory::Call<void*>(PATTERN(patterns::history_field_alloc),
                                   history_field->m_arena);
    if (!raw) {
      return nullptr;
    }

    memory::Call<std::uintptr_t>(
        PATTERN(patterns::utl_vector_push),
        reinterpret_cast<std::uintptr_t>(history_field),
        reinterpret_cast<std::uintptr_t>(raw));

    entry = proto::ImplPtr<proto::InputHistoryEntry>(raw);
  }

  if (!entry) {
    return nullptr;
  }

  entry->SetRenderTickCount(params.render_tick);
  entry->SetRenderTickFraction(params.render_frac);
  entry->SetPlayerTickCount(params.player_tick);
  entry->SetPlayerTickFraction(params.player_frac);
  entry->SetFrameNumber(params.frame_number);
  entry->SetTargetEntIndex(params.target_ent_index);

  if (auto va = entry->MutableViewAngles()) {
    va->SetX(params.view_angles.x);
    va->SetY(params.view_angles.y);
    va->SetZ(params.view_angles.z);
  }

  if (auto sp = entry->MutableShootPosition()) {
    sp->SetX(params.shoot_position.x);
    sp->SetY(params.shoot_position.y);
    sp->SetZ(params.shoot_position.z);
  }

  if (auto ci = entry->MutableClInterp()) {
    ci->SetFrac(params.cl_interp_frac);
  }

  if (auto si0 = entry->MutableSvInterp0()) {
    si0->SetSrcTick(params.sv_interp0_src);
    si0->SetDstTick(params.sv_interp0_dst);
    si0->SetFrac(params.sv_interp0_frac);
  }

  if (auto si1 = entry->MutableSvInterp1()) {
    si1->SetSrcTick(params.sv_interp1_src);
    si1->SetDstTick(params.sv_interp1_dst);
    si1->SetFrac(params.sv_interp1_frac);
  }

  if (auto pi = entry->MutablePlayerInterp()) {
    pi->SetSrcTick(params.player_interp_src);
    pi->SetDstTick(params.player_interp_dst);
    pi->SetFrac(params.player_interp_frac);
  }

  if (params.fill_cheat_check_data) {
    if (auto head = entry->MutableTargetHeadPosCheck()) {
      head->SetX(params.target_head_pos.x);
      head->SetY(params.target_head_pos.y);
      head->SetZ(params.target_head_pos.z);
    }

    if (auto abs_pos = entry->MutableTargetAbsPosCheck()) {
      abs_pos->SetX(params.target_abs_pos.x);
      abs_pos->SetY(params.target_abs_pos.y);
      abs_pos->SetZ(params.target_abs_pos.z);
    }

    if (auto abs_ang = entry->MutableTargetAbsAngCheck()) {
      abs_ang->SetX(params.target_abs_ang.x);
      abs_ang->SetY(params.target_abs_ang.y);
      abs_ang->SetZ(params.target_abs_ang.z);
    }
  }

  return entry;
}

void Input::SetViewAngles(const math::Vector3& angles) const {
  memory::Call<void>(PATTERN(patterns::set_view_angles),
                     addresses::globals::csgo_input, 0, &angles);
}

void Input::Desubtick(Usercmd* cmd) const {
  cmd->csgo_user_cmd.MutableBase()->MutableSubtickMoves()->Clear();
}

void Input::SetWeaponSelect(Usercmd* cmd, std::uintptr_t csgo_input) const {
  const auto frame_data = csgo_input + 552;
  const auto weapon_handle = memory::Read<uint32_t>(frame_data + 1132);

  if (!weapon_handle) {
    return;
  }

  const auto weapon = systems::g_entities.Lookup(weapon_handle);
  if (!weapon) {
    return;
  }

  auto entity_index{-1};
  memory::Call<int*>(PATTERN(patterns::weapon_get_entity_index), weapon,
                     &entity_index);

  if (entity_index == -1) {
    return;
  }

  const auto base = cmd->csgo_user_cmd.MutableBase();
  if (base) {
    base->SetWeaponselect(entity_index);
  }
}

Input::Usercmd* Input::GetCommandBySequence(std::uintptr_t local_controller,
                                            int sequence) const {
  const auto get_usercmd_base = PATTERN(patterns::get_usercmd_base);
  if (!get_usercmd_base) {
    return nullptr;
  }

  const auto usercmd_base =
      memory::Call<std::uintptr_t>(get_usercmd_base, local_controller);
  if (!usercmd_base) {
    return nullptr;
  }

  const auto index = sequence % 150;
  return reinterpret_cast<Usercmd*>(usercmd_base + sizeof(Usercmd) * index);
}

bool Input::IsSubtickOverwrite(Usercmd* cmd) const {
  return memory::Read<int>(reinterpret_cast<std::uintptr_t>(cmd) + 148) == 2;
}

bool Input::CalculateCrc(proto::BaseUsercmdPb* base) const {
  if (!base) {
    return false;
  }

  const auto string_copy = PATTERN(patterns::string_copy);
  const auto serialize_move_crc = PATTERN(patterns::serialize_move_crc);
  if (!string_copy || !serialize_move_crc) {
    diag::Write(diag::Level::kError,
                "input CRC helpers unavailable; capturing a pre-termination "
                "diagnostic dump");
    diag::CaptureSnapshot("input CRC helpers unavailable");
    return false;
  }

  const auto btns = base->ButtonsPb();
  const auto va = base->Viewangles();

  std::uint8_t buf[64]{};
  std::uint8_t btn_size{0};

  auto p = buf;

  auto bs1 = btns ? btns->Buttonstate1() : 0;
  auto bs2 = btns ? btns->Buttonstate2() : 0;
  auto bs3 = btns ? btns->Buttonstate3() : 0;

  if (bs1) {
    btn_size += 9;
  }
  if (bs2) {
    btn_size += 9;
  }
  if (bs3) {
    btn_size += 9;
  }

  if (btn_size > 0) {
    *p++ = 0x1a;
    *p++ = btn_size;

    if (bs1) {
      *p++ = 0x09;
      std::memcpy(p, &bs1, 8);
      p += 8;
    }
    if (bs2) {
      *p++ = 0x11;
      std::memcpy(p, &bs2, 8);
      p += 8;
    }
    if (bs3) {
      *p++ = 0x19;
      std::memcpy(p, &bs3, 8);
      p += 8;
    }
  }

  auto pitch = va ? va->X() : 0.0f;
  auto yaw = va ? va->Y() : 0.0f;
  auto roll = va ? va->Z() : 0.0f;

  std::uint8_t va_size{0};
  if (pitch != 0.0f) {
    va_size += 5;
  }
  if (yaw != 0.0f) {
    va_size += 5;
  }
  if (roll != 0.0f) {
    va_size += 5;
  }

  if (va_size > 0) {
    *p++ = 0x22;
    *p++ = va_size;

    if (pitch != 0.0f) {
      *p++ = 0x0d;
      std::memcpy(p, &pitch, 4);
      p += 4;
    }
    if (yaw != 0.0f) {
      *p++ = 0x15;
      std::memcpy(p, &yaw, 4);
      p += 4;
    }
    if (roll != 0.0f) {
      *p++ = 0x1d;
      std::memcpy(p, &roll, 4);
      p += 4;
    }
  }

  auto total = static_cast<int>(p - buf);

  base->m_has_bits.Set(0x1u);

  auto raw_msg =
      reinterpret_cast<std::uintptr_t>(base) - proto::kMessageImplOffset;
  auto arena_raw = memory::Read<std::uintptr_t>(raw_msg + 0x08);
  auto arena = arena_raw & ~0x3ull;

  if (arena_raw & 1) {
    arena = memory::Read<std::uintptr_t>(arena);
  }

  std::uint8_t msg[0x18]{};
  memory::Call<void>(string_copy, reinterpret_cast<std::uintptr_t>(msg),
                     reinterpret_cast<std::uintptr_t>(buf), total);
  memory::Call<void>(serialize_move_crc, &base->m_move_crc,
                     reinterpret_cast<std::uintptr_t>(msg), arena);

  return true;
}

}  // namespace systems
