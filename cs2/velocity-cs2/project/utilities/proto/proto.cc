module;

#include "platform/platform.h"

module velocity;

import <cstddef>;

namespace proto {

// has_bits

bool HasBits::Test(std::uint32_t mask) const {
  return (this->bits[0] & mask) != 0;
}
void HasBits::Set(std::uint32_t mask) { this->bits[0] |= mask; }
void HasBits::Clear(std::uint32_t mask) { this->bits[0] &= ~mask; }

// msg_qangle

float MsgQangle::X() const { return this->m_x; }
float MsgQangle::Y() const { return this->m_y; }
float MsgQangle::Z() const { return this->m_z; }
void MsgQangle::SetX(float v) {
  this->m_has_bits.Set(0x1u);
  this->m_x = v;
}
void MsgQangle::SetY(float v) {
  this->m_has_bits.Set(0x2u);
  this->m_y = v;
}
void MsgQangle::SetZ(float v) {
  this->m_has_bits.Set(0x4u);
  this->m_z = v;
}

// msg_vector

float MsgVector::X() const { return this->m_x; }
float MsgVector::Y() const { return this->m_y; }
float MsgVector::Z() const { return this->m_z; }
void MsgVector::SetX(float v) {
  this->m_has_bits.Set(0x1u);
  this->m_x = v;
}
void MsgVector::SetY(float v) {
  this->m_has_bits.Set(0x2u);
  this->m_y = v;
}
void MsgVector::SetZ(float v) {
  this->m_has_bits.Set(0x4u);
  this->m_z = v;
}

// interpolation_info

bool InterpolationInfo::HasFrac() const { return this->m_has_bits.Test(0x1u); }
float InterpolationInfo::Frac() const { return this->m_frac; }
void InterpolationInfo::SetFrac(float v) {
  this->m_has_bits.Set(0x1u);
  this->m_frac = v;
}
bool InterpolationInfo::HasSrcTick() const {
  return this->m_has_bits.Test(0x2u);
}
std::int32_t InterpolationInfo::SrcTick() const { return this->m_src_tick; }
void InterpolationInfo::SetSrcTick(std::int32_t v) {
  this->m_has_bits.Set(0x2u);
  this->m_src_tick = v;
}
bool InterpolationInfo::HasDstTick() const {
  return this->m_has_bits.Test(0x4u);
}
std::int32_t InterpolationInfo::DstTick() const { return this->m_dst_tick; }
void InterpolationInfo::SetDstTick(std::int32_t v) {
  this->m_has_bits.Set(0x4u);
  this->m_dst_tick = v;
}

// interpolation_info_cl

bool InterpolationInfoCl::HasFrac() const {
  return this->m_has_bits.Test(0x1u);
}
float InterpolationInfoCl::Frac() const { return this->m_frac; }
void InterpolationInfoCl::SetFrac(float v) {
  this->m_has_bits.Set(0x1u);
  this->m_frac = v;
}

// in_button_state_pb

std::uint64_t InButtonStatePb::Buttonstate1() const {
  return this->m_buttonstate1;
}
std::uint64_t InButtonStatePb::Buttonstate2() const {
  return this->m_buttonstate2;
}
std::uint64_t InButtonStatePb::Buttonstate3() const {
  return this->m_buttonstate3;
}
void InButtonStatePb::SetButtonstate1(std::uint64_t v) {
  this->m_has_bits.Set(0x1u);
  this->m_buttonstate1 = v;
}
void InButtonStatePb::SetButtonstate2(std::uint64_t v) {
  this->m_has_bits.Set(0x2u);
  this->m_buttonstate2 = v;
}
void InButtonStatePb::SetButtonstate3(std::uint64_t v) {
  this->m_has_bits.Set(0x4u);
  this->m_buttonstate3 = v;
}

// subtick_move_step

std::uint64_t SubtickMoveStep::Button() const { return this->m_button; }
void SubtickMoveStep::SetButton(std::uint64_t v) {
  this->m_has_bits.Set(0x1u);
  this->m_button = v;
}
bool SubtickMoveStep::Pressed() const { return this->m_pressed; }
void SubtickMoveStep::SetPressed(bool v) {
  this->m_has_bits.Set(0x2u);
  this->m_pressed = v;
}
float SubtickMoveStep::When() const { return this->m_when; }
void SubtickMoveStep::SetWhen(float v) {
  this->m_has_bits.Set(0x4u);
  this->m_when = v;
}
float SubtickMoveStep::AnalogForwardDelta() const {
  return this->m_analog_forward_delta;
}
void SubtickMoveStep::SetAnalogForwardDelta(float v) {
  this->m_has_bits.Set(0x8u);
  this->m_analog_forward_delta = v;
}
float SubtickMoveStep::AnalogLeftDelta() const {
  return this->m_analog_left_delta;
}
void SubtickMoveStep::SetAnalogLeftDelta(float v) {
  this->m_has_bits.Set(0x10u);
  this->m_analog_left_delta = v;
}
float SubtickMoveStep::PitchDelta() const { return this->m_pitch_delta; }
void SubtickMoveStep::SetPitchDelta(float v) {
  this->m_has_bits.Set(0x20u);
  this->m_pitch_delta = v;
}
float SubtickMoveStep::YawDelta() const { return this->m_yaw_delta; }
void SubtickMoveStep::SetYawDelta(float v) {
  this->m_has_bits.Set(0x40u);
  this->m_yaw_delta = v;
}

// base_usercmd_pb

bool BaseUsercmdPb::HasButtonsPb() const { return this->m_has_bits.Test(0x2u); }
const InButtonStatePb* BaseUsercmdPb::ButtonsPb() const {
  return ImplPtr<const InButtonStatePb>(this->m_buttons_pb);
}
InButtonStatePb* BaseUsercmdPb::MutableButtonsPb() {
  this->m_has_bits.Set(0x2u);
  return ImplPtr<InButtonStatePb>(this->m_buttons_pb);
}
bool BaseUsercmdPb::HasViewangles() const {
  return this->m_has_bits.Test(0x4u);
}
const MsgQangle* BaseUsercmdPb::Viewangles() const {
  return ImplPtr<const MsgQangle>(this->m_viewangles);
}
MsgQangle* BaseUsercmdPb::MutableViewangles() {
  this->m_has_bits.Set(0x4u);
  return ImplPtr<MsgQangle>(this->m_viewangles);
}
const RepeatedPtrField<SubtickMoveStep>& BaseUsercmdPb::SubtickMoves() const {
  return this->m_subtick_moves;
}
RepeatedPtrField<SubtickMoveStep>* BaseUsercmdPb::MutableSubtickMoves() {
  return &this->m_subtick_moves;
}
int BaseUsercmdPb::SubtickMovesSize() const {
  return this->m_subtick_moves.size();
}
SubtickMoveStep* BaseUsercmdPb::MutableSubtickMoves(int i) {
  return this->m_subtick_moves.MutableAt(i);
}

void BaseUsercmdPb::SetLegacyCommandNumber(std::int32_t v) {
  this->m_has_bits.Set(0x10u);
  this->m_legacy_command_number = v;
}
void BaseUsercmdPb::SetClientTick(std::int32_t v) {
  this->m_has_bits.Set(0x20u);
  this->m_client_tick = v;
}
void BaseUsercmdPb::SetForwardmove(float v) {
  this->m_has_bits.Set(0x40u);
  this->m_forwardmove = v;
}
void BaseUsercmdPb::SetLeftmove(float v) {
  this->m_has_bits.Set(0x80u);
  this->m_leftmove = v;
}
void BaseUsercmdPb::SetUpmove(float v) {
  this->m_has_bits.Set(0x100u);
  this->m_upmove = v;
}
void BaseUsercmdPb::SetImpulse(std::int32_t v) {
  this->m_has_bits.Set(0x200u);
  this->m_impulse = v;
}
void BaseUsercmdPb::SetWeaponselect(std::int32_t v) {
  this->m_has_bits.Set(0x400u);
  this->m_weaponselect = v;
}
void BaseUsercmdPb::SetRandomSeed(std::int32_t v) {
  this->m_has_bits.Set(0x800u);
  this->m_random_seed = v;
}
void BaseUsercmdPb::SetMousedx(std::int32_t v) {
  this->m_has_bits.Set(0x1000u);
  this->m_mousedx = v;
}
void BaseUsercmdPb::SetMousedy(std::int32_t v) {
  this->m_has_bits.Set(0x2000u);
  this->m_mousedy = v;
}
void BaseUsercmdPb::SetPredictionOffsetTicksX256(std::uint32_t v) {
  this->m_has_bits.Set(0x4000u);
  this->m_prediction_offset_ticks_x256 = v;
}
void BaseUsercmdPb::SetConsumedServerAngleChanges(std::uint32_t v) {
  this->m_has_bits.Set(0x8000u);
  this->m_consumed_server_angle_changes = v;
}
void BaseUsercmdPb::SetCmdFlags(std::int32_t v) {
  this->m_has_bits.Set(0x10000u);
  this->m_cmd_flags = v;
}
void BaseUsercmdPb::SetPawnEntityHandle(std::uint32_t v) {
  this->m_has_bits.Set(0x20000u);
  this->m_pawn_entity_handle = v;
}

std::int32_t BaseUsercmdPb::LegacyCommandNumber() const {
  return this->m_legacy_command_number;
}
std::int32_t BaseUsercmdPb::ClientTick() const { return this->m_client_tick; }
float BaseUsercmdPb::Forwardmove() const { return this->m_forwardmove; }
float BaseUsercmdPb::Leftmove() const { return this->m_leftmove; }
float BaseUsercmdPb::Upmove() const { return this->m_upmove; }
std::int32_t BaseUsercmdPb::Impulse() const { return this->m_impulse; }
std::int32_t BaseUsercmdPb::Weaponselect() const {
  return this->m_weaponselect;
}
std::int32_t BaseUsercmdPb::RandomSeed() const { return this->m_random_seed; }
std::int32_t BaseUsercmdPb::Mousedx() const { return this->m_mousedx; }
std::int32_t BaseUsercmdPb::Mousedy() const { return this->m_mousedy; }
std::uint32_t BaseUsercmdPb::PredictionOffsetTicksX256() const {
  return this->m_prediction_offset_ticks_x256;
}
std::uint32_t BaseUsercmdPb::ConsumedServerAngleChanges() const {
  return this->m_consumed_server_angle_changes;
}
std::int32_t BaseUsercmdPb::CmdFlags() const { return this->m_cmd_flags; }
std::uint32_t BaseUsercmdPb::PawnEntityHandle() const {
  return this->m_pawn_entity_handle;
}

bool BaseUsercmdPb::HasForwardmove() const {
  return this->m_has_bits.Test(0x40u);
}
bool BaseUsercmdPb::HasLeftmove() const { return this->m_has_bits.Test(0x80u); }
bool BaseUsercmdPb::HasUpmove() const { return this->m_has_bits.Test(0x100u); }
bool BaseUsercmdPb::HasPawnEntityHandle() const {
  return this->m_has_bits.Test(0x20000u);
}

// input_history_entry

bool InputHistoryEntry::HasViewAngles() const {
  return this->m_has_bits.Test(0x1u);
}
const MsgQangle* InputHistoryEntry::ViewAngles() const {
  return ImplPtr<const MsgQangle>(this->m_view_angles);
}
MsgQangle* InputHistoryEntry::MutableViewAngles() {
  this->m_has_bits.Set(0x1u);
  return ImplPtr<MsgQangle>(this->m_view_angles);
}
bool InputHistoryEntry::HasClInterp() const {
  return this->m_has_bits.Test(0x2u);
}
const InterpolationInfoCl* InputHistoryEntry::ClInterp() const {
  return ImplPtr<const InterpolationInfoCl>(this->m_cl_interp);
}
InterpolationInfoCl* InputHistoryEntry::MutableClInterp() {
  this->m_has_bits.Set(0x2u);
  return ImplPtr<InterpolationInfoCl>(this->m_cl_interp);
}
bool InputHistoryEntry::HasSvInterp0() const {
  return this->m_has_bits.Test(0x4u);
}
const InterpolationInfo* InputHistoryEntry::SvInterp0() const {
  return ImplPtr<const InterpolationInfo>(this->m_sv_interp0);
}
InterpolationInfo* InputHistoryEntry::MutableSvInterp0() {
  this->m_has_bits.Set(0x4u);
  return ImplPtr<InterpolationInfo>(this->m_sv_interp0);
}
bool InputHistoryEntry::HasSvInterp1() const {
  return this->m_has_bits.Test(0x8u);
}
const InterpolationInfo* InputHistoryEntry::SvInterp1() const {
  return ImplPtr<const InterpolationInfo>(this->m_sv_interp1);
}
InterpolationInfo* InputHistoryEntry::MutableSvInterp1() {
  this->m_has_bits.Set(0x8u);
  return ImplPtr<InterpolationInfo>(this->m_sv_interp1);
}
bool InputHistoryEntry::HasPlayerInterp() const {
  return this->m_has_bits.Test(0x10u);
}
const InterpolationInfo* InputHistoryEntry::PlayerInterp() const {
  return ImplPtr<const InterpolationInfo>(this->m_player_interp);
}
InterpolationInfo* InputHistoryEntry::MutablePlayerInterp() {
  this->m_has_bits.Set(0x10u);
  return ImplPtr<InterpolationInfo>(this->m_player_interp);
}
bool InputHistoryEntry::HasShootPosition() const {
  return this->m_has_bits.Test(0x20u);
}
const MsgVector* InputHistoryEntry::ShootPosition() const {
  return ImplPtr<const MsgVector>(this->m_shoot_position);
}
MsgVector* InputHistoryEntry::MutableShootPosition() {
  this->m_has_bits.Set(0x20u);
  return ImplPtr<MsgVector>(this->m_shoot_position);
}
bool InputHistoryEntry::HasTargetHeadPosCheck() const {
  return this->m_has_bits.Test(0x40u);
}
const MsgVector* InputHistoryEntry::TargetHeadPosCheck() const {
  return ImplPtr<const MsgVector>(this->m_target_head_pos_check);
}
MsgVector* InputHistoryEntry::MutableTargetHeadPosCheck() {
  this->m_has_bits.Set(0x40u);
  return ImplPtr<MsgVector>(this->m_target_head_pos_check);
}
bool InputHistoryEntry::HasTargetAbsPosCheck() const {
  return this->m_has_bits.Test(0x80u);
}
const MsgVector* InputHistoryEntry::TargetAbsPosCheck() const {
  return ImplPtr<const MsgVector>(this->m_target_abs_pos_check);
}
MsgVector* InputHistoryEntry::MutableTargetAbsPosCheck() {
  this->m_has_bits.Set(0x80u);
  return ImplPtr<MsgVector>(this->m_target_abs_pos_check);
}
bool InputHistoryEntry::HasTargetAbsAngCheck() const {
  return this->m_has_bits.Test(0x100u);
}
const MsgQangle* InputHistoryEntry::TargetAbsAngCheck() const {
  return ImplPtr<const MsgQangle>(this->m_target_abs_ang_check);
}
MsgQangle* InputHistoryEntry::MutableTargetAbsAngCheck() {
  this->m_has_bits.Set(0x100u);
  return ImplPtr<MsgQangle>(this->m_target_abs_ang_check);
}

bool InputHistoryEntry::HasRenderTickCount() const {
  return this->m_has_bits.Test(0x200u);
}
std::int32_t InputHistoryEntry::RenderTickCount() const {
  return this->m_render_tick_count;
}
void InputHistoryEntry::SetRenderTickCount(std::int32_t v) {
  this->m_has_bits.Set(0x200u);
  this->m_render_tick_count = v;
}

bool InputHistoryEntry::HasRenderTickFraction() const {
  return this->m_has_bits.Test(0x400u);
}
float InputHistoryEntry::RenderTickFraction() const {
  return this->m_render_tick_fraction;
}
void InputHistoryEntry::SetRenderTickFraction(float v) {
  this->m_has_bits.Set(0x400u);
  this->m_render_tick_fraction = v;
}

bool InputHistoryEntry::HasPlayerTickCount() const {
  return this->m_has_bits.Test(0x800u);
}
std::int32_t InputHistoryEntry::PlayerTickCount() const {
  return this->m_player_tick_count;
}
void InputHistoryEntry::SetPlayerTickCount(std::int32_t v) {
  this->m_has_bits.Set(0x800u);
  this->m_player_tick_count = v;
}

bool InputHistoryEntry::HasPlayerTickFraction() const {
  return this->m_has_bits.Test(0x1000u);
}
float InputHistoryEntry::PlayerTickFraction() const {
  return this->m_player_tick_fraction;
}
void InputHistoryEntry::SetPlayerTickFraction(float v) {
  this->m_has_bits.Set(0x1000u);
  this->m_player_tick_fraction = v;
}

bool InputHistoryEntry::HasFrameNumber() const {
  return this->m_has_bits.Test(0x2000u);
}
std::int32_t InputHistoryEntry::FrameNumber() const {
  return this->m_frame_number;
}
void InputHistoryEntry::SetFrameNumber(std::int32_t v) {
  this->m_has_bits.Set(0x2000u);
  this->m_frame_number = v;
}

bool InputHistoryEntry::HasTargetEntIndex() const {
  return this->m_has_bits.Test(0x4000u);
}
std::int32_t InputHistoryEntry::TargetEntIndex() const {
  return this->m_target_ent_index;
}
void InputHistoryEntry::SetTargetEntIndex(std::int32_t v) {
  this->m_has_bits.Set(0x4000u);
  this->m_target_ent_index = v;
}

// csgo_usercmd_pb

bool CsgoUsercmdPb::HasBase() const { return this->m_has_bits.Test(0x1u); }
BaseUsercmdPb* CsgoUsercmdPb::MutableBase() {
  this->m_has_bits.Set(0x1u);
  return ImplPtr<BaseUsercmdPb>(this->m_base);
}
const BaseUsercmdPb* CsgoUsercmdPb::Base() const {
  return ImplPtr<const BaseUsercmdPb>(this->m_base);
}

int CsgoUsercmdPb::InputHistorySize() const {
  return this->m_input_history.size();
}
InputHistoryEntry* CsgoUsercmdPb::MutableInputHistory(int i) {
  return this->m_input_history.MutableAt(i);
}
RepeatedPtrField<InputHistoryEntry>* CsgoUsercmdPb::MutableInputHistory() {
  return &this->m_input_history;
}

void CsgoUsercmdPb::SetLeftHandDesired(bool v) {
  this->m_has_bits.Set(0x2u);
  this->m_left_hand_desired = v;
}
void CsgoUsercmdPb::SetIsPredictingBodyShotFx(bool v) {
  this->m_has_bits.Set(0x4u);
  this->m_is_predicting_body_shot_fx = v;
}
void CsgoUsercmdPb::SetIsPredictingHeadShotFx(bool v) {
  this->m_has_bits.Set(0x8u);
  this->m_is_predicting_head_shot_fx = v;
}
void CsgoUsercmdPb::SetIsPredictingKillRagdolls(bool v) {
  this->m_has_bits.Set(0x10u);
  this->m_is_predicting_kill_ragdolls = v;
}
void CsgoUsercmdPb::SetAttack1StartHistoryIndex(std::int32_t v) {
  this->m_has_bits.Set(0x20u);
  this->m_attack1_start_history_index = v;
}
void CsgoUsercmdPb::SetAttack2StartHistoryIndex(std::int32_t v) {
  this->m_has_bits.Set(0x40u);
  this->m_attack2_start_history_index = v;
}

bool CsgoUsercmdPb::LeftHandDesired() const {
  return this->m_left_hand_desired;
}
bool CsgoUsercmdPb::IsPredictingBodyShotFx() const {
  return this->m_is_predicting_body_shot_fx;
}
bool CsgoUsercmdPb::IsPredictingHeadShotFx() const {
  return this->m_is_predicting_head_shot_fx;
}
bool CsgoUsercmdPb::IsPredictingKillRagdolls() const {
  return this->m_is_predicting_kill_ragdolls;
}
std::int32_t CsgoUsercmdPb::Attack1StartHistoryIndex() const {
  return this->m_attack1_start_history_index;
}
std::int32_t CsgoUsercmdPb::Attack2StartHistoryIndex() const {
  return this->m_attack2_start_history_index;
}

}  // namespace proto
