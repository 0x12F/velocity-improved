#pragma once

namespace features::movement {

	class bhop
	{
	public:
		void on_create_move( systems::input::usercmd* cmd );
		[[nodiscard]] bool landing_jump_this_tick( ) const { return this->m_landing_jump_this_tick; }
		[[nodiscard]] float landing_fraction( ) const { return this->m_landing_fraction; }

	private:
		bool m_landing_jump_this_tick{ false };
		float m_landing_fraction{ 1.0f };
	};

	class airstrafe
	{
	public:
		void on_create_move( systems::input::usercmd* cmd );
		void store_angles( );

	private:
		void check_button( std::uintptr_t current_buttons, std::uintptr_t button );
		void rotate_movement( proto::base_usercmd_pb* base, float target_yaw, float view_yaw ) const;
		void rotate_to_stop( proto::base_usercmd_pb* base, const math::vector3& velocity ) const;

		std::uintptr_t m_last_buttons{};
		std::uintptr_t m_last_pressed{};
		bool m_side_switch{};
		math::vector3 m_angles{};
	};

	class jumpbug
	{
	public:
		void on_create_move( systems::input::usercmd* cmd );
		[[nodiscard]] bool active_this_tick( ) const { return this->m_active_this_tick; }
		[[nodiscard]] float landing_fraction( ) const { return this->m_landing_fraction; }

	private:
		[[nodiscard]] float get_impulse_mul( std::uintptr_t local_pawn ) const;

		float m_landing_fraction{ 1.0f };
		bool m_active_this_tick{ false };
	};

	class fastladder
	{
	public:
		void on_create_move( systems::input::usercmd* cmd ) const;
	};

	class edgejump
	{
	public:
		void on_create_move( systems::input::usercmd* cmd ) const;
	};

	class edgestop
	{
	public:
		void on_create_move( systems::input::usercmd* cmd ) const;
	};

	class edgebug
	{
	public:
		void on_create_move( systems::input::usercmd* cmd );
		void on_render( xdraw::draw_list& draw_list );

		[[nodiscard]] bool active_this_tick( ) const { return this->m_active_this_tick; }

	private:
		bool m_active_this_tick{ false };
	};

	class slowwalk
	{
	public:
		void on_create_move( systems::input::usercmd* cmd ) const;
	};

	class test_strafer
	{
	public:
		void on_create_move( systems::input::usercmd* cmd );
		[[nodiscard]] bool is_active( ) const;
		[[nodiscard]] bool handled_this_tick( ) const { return this->m_handled_this_tick; }

	private:
		void quantized_path( systems::input::usercmd* cmd );
		[[nodiscard]] bool apply_yaw_subtick( proto::base_usercmd_pb* base, float when, float yaw_delta ) const;
		void check_button( std::uintptr_t current_buttons, std::uintptr_t button );
		[[nodiscard]] static math::vector2 movement_from_buttons( std::uintptr_t pressed );

		std::uintptr_t m_last_buttons{};
		std::uintptr_t m_last_pressed{};
		int m_substep_counter{};
		bool m_handled_this_tick{};
	};

} // namespace features::movement
