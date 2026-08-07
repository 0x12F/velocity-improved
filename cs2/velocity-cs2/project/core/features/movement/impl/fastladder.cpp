#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>

#include "../movement.hpp"

namespace features::movement {

	void fastladder::on_create_move( systems::input::usercmd* cmd ) const
	{
		if ( !settings::g_movement.fastladder.value )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.pawn )
		{
			return;
		}

		const auto move_type = memory::read<std::uint8_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_nActualMoveType"_hash ) );
		if ( move_type != cstypes::move_type::ladder )
		{
			return;
		}

		const auto base = cmd->csgo_user_cmd.mutable_base( );
		if ( !base )
		{
			return;
		}

		const auto forward_move = base->forwardmove( );
		const auto side_move = base->leftmove( );

		if ( forward_move == 0.0f && side_move == 0.0f )
		{
			return;
		}

		bool going_up{};

		if ( std::fabsf( forward_move ) > 0.01f )
		{
			going_up = forward_move > 0.0f;
		}
		else
		{
			const auto view_angles = systems::g_input.get_view_angles( );
			going_up = view_angles.x < 0.0f;
		}

		const auto view_angles = systems::g_input.get_view_angles( );
		auto modified = view_angles;
		modified.x = 89.0f;
		modified.y += going_up ? -90.0f : 90.0f;
		math::helpers::normalize_angles( modified );

		base->set_forwardmove( -1.0f );
		base->set_leftmove( going_up ? 1.0f : -1.0f );

		auto buttons = cmd->buttons.value;
		buttons &= ~( cstypes::command_buttons::in_forward | cstypes::command_buttons::in_back | cstypes::command_buttons::in_moveleft | cstypes::command_buttons::in_moveright );
		buttons |= cstypes::command_buttons::in_back;
		buttons |= going_up ? cstypes::command_buttons::in_moveleft : cstypes::command_buttons::in_moveright;
		cmd->buttons.value = buttons;

		base->mutable_viewangles( )->set_x( modified.x );
		base->mutable_viewangles( )->set_y( modified.y );
		base->mutable_viewangles( )->set_z( 0.0f );
	}

} // namespace features::movement