#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>

#include "../movement.hpp"
#include <protection/game_addresses.hpp>

namespace features::movement {

	void jumpbug::on_create_move( systems::input::usercmd* cmd )
	{
		this->m_active_this_tick = false;

		if ( !settings::g_movement.jumpbug.value )
		{
			return;
		}

		if ( features::movement::g_edgebug.active_this_tick( ) )
		{
			return;
		}

		auto& buttons = cmd->buttons;
		if ( !( buttons.value & cstypes::command_buttons::in_jump ) )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.pawn )
		{
			return;
		}

		const auto move_type = memory::read<std::uint8_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_nActualMoveType"_hash ) );
		if ( move_type == cstypes::move_type::ladder || move_type == cstypes::move_type::noclip )
		{
			return;
		}

		const auto& prestate = systems::g_prediction.pre( );
		if ( prestate.flags & cstypes::entity_flags::on_ground )
		{
			return;
		}

		if ( prestate.networked_velocity.z > 0.0f )
		{
			return;
		}

		const auto movement_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );
		if ( !movement_services )
		{
			return;
		}

		const auto duck_amount = memory::read<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_flDuckAmount"_hash ) );
		const auto duck_speed = memory::read<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_flDuckSpeed"_hash ) );
		const auto holding_duck = ( buttons.value & cstypes::command_buttons::in_duck ) != 0;
		const auto mins = memory::read<math::vector3>( local.pawn + SCHEMA( "C_BaseModelEntity", "m_Collision"_hash ) + SCHEMA( "CCollisionProperty", "m_vecMins"_hash ) );
		auto maxs = memory::read<math::vector3>( local.pawn + SCHEMA( "C_BaseModelEntity", "m_Collision"_hash ) + SCHEMA( "CCollisionProperty", "m_vecMaxs"_hash ) );

		auto trace_origin = prestate.networked_origin;
		{
			if ( holding_duck && duck_amount > 0.0f )
			{
				const auto standing_height{ 72.0f };
				const auto duck_hull_diff = standing_height - maxs.z;
				trace_origin.z -= duck_hull_diff * 0.5f;
				maxs.z = standing_height;
			}
		}

		auto trace_mask{ 0ull };
		{
			const auto pawn_ptr = memory::read<std::uintptr_t>( movement_services + 56 );
			trace_mask = memory::read<std::uintptr_t>( pawn_ptr + 0xd48 );

			if ( !pawn_ptr || ( memory::read<std::uint32_t>( pawn_ptr + 0x3f8 ) & 0x10 ) )
			{
				trace_mask |= 0x20;
			}
		}

		const auto filter = systems::g_tracing.make_player_movement_filter( local.pawn, trace_mask, 11 );
		const auto sv_gravity = CONVAR ("sv_gravity")->get<float>( );
		const auto sv_standable_normal = CONVAR ("sv_standable_normal")->get<float>( );
		const auto gravity_scale = memory::read<float>( local.pawn + SCHEMA( "C_BaseEntity", "m_flGravityScale"_hash ) );

		auto velocity = prestate.networked_velocity;
		velocity.z -= ( gravity_scale * sv_gravity * cstypes::tick_interval ) * 0.5f;

		math::vector3 trace_start = trace_origin;
		math::vector3 trace_end{};

		trace_end.x = trace_origin.x + velocity.x * cstypes::tick_interval;
		trace_end.y = trace_origin.y + velocity.y * cstypes::tick_interval;
		trace_end.z = trace_origin.z + velocity.z * cstypes::tick_interval;

		trace_end.z -= 2.0f;

		const auto result = systems::g_tracing.trace_player_bbox( trace_start, trace_end, { mins, maxs }, filter, movement_services );
		const auto valid_trace = ( result.fraction > 0.0f && result.fraction < 1.0f );

		if ( !valid_trace || result.normal.z < sv_standable_normal )
		{
			return;
		}

		const auto dot = velocity.x * result.normal.x + velocity.y * result.normal.y;
		this->m_active_this_tick = result.normal.z >= 0.98f || dot >= 0.0f;

		const auto when = std::clamp( result.fraction, 0.001f, 0.99f );
		//const auto when = std::clamp( std::round( result.fraction * 64.0f ) / 64.0f, 1.0f / 64.0f, 63.0f / 64.0f );

		this->m_landing_fraction = when;

		//buttons.value &= ~cstypes::command_buttons::in_duck;
		//buttons.value_changed &= ~cstypes::command_buttons::in_duck;

		const auto base = cmd->csgo_user_cmd.mutable_base( );
		if ( !base )
		{
			return;
		}

		// i know this looks sketchy but i swear it works
		if (result.normal.z < 0.985f)
		{
			this->m_active_this_tick = false;
		}

		const auto subtick_moves = base->mutable_subtick_moves( );

		if ( const auto duck_down = systems::g_input.acquire_subtick_step( subtick_moves ) )
		{
			duck_down->set_button( cstypes::command_buttons::in_duck );
			duck_down->set_pressed( true );
			duck_down->set_when( 0.0f );
			duck_down->set_analog_forward_delta( 0.0f );
			duck_down->set_analog_left_delta( 0.0f );
		}

		if ( const auto duck_up = systems::g_input.acquire_subtick_step( subtick_moves ) )
		{
			duck_up->set_button( cstypes::command_buttons::in_duck );
			duck_up->set_pressed( false );
			duck_up->set_when( when );
			duck_up->set_analog_forward_delta( 0.0f );
			duck_up->set_analog_left_delta( 0.0f );
		}

		if ( const auto jump_up = systems::g_input.acquire_subtick_step( subtick_moves ) )
		{
			jump_up->set_button( cstypes::command_buttons::in_jump );
			jump_up->set_pressed( false );
			jump_up->set_when( when );
			jump_up->set_analog_forward_delta( 0.0f );
			jump_up->set_analog_left_delta( 0.0f );
		}

		if ( const auto jump_down = systems::g_input.acquire_subtick_step( subtick_moves ) )
		{
			jump_down->set_button( cstypes::command_buttons::in_jump );
			jump_down->set_pressed( true );
			jump_down->set_when( when );
			jump_down->set_analog_forward_delta( 0.0f );
			jump_down->set_analog_left_delta( 0.0f );
		}

		//buttons.value &= ~cstypes::command_buttons::in_jump;
		//buttons.value_changed &= ~cstypes::command_buttons::in_jump;
	}

	float jumpbug::get_impulse_mul( std::uintptr_t local_pawn ) const
	{
		const auto movement_services = memory::read<std::uintptr_t>( local_pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );
		if ( !movement_services )
		{
			return 0.0f;
		}

		const auto stamina = memory::read<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_flStamina"_hash ) );

		if (CONVAR ("sv_legacy_jump")->get<bool>( ) )
		{
			if ( stamina <= 0.0f )
			{
				return 1.0f;
			}

			return std::clamp( 1.0f - ( stamina / 100.0f ), 0.0f, 1.0f );
		}

		const auto current_tick = memory::read<int>( memory::read<std::uintptr_t>( addresses::globals::global_vars ) + 0x44 );
		const auto modern_jump = movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_ModernJump"_hash );
		const auto landing_vel_z = memory::read<float>( modern_jump + SCHEMA( "CCSPlayerModernJump", "m_flLastLandedVelocityZ"_hash ) );
		const auto landed_tick = memory::read<std::uint32_t>( modern_jump + SCHEMA( "CCSPlayerModernJump", "m_nLastLandedTick"_hash ) );
		const auto base = std::clamp( ( landing_vel_z * 0.0005f ) + 1.0f, 0.02f, 1.0f );
		const auto ticks_since_landing = static_cast< float >( current_tick - landed_tick );

		auto result = std::clamp( base + ( ticks_since_landing * 0.6f ), 0.0f, 1.0f );

		if ( stamina > 0.0f )
		{
			result *= std::clamp( 1.0f - ( stamina / 100.0f ), 0.0f, 1.0f );
		}

		return result;
	}

} // namespace features::movement