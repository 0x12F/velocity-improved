#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/math/math.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>

namespace features::movement {

	void edgejump::on_create_move( systems::input::usercmd* cmd ) const
	{
		if ( !settings::g_movement.edgejump.value )
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

		const auto move_type = memory::read<std::uint8_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_nActualMoveType"_hash ) );
		if ( move_type == cstypes::move_type::ladder || move_type == cstypes::move_type::noclip )
		{
			return;
		}

		const auto velocity = prestate.networked_velocity;
		if ( velocity.length_2d( ) < 1.0f )
		{
			return;
		}

		const auto game_scene_node = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		if ( !game_scene_node )
		{
			return;
		}

		const auto origin = memory::read<math::vector3>( game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
		const auto movement_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );

		if ( !movement_services )
		{
			return;
		}

		const auto collision = local.pawn + SCHEMA( "C_BaseModelEntity", "m_Collision"_hash );
		const auto mins = memory::read<math::vector3>( collision + SCHEMA( "CCollisionProperty", "m_vecMins"_hash ) );
		const auto maxs = memory::read<math::vector3>( collision + SCHEMA( "CCollisionProperty", "m_vecMaxs"_hash ) );

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
		const auto sv_standable_normal = CONVAR ("sv_standable_normal")->get<float>( );

		const auto check_edge = [ & ]( int ticks_ahead ) -> bool
			{
				auto predicted = origin;
				predicted.x += velocity.x * cstypes::tick_interval * static_cast< float >( ticks_ahead );
				predicted.y += velocity.y * cstypes::tick_interval * static_cast< float >( ticks_ahead );

				const auto start = math::vector3{ predicted.x, predicted.y, predicted.z + 2.0f };
				const auto end = math::vector3{ predicted.x, predicted.y, predicted.z - 4.0f };

				const auto result = systems::g_tracing.trace_player_bbox( start, end, { mins, maxs }, filter, movement_services );
				return result.fraction >= 1.0f || result.normal.z < sv_standable_normal;
			};

		const auto current_on_edge = check_edge( 1 );
		const auto next_on_edge = check_edge( 2 );

		if ( next_on_edge && !current_on_edge )
		{
			const auto vel_xy = math::vector2{ velocity.x, velocity.y };
			const auto speed_xy = vel_xy.length( );

			math::vector2 perp{ 0.0f, 0.0f };
			if ( speed_xy >= 0.01f )
			{
				perp = math::vector2{ -vel_xy.y / speed_xy, vel_xy.x / speed_xy };
			}

			static constexpr float k_lateral[ ]{ 0.0f, 7.0f, -7.0f, 14.0f, -14.0f, 22.0f, -22.0f, 34.0f, -34.0f };

			const auto ground_ahead_has_stair_step = [ & ]( int ticks_ahead ) -> bool
				{
					const auto bt = cstypes::tick_interval * static_cast< float >( ticks_ahead );
					const auto base_x = origin.x + velocity.x * bt;
					const auto base_y = origin.y + velocity.y * bt;

					for ( const auto lat : k_lateral )
					{
						const auto wx = base_x + perp.x * lat;
						const auto wy = base_y + perp.y * lat;

						const auto stair_scan_start = math::vector3{ wx, wy, origin.z + 2.0f };
						const auto stair_scan_end = math::vector3{ wx, wy, origin.z - 110.0f };
						const auto stair_trace = systems::g_tracing.trace_player_bbox( stair_scan_start, stair_scan_end, { mins, maxs }, filter, movement_services );

						if ( stair_trace.fraction >= 1.0f || stair_trace.normal.z < sv_standable_normal )
						{
							continue;
						}

						const auto step_down = origin.z - stair_trace.position.z;
						constexpr auto k_min_step = 0.12f;
						constexpr auto k_max_step = 48.0f;

						if ( step_down >= k_min_step && step_down <= k_max_step )
						{
							return true;
						}
					}

					return false;
				};

			if ( ground_ahead_has_stair_step( 1 ) || ground_ahead_has_stair_step( 2 ) )
			{
				return;
			}

			cmd->buttons.value |= cstypes::command_buttons::in_jump;
			cmd->buttons.value_changed |= cstypes::command_buttons::in_jump;
		}
	}

} // namespace features::movement