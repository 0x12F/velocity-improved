#include <pch/pch.hpp>
#include "proto.hpp"

namespace proto {

	// has_bits

	bool has_bits::test( std::uint32_t mask ) const { return ( this->bits[ 0 ] & mask ) != 0; }
	void has_bits::set( std::uint32_t mask ) { this->bits[ 0 ] |= mask; }
	void has_bits::clear( std::uint32_t mask ) { this->bits[ 0 ] &= ~mask; }

	// msg_qangle

	float msg_qangle::x( ) const { return this->m_x; }
	float msg_qangle::y( ) const { return this->m_y; }
	float msg_qangle::z( ) const { return this->m_z; }
	void msg_qangle::set_x( float v ) { this->m_has_bits.set( 0x1u ); this->m_x = v; }
	void msg_qangle::set_y( float v ) { this->m_has_bits.set( 0x2u ); this->m_y = v; }
	void msg_qangle::set_z( float v ) { this->m_has_bits.set( 0x4u ); this->m_z = v; }

	// msg_vector

	float msg_vector::x( ) const { return this->m_x; }
	float msg_vector::y( ) const { return this->m_y; }
	float msg_vector::z( ) const { return this->m_z; }
	void msg_vector::set_x( float v ) { this->m_has_bits.set( 0x1u ); this->m_x = v; }
	void msg_vector::set_y( float v ) { this->m_has_bits.set( 0x2u ); this->m_y = v; }
	void msg_vector::set_z( float v ) { this->m_has_bits.set( 0x4u ); this->m_z = v; }

	// interpolation_info

	bool interpolation_info::has_frac( ) const { return this->m_has_bits.test( 0x1u ); }
	float interpolation_info::frac( ) const { return this->m_frac; }
	void interpolation_info::set_frac( float v ) { this->m_has_bits.set( 0x1u ); this->m_frac = v; }
	bool interpolation_info::has_src_tick( ) const { return this->m_has_bits.test( 0x2u ); }
	std::int32_t interpolation_info::src_tick( ) const { return this->m_src_tick; }
	void interpolation_info::set_src_tick( std::int32_t v ) { this->m_has_bits.set( 0x2u ); this->m_src_tick = v; }
	bool interpolation_info::has_dst_tick( ) const { return this->m_has_bits.test( 0x4u ); }
	std::int32_t interpolation_info::dst_tick( ) const { return this->m_dst_tick; }
	void interpolation_info::set_dst_tick( std::int32_t v ) { this->m_has_bits.set( 0x4u ); this->m_dst_tick = v; }

	// interpolation_info_cl

	bool interpolation_info_cl::has_frac( ) const { return this->m_has_bits.test( 0x1u ); }
	float interpolation_info_cl::frac( ) const { return this->m_frac; }
	void interpolation_info_cl::set_frac( float v ) { this->m_has_bits.set( 0x1u ); this->m_frac = v; }

	// in_button_state_pb

	std::uint64_t in_button_state_pb::buttonstate1( ) const { return this->m_buttonstate1; }
	std::uint64_t in_button_state_pb::buttonstate2( ) const { return this->m_buttonstate2; }
	std::uint64_t in_button_state_pb::buttonstate3( ) const { return this->m_buttonstate3; }
	void in_button_state_pb::set_buttonstate1( std::uint64_t v ) { this->m_has_bits.set( 0x1u ); this->m_buttonstate1 = v; }
	void in_button_state_pb::set_buttonstate2( std::uint64_t v ) { this->m_has_bits.set( 0x2u ); this->m_buttonstate2 = v; }
	void in_button_state_pb::set_buttonstate3( std::uint64_t v ) { this->m_has_bits.set( 0x4u ); this->m_buttonstate3 = v; }

	// subtick_move_step

	std::uint64_t subtick_move_step::button( ) const { return this->m_button; }
	void subtick_move_step::set_button( std::uint64_t v ) { this->m_has_bits.set( 0x1u ); this->m_button = v; }
	bool subtick_move_step::pressed( ) const { return this->m_pressed; }
	void subtick_move_step::set_pressed( bool v ) { this->m_has_bits.set( 0x2u ); this->m_pressed = v; }
	float subtick_move_step::when( ) const { return this->m_when; }
	void subtick_move_step::set_when( float v ) { this->m_has_bits.set( 0x4u ); this->m_when = v; }
	float subtick_move_step::analog_forward_delta( ) const { return this->m_analog_forward_delta; }
	void subtick_move_step::set_analog_forward_delta( float v ) { this->m_has_bits.set( 0x8u ); this->m_analog_forward_delta = v; }
	float subtick_move_step::analog_left_delta( ) const { return this->m_analog_left_delta; }
	void subtick_move_step::set_analog_left_delta( float v ) { this->m_has_bits.set( 0x10u ); this->m_analog_left_delta = v; }
	float subtick_move_step::pitch_delta( ) const { return this->m_pitch_delta; }
	void subtick_move_step::set_pitch_delta( float v ) { this->m_has_bits.set( 0x20u ); this->m_pitch_delta = v; }
	float subtick_move_step::yaw_delta( ) const { return this->m_yaw_delta; }
	void subtick_move_step::set_yaw_delta( float v ) { this->m_has_bits.set( 0x40u ); this->m_yaw_delta = v; }

	// base_usercmd_pb

	bool base_usercmd_pb::has_buttons_pb( ) const { return this->m_has_bits.test( 0x2u ); }
	const in_button_state_pb* base_usercmd_pb::buttons_pb( ) const { return impl_ptr<const in_button_state_pb>( this->m_buttons_pb ); }
	in_button_state_pb* base_usercmd_pb::mutable_buttons_pb( ) { this->m_has_bits.set( 0x2u ); return impl_ptr<in_button_state_pb>( this->m_buttons_pb ); }
	bool base_usercmd_pb::has_viewangles( ) const { return this->m_has_bits.test( 0x4u ); }
	const msg_qangle* base_usercmd_pb::viewangles( ) const { return impl_ptr<const msg_qangle>( this->m_viewangles ); }
	msg_qangle* base_usercmd_pb::mutable_viewangles( ) { this->m_has_bits.set( 0x4u ); return impl_ptr<msg_qangle>( this->m_viewangles ); }
	const repeated_ptr_field<subtick_move_step>& base_usercmd_pb::subtick_moves( ) const { return this->m_subtick_moves; }
	repeated_ptr_field<subtick_move_step>* base_usercmd_pb::mutable_subtick_moves( ) { return &this->m_subtick_moves; }
	int base_usercmd_pb::subtick_moves_size( ) const { return this->m_subtick_moves.size( ); }
	subtick_move_step* base_usercmd_pb::mutable_subtick_moves( int i ) { return this->m_subtick_moves.mutable_at( i ); }

	void base_usercmd_pb::set_legacy_command_number( std::int32_t v ) { this->m_has_bits.set( 0x10u ); this->m_legacy_command_number = v; }
	void base_usercmd_pb::set_client_tick( std::int32_t v ) { this->m_has_bits.set( 0x20u ); this->m_client_tick = v; }
	void base_usercmd_pb::set_forwardmove( float v ) { this->m_has_bits.set( 0x40u ); this->m_forwardmove = v; }
	void base_usercmd_pb::set_leftmove( float v ) { this->m_has_bits.set( 0x80u ); this->m_leftmove = v; }
	void base_usercmd_pb::set_upmove( float v ) { this->m_has_bits.set( 0x100u ); this->m_upmove = v; }
	void base_usercmd_pb::set_impulse( std::int32_t v ) { this->m_has_bits.set( 0x200u ); this->m_impulse = v; }
	void base_usercmd_pb::set_weaponselect( std::int32_t v ) { this->m_has_bits.set( 0x400u ); this->m_weaponselect = v; }
	void base_usercmd_pb::set_random_seed( std::int32_t v ) { this->m_has_bits.set( 0x800u ); this->m_random_seed = v; }
	void base_usercmd_pb::set_mousedx( std::int32_t v ) { this->m_has_bits.set( 0x1000u ); this->m_mousedx = v; }
	void base_usercmd_pb::set_mousedy( std::int32_t v ) { this->m_has_bits.set( 0x2000u ); this->m_mousedy = v; }
	void base_usercmd_pb::set_prediction_offset_ticks_x256( std::uint32_t v ) { this->m_has_bits.set( 0x4000u ); this->m_prediction_offset_ticks_x256 = v; }
	void base_usercmd_pb::set_consumed_server_angle_changes( std::uint32_t v ) { this->m_has_bits.set( 0x8000u ); this->m_consumed_server_angle_changes = v; }
	void base_usercmd_pb::set_cmd_flags( std::int32_t v ) { this->m_has_bits.set( 0x10000u ); this->m_cmd_flags = v; }
	void base_usercmd_pb::set_pawn_entity_handle( std::uint32_t v ) { this->m_has_bits.set( 0x20000u ); this->m_pawn_entity_handle = v; }

	std::int32_t base_usercmd_pb::legacy_command_number( ) const { return this->m_legacy_command_number; }
	std::int32_t base_usercmd_pb::client_tick( ) const { return this->m_client_tick; }
	float base_usercmd_pb::forwardmove( ) const { return this->m_forwardmove; }
	float base_usercmd_pb::leftmove( ) const { return this->m_leftmove; }
	float base_usercmd_pb::upmove( ) const { return this->m_upmove; }
	std::int32_t base_usercmd_pb::impulse( ) const { return this->m_impulse; }
	std::int32_t base_usercmd_pb::weaponselect( ) const { return this->m_weaponselect; }
	std::int32_t base_usercmd_pb::random_seed( ) const { return this->m_random_seed; }
	std::int32_t base_usercmd_pb::mousedx( ) const { return this->m_mousedx; }
	std::int32_t base_usercmd_pb::mousedy( ) const { return this->m_mousedy; }
	std::uint32_t base_usercmd_pb::prediction_offset_ticks_x256( ) const { return this->m_prediction_offset_ticks_x256; }
	std::uint32_t base_usercmd_pb::consumed_server_angle_changes( ) const { return this->m_consumed_server_angle_changes; }
	std::int32_t base_usercmd_pb::cmd_flags( ) const { return this->m_cmd_flags; }
	std::uint32_t base_usercmd_pb::pawn_entity_handle( ) const { return this->m_pawn_entity_handle; }

	bool base_usercmd_pb::has_forwardmove( ) const { return this->m_has_bits.test( 0x40u ); }
	bool base_usercmd_pb::has_leftmove( ) const { return this->m_has_bits.test( 0x80u ); }
	bool base_usercmd_pb::has_upmove( ) const { return this->m_has_bits.test( 0x100u ); }
	bool base_usercmd_pb::has_pawn_entity_handle( ) const { return this->m_has_bits.test( 0x20000u ); }

	// input_history_entry

	bool input_history_entry::has_view_angles( ) const { return this->m_has_bits.test( 0x1u ); }
	const msg_qangle* input_history_entry::view_angles( ) const { return impl_ptr<const msg_qangle>( this->m_view_angles ); }
	msg_qangle* input_history_entry::mutable_view_angles( ) { this->m_has_bits.set( 0x1u ); return impl_ptr<msg_qangle>( this->m_view_angles ); }
	bool input_history_entry::has_cl_interp( ) const { return this->m_has_bits.test( 0x2u ); }
	const interpolation_info_cl* input_history_entry::cl_interp( ) const { return impl_ptr<const interpolation_info_cl>( this->m_cl_interp ); }
	interpolation_info_cl* input_history_entry::mutable_cl_interp( ) { this->m_has_bits.set( 0x2u ); return impl_ptr<interpolation_info_cl>( this->m_cl_interp ); }
	bool input_history_entry::has_sv_interp0( ) const { return this->m_has_bits.test( 0x4u ); }
	const interpolation_info* input_history_entry::sv_interp0( ) const { return impl_ptr<const interpolation_info>( this->m_sv_interp0 ); }
	interpolation_info* input_history_entry::mutable_sv_interp0( ) { this->m_has_bits.set( 0x4u ); return impl_ptr<interpolation_info>( this->m_sv_interp0 ); }
	bool input_history_entry::has_sv_interp1( ) const { return this->m_has_bits.test( 0x8u ); }
	const interpolation_info* input_history_entry::sv_interp1( ) const { return impl_ptr<const interpolation_info>( this->m_sv_interp1 ); }
	interpolation_info* input_history_entry::mutable_sv_interp1( ) { this->m_has_bits.set( 0x8u ); return impl_ptr<interpolation_info>( this->m_sv_interp1 ); }
	bool input_history_entry::has_player_interp( ) const { return this->m_has_bits.test( 0x10u ); }
	const interpolation_info* input_history_entry::player_interp( ) const { return impl_ptr<const interpolation_info>( this->m_player_interp ); }
	interpolation_info* input_history_entry::mutable_player_interp( ) { this->m_has_bits.set( 0x10u ); return impl_ptr<interpolation_info>( this->m_player_interp ); }
	bool input_history_entry::has_shoot_position( ) const { return this->m_has_bits.test( 0x20u ); }
	const msg_vector* input_history_entry::shoot_position( ) const { return impl_ptr<const msg_vector>( this->m_shoot_position ); }
	msg_vector* input_history_entry::mutable_shoot_position( ) { this->m_has_bits.set( 0x20u ); return impl_ptr<msg_vector>( this->m_shoot_position ); }
	bool input_history_entry::has_target_head_pos_check( ) const { return this->m_has_bits.test( 0x40u ); }
	const msg_vector* input_history_entry::target_head_pos_check( ) const { return impl_ptr<const msg_vector>( this->m_target_head_pos_check ); }
	msg_vector* input_history_entry::mutable_target_head_pos_check( ) { this->m_has_bits.set( 0x40u ); return impl_ptr<msg_vector>( this->m_target_head_pos_check ); }
	bool input_history_entry::has_target_abs_pos_check( ) const { return this->m_has_bits.test( 0x80u ); }
	const msg_vector* input_history_entry::target_abs_pos_check( ) const { return impl_ptr<const msg_vector>( this->m_target_abs_pos_check ); }
	msg_vector* input_history_entry::mutable_target_abs_pos_check( ) { this->m_has_bits.set( 0x80u ); return impl_ptr<msg_vector>( this->m_target_abs_pos_check ); }
	bool input_history_entry::has_target_abs_ang_check( ) const { return this->m_has_bits.test( 0x100u ); }
	const msg_qangle* input_history_entry::target_abs_ang_check( ) const { return impl_ptr<const msg_qangle>( this->m_target_abs_ang_check ); }
	msg_qangle* input_history_entry::mutable_target_abs_ang_check( ) { this->m_has_bits.set( 0x100u ); return impl_ptr<msg_qangle>( this->m_target_abs_ang_check ); }

	bool input_history_entry::has_render_tick_count( ) const { return this->m_has_bits.test( 0x200u ); }
	std::int32_t input_history_entry::render_tick_count( ) const { return this->m_render_tick_count; }
	void input_history_entry::set_render_tick_count( std::int32_t v ) { this->m_has_bits.set( 0x200u ); this->m_render_tick_count = v; }

	bool input_history_entry::has_render_tick_fraction( ) const { return this->m_has_bits.test( 0x400u ); }
	float input_history_entry::render_tick_fraction( ) const { return this->m_render_tick_fraction; }
	void input_history_entry::set_render_tick_fraction( float v ) { this->m_has_bits.set( 0x400u ); this->m_render_tick_fraction = v; }

	bool input_history_entry::has_player_tick_count( ) const { return this->m_has_bits.test( 0x800u ); }
	std::int32_t input_history_entry::player_tick_count( ) const { return this->m_player_tick_count; }
	void input_history_entry::set_player_tick_count( std::int32_t v ) { this->m_has_bits.set( 0x800u ); this->m_player_tick_count = v; }

	bool input_history_entry::has_player_tick_fraction( ) const { return this->m_has_bits.test( 0x1000u ); }
	float input_history_entry::player_tick_fraction( ) const { return this->m_player_tick_fraction; }
	void input_history_entry::set_player_tick_fraction( float v ) { this->m_has_bits.set( 0x1000u ); this->m_player_tick_fraction = v; }

	bool input_history_entry::has_frame_number( ) const { return this->m_has_bits.test( 0x2000u ); }
	std::int32_t input_history_entry::frame_number( ) const { return this->m_frame_number; }
	void input_history_entry::set_frame_number( std::int32_t v ) { this->m_has_bits.set( 0x2000u ); this->m_frame_number = v; }

	bool input_history_entry::has_target_ent_index( ) const { return this->m_has_bits.test( 0x4000u ); }
	std::int32_t input_history_entry::target_ent_index( ) const { return this->m_target_ent_index; }
	void input_history_entry::set_target_ent_index( std::int32_t v ) { this->m_has_bits.set( 0x4000u ); this->m_target_ent_index = v; }

	// csgo_usercmd_pb

	bool csgo_usercmd_pb::has_base( ) const { return this->m_has_bits.test( 0x1u ); }
	base_usercmd_pb* csgo_usercmd_pb::mutable_base( ) { this->m_has_bits.set( 0x1u ); return impl_ptr<base_usercmd_pb>( this->m_base ); }
	const base_usercmd_pb* csgo_usercmd_pb::base( ) const { return impl_ptr<const base_usercmd_pb>( this->m_base ); }

	int csgo_usercmd_pb::input_history_size( ) const { return this->m_input_history.size( ); }
	input_history_entry* csgo_usercmd_pb::mutable_input_history( int i ) { return this->m_input_history.mutable_at( i ); }
	repeated_ptr_field<input_history_entry>* csgo_usercmd_pb::mutable_input_history( ) { return &this->m_input_history; }

	void csgo_usercmd_pb::set_left_hand_desired( bool v ) { this->m_has_bits.set( 0x2u ); this->m_left_hand_desired = v; }
	void csgo_usercmd_pb::set_is_predicting_body_shot_fx( bool v ) { this->m_has_bits.set( 0x4u ); this->m_is_predicting_body_shot_fx = v; }
	void csgo_usercmd_pb::set_is_predicting_head_shot_fx( bool v ) { this->m_has_bits.set( 0x8u ); this->m_is_predicting_head_shot_fx = v; }
	void csgo_usercmd_pb::set_is_predicting_kill_ragdolls( bool v ) { this->m_has_bits.set( 0x10u ); this->m_is_predicting_kill_ragdolls = v; }
	void csgo_usercmd_pb::set_attack1_start_history_index( std::int32_t v ) { this->m_has_bits.set( 0x20u ); this->m_attack1_start_history_index = v; }
	void csgo_usercmd_pb::set_attack2_start_history_index( std::int32_t v ) { this->m_has_bits.set( 0x40u ); this->m_attack2_start_history_index = v; }

	bool csgo_usercmd_pb::left_hand_desired( ) const { return this->m_left_hand_desired; }
	bool csgo_usercmd_pb::is_predicting_body_shot_fx( ) const { return this->m_is_predicting_body_shot_fx; }
	bool csgo_usercmd_pb::is_predicting_head_shot_fx( ) const { return this->m_is_predicting_head_shot_fx; }
	bool csgo_usercmd_pb::is_predicting_kill_ragdolls( ) const { return this->m_is_predicting_kill_ragdolls; }
	std::int32_t csgo_usercmd_pb::attack1_start_history_index( ) const { return this->m_attack1_start_history_index; }
	std::int32_t csgo_usercmd_pb::attack2_start_history_index( ) const { return this->m_attack2_start_history_index; }

} // namespace proto