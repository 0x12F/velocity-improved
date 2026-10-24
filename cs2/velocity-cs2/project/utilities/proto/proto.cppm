module;

#include "platform/platform.h"

export module velocity:utilities.proto.proto;

import <cstddef>;
import <cstdint>;

export {
namespace proto {

constexpr std::uintptr_t kMessageImplOffset{0x10};

template <typename T>
[[nodiscard]] inline T* ImplPtr(void* msg) {
  if (!msg) {
    return nullptr;
  }

  return reinterpret_cast<T*>(reinterpret_cast<std::uintptr_t>(msg) +
                              kMessageImplOffset);
}

template <typename T>
[[nodiscard]] inline const T* ImplPtr(const void* msg) {
  if (!msg) {
    return nullptr;
  }

  return reinterpret_cast<const T*>(reinterpret_cast<std::uintptr_t>(msg) +
                                    kMessageImplOffset);
}

template <typename T>
struct RepeatedPtrField {
  void* m_arena;
  int m_current_size;
  int m_total_size;

  struct RepT {
    int allocated_size;
    void* elements[1];
  };

  RepT* m_rep;

  [[nodiscard]] bool empty() const { return this->m_current_size == 0; }
  [[nodiscard]] int size() const { return this->m_current_size; }

  [[nodiscard]] T* MutableAt(int index) {
    return ImplPtr<T>(this->m_rep->elements[index]);
  }

  [[nodiscard]] T* Add() {
    if (this->m_rep != nullptr &&
        this->m_current_size < this->m_rep->allocated_size) {
      return impl_ptr<T>(this->m_rep->elements[this->m_current_size++]);
    }

    return nullptr;
  }

  void Clear() { this->m_current_size = 0; }
};

struct HasBits {
  std::uint32_t bits[1]{};

  [[nodiscard]] bool Test(std::uint32_t mask) const;
  void Set(std::uint32_t mask);
  void Clear(std::uint32_t mask);
};

struct MsgQangle {
  HasBits m_has_bits;
  std::uint32_t m_cached_size;
  float m_x;
  float m_y;
  float m_z;

  [[nodiscard]] float X() const;
  [[nodiscard]] float Y() const;
  [[nodiscard]] float Z() const;
  void SetX(float v);
  void SetY(float v);
  void SetZ(float v);
};

struct MsgVector {
  HasBits m_has_bits;
  std::uint32_t m_cached_size;
  float m_x;
  float m_y;
  float m_z;
  float m_w;

  [[nodiscard]] float X() const;
  [[nodiscard]] float Y() const;
  [[nodiscard]] float Z() const;
  void SetX(float v);
  void SetY(float v);
  void SetZ(float v);
};

struct InterpolationInfo {
  HasBits m_has_bits;
  std::uint32_t m_cached_size;
  float m_frac;
  std::int32_t m_src_tick;
  std::int32_t m_dst_tick;

  [[nodiscard]] bool HasFrac() const;
  [[nodiscard]] float Frac() const;
  void SetFrac(float v);
  [[nodiscard]] bool HasSrcTick() const;
  [[nodiscard]] std::int32_t SrcTick() const;
  void SetSrcTick(std::int32_t v);
  [[nodiscard]] bool HasDstTick() const;
  [[nodiscard]] std::int32_t DstTick() const;
  void SetDstTick(std::int32_t v);
};

struct InterpolationInfoCl {
  HasBits m_has_bits;
  std::uint32_t m_cached_size;
  float m_frac;

  [[nodiscard]] bool HasFrac() const;
  [[nodiscard]] float Frac() const;
  void SetFrac(float v);
};

struct InButtonStatePb {
  HasBits m_has_bits;
  std::uint32_t m_cached_size;
  std::uint64_t m_buttonstate1;
  std::uint64_t m_buttonstate2;
  std::uint64_t m_buttonstate3;

  [[nodiscard]] std::uint64_t Buttonstate1() const;
  [[nodiscard]] std::uint64_t Buttonstate2() const;
  [[nodiscard]] std::uint64_t Buttonstate3() const;
  void SetButtonstate1(std::uint64_t v);
  void SetButtonstate2(std::uint64_t v);
  void SetButtonstate3(std::uint64_t v);
};

struct SubtickMoveStep {
  HasBits m_has_bits;
  std::uint32_t m_cached_size;
  std::uint64_t m_button;
  bool m_pressed;
  float m_when;
  float m_analog_forward_delta;
  float m_analog_left_delta;
  float m_pitch_delta;
  float m_yaw_delta;

  [[nodiscard]] std::uint64_t Button() const;
  void SetButton(std::uint64_t v);
  [[nodiscard]] bool Pressed() const;
  void SetPressed(bool v);
  [[nodiscard]] float When() const;
  void SetWhen(float v);
  [[nodiscard]] float AnalogForwardDelta() const;
  void SetAnalogForwardDelta(float v);
  [[nodiscard]] float AnalogLeftDelta() const;
  void SetAnalogLeftDelta(float v);
  [[nodiscard]] float PitchDelta() const;
  void SetPitchDelta(float v);
  [[nodiscard]] float YawDelta() const;
  void SetYawDelta(float v);
};

struct BaseUsercmdPb {
  HasBits m_has_bits;
  std::uint32_t m_cached_size;
  RepeatedPtrField<SubtickMoveStep> m_subtick_moves;
  void* m_move_crc;
  InButtonStatePb* m_buttons_pb;
  MsgQangle* m_viewangles;
  void* m_execution_notes;
  std::int32_t m_legacy_command_number;
  std::int32_t m_client_tick;
  float m_forwardmove;
  float m_leftmove;
  float m_upmove;
  std::int32_t m_impulse;
  std::int32_t m_weaponselect;
  std::int32_t m_random_seed;
  std::int32_t m_mousedx;
  std::int32_t m_mousedy;
  std::uint32_t m_prediction_offset_ticks_x256;
  std::uint32_t m_consumed_server_angle_changes;
  std::int32_t m_cmd_flags;
  std::uint32_t m_pawn_entity_handle;

  [[nodiscard]] bool HasButtonsPb() const;
  [[nodiscard]] const InButtonStatePb* ButtonsPb() const;
  [[nodiscard]] InButtonStatePb* MutableButtonsPb();
  [[nodiscard]] bool HasViewangles() const;
  [[nodiscard]] const MsgQangle* Viewangles() const;
  [[nodiscard]] MsgQangle* MutableViewangles();
  [[nodiscard]] const RepeatedPtrField<SubtickMoveStep>& SubtickMoves() const;
  [[nodiscard]] RepeatedPtrField<SubtickMoveStep>* MutableSubtickMoves();
  [[nodiscard]] int SubtickMovesSize() const;
  [[nodiscard]] SubtickMoveStep* MutableSubtickMoves(int i);

  void SetLegacyCommandNumber(std::int32_t v);
  void SetClientTick(std::int32_t v);
  void SetForwardmove(float v);
  void SetLeftmove(float v);
  void SetUpmove(float v);
  void SetImpulse(std::int32_t v);
  void SetWeaponselect(std::int32_t v);
  void SetRandomSeed(std::int32_t v);
  void SetMousedx(std::int32_t v);
  void SetMousedy(std::int32_t v);
  void SetPredictionOffsetTicksX256(std::uint32_t v);
  void SetConsumedServerAngleChanges(std::uint32_t v);
  void SetCmdFlags(std::int32_t v);
  void SetPawnEntityHandle(std::uint32_t v);

  [[nodiscard]] std::int32_t LegacyCommandNumber() const;
  [[nodiscard]] std::int32_t ClientTick() const;
  [[nodiscard]] float Forwardmove() const;
  [[nodiscard]] float Leftmove() const;
  [[nodiscard]] float Upmove() const;
  [[nodiscard]] std::int32_t Impulse() const;
  [[nodiscard]] std::int32_t Weaponselect() const;
  [[nodiscard]] std::int32_t RandomSeed() const;
  [[nodiscard]] std::int32_t Mousedx() const;
  [[nodiscard]] std::int32_t Mousedy() const;
  [[nodiscard]] std::uint32_t PredictionOffsetTicksX256() const;
  [[nodiscard]] std::uint32_t ConsumedServerAngleChanges() const;
  [[nodiscard]] std::int32_t CmdFlags() const;
  [[nodiscard]] std::uint32_t PawnEntityHandle() const;

  [[nodiscard]] bool HasForwardmove() const;
  [[nodiscard]] bool HasLeftmove() const;
  [[nodiscard]] bool HasUpmove() const;
  [[nodiscard]] bool HasPawnEntityHandle() const;
};

struct InputHistoryEntry {
  HasBits m_has_bits;
  std::uint32_t m_cached_size;
  MsgQangle* m_view_angles;
  InterpolationInfoCl* m_cl_interp;
  InterpolationInfo* m_sv_interp0;
  InterpolationInfo* m_sv_interp1;
  InterpolationInfo* m_player_interp;
  MsgVector* m_shoot_position;
  MsgVector* m_target_head_pos_check;
  MsgVector* m_target_abs_pos_check;
  MsgQangle* m_target_abs_ang_check;
  std::int32_t m_render_tick_count;
  float m_render_tick_fraction;
  std::int32_t m_player_tick_count;
  float m_player_tick_fraction;
  std::int32_t m_frame_number;
  std::int32_t m_target_ent_index;

  [[nodiscard]] bool HasViewAngles() const;
  [[nodiscard]] const MsgQangle* ViewAngles() const;
  [[nodiscard]] MsgQangle* MutableViewAngles();
  [[nodiscard]] bool HasClInterp() const;
  [[nodiscard]] const InterpolationInfoCl* ClInterp() const;
  [[nodiscard]] InterpolationInfoCl* MutableClInterp();
  [[nodiscard]] bool HasSvInterp0() const;
  [[nodiscard]] const InterpolationInfo* SvInterp0() const;
  [[nodiscard]] InterpolationInfo* MutableSvInterp0();
  [[nodiscard]] bool HasSvInterp1() const;
  [[nodiscard]] const InterpolationInfo* SvInterp1() const;
  [[nodiscard]] InterpolationInfo* MutableSvInterp1();
  [[nodiscard]] bool HasPlayerInterp() const;
  [[nodiscard]] const InterpolationInfo* PlayerInterp() const;
  [[nodiscard]] InterpolationInfo* MutablePlayerInterp();
  [[nodiscard]] bool HasShootPosition() const;
  [[nodiscard]] const MsgVector* ShootPosition() const;
  [[nodiscard]] MsgVector* MutableShootPosition();
  [[nodiscard]] bool HasTargetHeadPosCheck() const;
  [[nodiscard]] const MsgVector* TargetHeadPosCheck() const;
  [[nodiscard]] MsgVector* MutableTargetHeadPosCheck();
  [[nodiscard]] bool HasTargetAbsPosCheck() const;
  [[nodiscard]] const MsgVector* TargetAbsPosCheck() const;
  [[nodiscard]] MsgVector* MutableTargetAbsPosCheck();
  [[nodiscard]] bool HasTargetAbsAngCheck() const;
  [[nodiscard]] const MsgQangle* TargetAbsAngCheck() const;
  [[nodiscard]] MsgQangle* MutableTargetAbsAngCheck();

  [[nodiscard]] bool HasRenderTickCount() const;
  [[nodiscard]] std::int32_t RenderTickCount() const;
  void SetRenderTickCount(std::int32_t v);
  [[nodiscard]] bool HasRenderTickFraction() const;
  [[nodiscard]] float RenderTickFraction() const;
  void SetRenderTickFraction(float v);
  [[nodiscard]] bool HasPlayerTickCount() const;
  [[nodiscard]] std::int32_t PlayerTickCount() const;
  void SetPlayerTickCount(std::int32_t v);
  [[nodiscard]] bool HasPlayerTickFraction() const;
  [[nodiscard]] float PlayerTickFraction() const;
  void SetPlayerTickFraction(float v);
  [[nodiscard]] bool HasFrameNumber() const;
  [[nodiscard]] std::int32_t FrameNumber() const;
  void SetFrameNumber(std::int32_t v);
  [[nodiscard]] bool HasTargetEntIndex() const;
  [[nodiscard]] std::int32_t TargetEntIndex() const;
  void SetTargetEntIndex(std::int32_t v);
};

struct CsgoUsercmdPb {
  HasBits m_has_bits;
  std::uint32_t m_cached_size;
  RepeatedPtrField<InputHistoryEntry> m_input_history;
  BaseUsercmdPb* m_base;
  bool m_left_hand_desired;
  bool m_is_predicting_body_shot_fx;
  bool m_is_predicting_head_shot_fx;
  bool m_is_predicting_kill_ragdolls;
  std::int32_t m_attack1_start_history_index;
  std::int32_t m_attack2_start_history_index;

  [[nodiscard]] bool HasBase() const;
  [[nodiscard]] BaseUsercmdPb* MutableBase();
  [[nodiscard]] const BaseUsercmdPb* Base() const;

  [[nodiscard]] int InputHistorySize() const;
  [[nodiscard]] InputHistoryEntry* MutableInputHistory(int i);
  [[nodiscard]] RepeatedPtrField<InputHistoryEntry>* MutableInputHistory();

  void SetLeftHandDesired(bool v);
  void SetIsPredictingBodyShotFx(bool v);
  void SetIsPredictingHeadShotFx(bool v);
  void SetIsPredictingKillRagdolls(bool v);
  void SetAttack1StartHistoryIndex(std::int32_t v);
  void SetAttack2StartHistoryIndex(std::int32_t v);

  [[nodiscard]] bool LeftHandDesired() const;
  [[nodiscard]] bool IsPredictingBodyShotFx() const;
  [[nodiscard]] bool IsPredictingHeadShotFx() const;
  [[nodiscard]] bool IsPredictingKillRagdolls() const;
  [[nodiscard]] std::int32_t Attack1StartHistoryIndex() const;
  [[nodiscard]] std::int32_t Attack2StartHistoryIndex() const;
};

}  // namespace proto
}  // export
