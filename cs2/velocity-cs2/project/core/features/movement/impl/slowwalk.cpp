#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>

namespace features::movement {

	void slowwalk::on_create_move( systems::input::usercmd* cmd ) const
	{
		if ( !settings::g_movement.slowwalk.value )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.pawn )
		{
			return;
		}

		const auto& prestate = systems::g_prediction.pre( );
		if ( !( prestate.flags & cstypes::entity_flags::on_ground ) )
		{
			return;
		}

		const auto base = cmd->csgo_user_cmd.mutable_base( );
		const auto forward_move = base->forwardmove( );
		const auto side_move = base->leftmove( );

		if ( forward_move == 0.0f && side_move == 0.0f )
		{
			return;
		}

		const auto target_ratio = settings::g_movement.slowwalk_speed / 100.0f;
		const auto move_length = std::sqrtf( forward_move * forward_move + side_move * side_move );

		if ( move_length < 0.001f )
		{
			return;
		}

		const auto scaled = std::fminf( move_length, target_ratio );

		base->set_forwardmove( ( forward_move / move_length ) * scaled );
		base->set_leftmove( ( side_move / move_length ) * scaled );
	}

} // namespace features::movement