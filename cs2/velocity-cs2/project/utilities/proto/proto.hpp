#pragma once

#include <cstdint>
#include <cstddef>

namespace proto {

	constexpr std::uintptr_t message_impl_offset{ 0x10 };

	template <typename T>
	[[nodiscard]] inline T* impl_ptr( void* msg )
	{
		if ( !msg )
		{
			return nullptr;
		}

		return reinterpret_cast< T* >( reinterpret_cast< std::uintptr_t >( msg ) + message_impl_offset );
	}

	template <typename T>
	[[nodiscard]] inline const T* impl_ptr( const void* msg )
	{
		if ( !msg )
		{
			return nullptr;
		}

		return reinterpret_cast< const T* >( reinterpret_cast< std::uintptr_t >( msg ) + message_impl_offset );
	}

	template <typename T>
	struct repeated_ptr_field
	{
		void* m_arena;
		int m_current_size;
		int m_total_size;

		struct rep_t
		{
			int allocated_size;
			void* elements[ 1 ];
		};

		rep_t* m_rep;

		[[nodiscard]] bool empty( ) const { return this->m_current_size == 0; }
		[[nodiscard]] int size( ) const { return this->m_current_size; }

		[[nodiscard]] T* mutable_at( int index )
		{
			return impl_ptr<T>( this->m_rep->elements[ index ] );
		}

		[[nodiscard]] T* add( )
		{
			if ( this->m_rep != nullptr && this->m_current_size < this->m_rep->allocated_size )
			{
				return impl_ptr<T>( this->m_rep->elements[ this->m_current_size++ ] );
			}

			return nullptr;
		}

		void clear( ) { this->m_current_size = 0; }
	};

	struct has_bits
	{
		std::uint32_t bits[ 1 ]{};

		[[nodiscard]] bool test( std::uint32_t mask ) const;
		void set( std::uint32_t mask );
		void clear( std::uint32_t mask );
	};

	struct msg_qangle
	{
		has_bits m_has_bits;
		std::uint32_t m_cached_size;
		float m_x;
		float m_y;
		float m_z;

		[[nodiscard]] float x( ) const;
		[[nodiscard]] float y( ) const;
		[[nodiscard]] float z( ) const;
		void set_x( float v );
		void set_y( float v );
		void set_z( float v );
	};

	struct msg_vector
	{
		has_bits m_has_bits;
		std::uint32_t m_cached_size;
		float m_x;
		float m_y;
		float m_z;
		float m_w;

		[[nodiscard]] float x( ) const;
		[[nodiscard]] float y( ) const;
		[[nodiscard]] float z( ) const;
		void set_x( float v );
		void set_y( float v );
		void set_z( float v );
	};

	struct interpolation_info
	{
		has_bits m_has_bits;
		std::uint32_t m_cached_size;
		float m_frac;
		std::int32_t m_src_tick;
		std::int32_t m_dst_tick;

		[[nodiscard]] bool has_frac( ) const;
		[[nodiscard]] float frac( ) const;
		void set_frac( float v );
		[[nodiscard]] bool has_src_tick( ) const;
		[[nodiscard]] std::int32_t src_tick( ) const;
		void set_src_tick( std::int32_t v );
		[[nodiscard]] bool has_dst_tick( ) const;
		[[nodiscard]] std::int32_t dst_tick( ) const;
		void set_dst_tick( std::int32_t v );
	};

	struct interpolation_info_cl
	{
		has_bits m_has_bits;
		std::uint32_t m_cached_size;
		float m_frac;

		[[nodiscard]] bool has_frac( ) const;
		[[nodiscard]] float frac( ) const;
		void set_frac( float v );
	};

	struct in_button_state_pb
	{
		has_bits m_has_bits;
		std::uint32_t m_cached_size;
		std::uint64_t m_buttonstate1;
		std::uint64_t m_buttonstate2;
		std::uint64_t m_buttonstate3;

		[[nodiscard]] std::uint64_t buttonstate1( ) const;
		[[nodiscard]] std::uint64_t buttonstate2( ) const;
		[[nodiscard]] std::uint64_t buttonstate3( ) const;
		void set_buttonstate1( std::uint64_t v );
		void set_buttonstate2( std::uint64_t v );
		void set_buttonstate3( std::uint64_t v );
	};

	struct subtick_move_step
	{
		has_bits m_has_bits;
		std::uint32_t m_cached_size;
		std::uint64_t m_button;
		bool m_pressed;
		float m_when;
		float m_analog_forward_delta;
		float m_analog_left_delta;
		float m_pitch_delta;
		float m_yaw_delta;

		[[nodiscard]] std::uint64_t button( ) const;
		void set_button( std::uint64_t v );
		[[nodiscard]] bool pressed( ) const;
		void set_pressed( bool v );
		[[nodiscard]] float when( ) const;
		void set_when( float v );
		[[nodiscard]] float analog_forward_delta( ) const;
		void set_analog_forward_delta( float v );
		[[nodiscard]] float analog_left_delta( ) const;
		void set_analog_left_delta( float v );
		[[nodiscard]] float pitch_delta( ) const;
		void set_pitch_delta( float v );
		[[nodiscard]] float yaw_delta( ) const;
		void set_yaw_delta( float v );
	};

	struct base_usercmd_pb
	{
		has_bits m_has_bits;
		std::uint32_t m_cached_size;
		repeated_ptr_field<subtick_move_step> m_subtick_moves;
		void* m_move_crc;
		in_button_state_pb* m_buttons_pb;
		msg_qangle* m_viewangles;
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

		[[nodiscard]] bool has_buttons_pb( ) const;
		[[nodiscard]] const in_button_state_pb* buttons_pb( ) const;
		[[nodiscard]] in_button_state_pb* mutable_buttons_pb( );
		[[nodiscard]] bool has_viewangles( ) const;
		[[nodiscard]] const msg_qangle* viewangles( ) const;
		[[nodiscard]] msg_qangle* mutable_viewangles( );
		[[nodiscard]] const repeated_ptr_field<subtick_move_step>& subtick_moves( ) const;
		[[nodiscard]] repeated_ptr_field<subtick_move_step>* mutable_subtick_moves( );
		[[nodiscard]] int subtick_moves_size( ) const;
		[[nodiscard]] subtick_move_step* mutable_subtick_moves( int i );

		void set_legacy_command_number( std::int32_t v );
		void set_client_tick( std::int32_t v );
		void set_forwardmove( float v );
		void set_leftmove( float v );
		void set_upmove( float v );
		void set_impulse( std::int32_t v );
		void set_weaponselect( std::int32_t v );
		void set_random_seed( std::int32_t v );
		void set_mousedx( std::int32_t v );
		void set_mousedy( std::int32_t v );
		void set_prediction_offset_ticks_x256( std::uint32_t v );
		void set_consumed_server_angle_changes( std::uint32_t v );
		void set_cmd_flags( std::int32_t v );
		void set_pawn_entity_handle( std::uint32_t v );

		[[nodiscard]] std::int32_t legacy_command_number( ) const;
		[[nodiscard]] std::int32_t client_tick( ) const;
		[[nodiscard]] float forwardmove( ) const;
		[[nodiscard]] float leftmove( ) const;
		[[nodiscard]] float upmove( ) const;
		[[nodiscard]] std::int32_t impulse( ) const;
		[[nodiscard]] std::int32_t weaponselect( ) const;
		[[nodiscard]] std::int32_t random_seed( ) const;
		[[nodiscard]] std::int32_t mousedx( ) const;
		[[nodiscard]] std::int32_t mousedy( ) const;
		[[nodiscard]] std::uint32_t prediction_offset_ticks_x256( ) const;
		[[nodiscard]] std::uint32_t consumed_server_angle_changes( ) const;
		[[nodiscard]] std::int32_t cmd_flags( ) const;
		[[nodiscard]] std::uint32_t pawn_entity_handle( ) const;

		[[nodiscard]] bool has_forwardmove( ) const;
		[[nodiscard]] bool has_leftmove( ) const;
		[[nodiscard]] bool has_upmove( ) const;
		[[nodiscard]] bool has_pawn_entity_handle( ) const;
	};

	struct input_history_entry
	{
		has_bits m_has_bits;
		std::uint32_t m_cached_size;
		msg_qangle* m_view_angles;
		interpolation_info_cl* m_cl_interp;
		interpolation_info* m_sv_interp0;
		interpolation_info* m_sv_interp1;
		interpolation_info* m_player_interp;
		msg_vector* m_shoot_position;
		msg_vector* m_target_head_pos_check;
		msg_vector* m_target_abs_pos_check;
		msg_qangle* m_target_abs_ang_check;
		std::int32_t m_render_tick_count;
		float m_render_tick_fraction;
		std::int32_t m_player_tick_count;
		float m_player_tick_fraction;
		std::int32_t m_frame_number;
		std::int32_t m_target_ent_index;

		[[nodiscard]] bool has_view_angles( ) const;
		[[nodiscard]] const msg_qangle* view_angles( ) const;
		[[nodiscard]] msg_qangle* mutable_view_angles( );
		[[nodiscard]] bool has_cl_interp( ) const;
		[[nodiscard]] const interpolation_info_cl* cl_interp( ) const;
		[[nodiscard]] interpolation_info_cl* mutable_cl_interp( );
		[[nodiscard]] bool has_sv_interp0( ) const;
		[[nodiscard]] const interpolation_info* sv_interp0( ) const;
		[[nodiscard]] interpolation_info* mutable_sv_interp0( );
		[[nodiscard]] bool has_sv_interp1( ) const;
		[[nodiscard]] const interpolation_info* sv_interp1( ) const;
		[[nodiscard]] interpolation_info* mutable_sv_interp1( );
		[[nodiscard]] bool has_player_interp( ) const;
		[[nodiscard]] const interpolation_info* player_interp( ) const;
		[[nodiscard]] interpolation_info* mutable_player_interp( );
		[[nodiscard]] bool has_shoot_position( ) const;
		[[nodiscard]] const msg_vector* shoot_position( ) const;
		[[nodiscard]] msg_vector* mutable_shoot_position( );
		[[nodiscard]] bool has_target_head_pos_check( ) const;
		[[nodiscard]] const msg_vector* target_head_pos_check( ) const;
		[[nodiscard]] msg_vector* mutable_target_head_pos_check( );
		[[nodiscard]] bool has_target_abs_pos_check( ) const;
		[[nodiscard]] const msg_vector* target_abs_pos_check( ) const;
		[[nodiscard]] msg_vector* mutable_target_abs_pos_check( );
		[[nodiscard]] bool has_target_abs_ang_check( ) const;
		[[nodiscard]] const msg_qangle* target_abs_ang_check( ) const;
		[[nodiscard]] msg_qangle* mutable_target_abs_ang_check( );

		[[nodiscard]] bool has_render_tick_count( ) const;
		[[nodiscard]] std::int32_t render_tick_count( ) const;
		void set_render_tick_count( std::int32_t v );
		[[nodiscard]] bool has_render_tick_fraction( ) const;
		[[nodiscard]] float render_tick_fraction( ) const;
		void set_render_tick_fraction( float v );
		[[nodiscard]] bool has_player_tick_count( ) const;
		[[nodiscard]] std::int32_t player_tick_count( ) const;
		void set_player_tick_count( std::int32_t v );
		[[nodiscard]] bool has_player_tick_fraction( ) const;
		[[nodiscard]] float player_tick_fraction( ) const;
		void set_player_tick_fraction( float v );
		[[nodiscard]] bool has_frame_number( ) const;
		[[nodiscard]] std::int32_t frame_number( ) const;
		void set_frame_number( std::int32_t v );
		[[nodiscard]] bool has_target_ent_index( ) const;
		[[nodiscard]] std::int32_t target_ent_index( ) const;
		void set_target_ent_index( std::int32_t v );
	};

	struct csgo_usercmd_pb
	{
		has_bits m_has_bits;
		std::uint32_t m_cached_size;
		repeated_ptr_field<input_history_entry> m_input_history;
		base_usercmd_pb* m_base;
		bool m_left_hand_desired;
		bool m_is_predicting_body_shot_fx;
		bool m_is_predicting_head_shot_fx;
		bool m_is_predicting_kill_ragdolls;
		std::int32_t m_attack1_start_history_index;
		std::int32_t m_attack2_start_history_index;

		[[nodiscard]] bool has_base( ) const;
		[[nodiscard]] base_usercmd_pb* mutable_base( );
		[[nodiscard]] const base_usercmd_pb* base( ) const;

		[[nodiscard]] int input_history_size( ) const;
		[[nodiscard]] input_history_entry* mutable_input_history( int i );
		[[nodiscard]] repeated_ptr_field<input_history_entry>* mutable_input_history( );

		void set_left_hand_desired( bool v );
		void set_is_predicting_body_shot_fx( bool v );
		void set_is_predicting_head_shot_fx( bool v );
		void set_is_predicting_kill_ragdolls( bool v );
		void set_attack1_start_history_index( std::int32_t v );
		void set_attack2_start_history_index( std::int32_t v );

		[[nodiscard]] bool left_hand_desired( ) const;
		[[nodiscard]] bool is_predicting_body_shot_fx( ) const;
		[[nodiscard]] bool is_predicting_head_shot_fx( ) const;
		[[nodiscard]] bool is_predicting_kill_ragdolls( ) const;
		[[nodiscard]] std::int32_t attack1_start_history_index( ) const;
		[[nodiscard]] std::int32_t attack2_start_history_index( ) const;
	};

} // namespace proto