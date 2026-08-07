#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>

#include "../systems.hpp"
#include <protection/game_addresses.hpp>

namespace systems {

	void local::update( )
	{
		const auto local_player_controller = memory::read<std::uintptr_t>( addresses::globals::local_player_controller );
		if ( !local_player_controller )
		{
			this->reset( );
			return;
		}

		const auto pawn_handle = memory::read<std::uint32_t>( local_player_controller + SCHEMA( "CBasePlayerController", "m_hPawn"_hash ) );
		if ( !pawn_handle )
		{
			this->reset( );
			return;
		}

		const auto pawn = g_entities.lookup( pawn_handle );
		if ( !pawn )
		{
			this->reset( );
			return;
		}

		snapshot s{};
		s.controller = local_player_controller;
		s.pawn = pawn;
		s.team = memory::read<int>( pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );

		const auto health = memory::read<int>( pawn + SCHEMA( "C_BaseEntity", "m_iHealth"_hash ) );
		const auto life_state = memory::read<std::uint8_t>( pawn + SCHEMA( "C_BaseEntity", "m_lifeState"_hash ) );
		s.is_alive = health > 0 && life_state == 0;

		if ( s.is_alive )
		{
			s.view_team = s.team;
		}
		else
		{
			const auto observer_pawn_handle = memory::read<std::uint32_t>( local_player_controller + SCHEMA( "CCSPlayerController", "m_hObserverPawn"_hash ) );
			if ( observer_pawn_handle )
			{
				const auto observer_pawn = g_entities.lookup( observer_pawn_handle );
				if ( observer_pawn )
				{
					const auto observer_services = memory::read<std::uintptr_t>( observer_pawn + SCHEMA( "C_BasePlayerPawn", "m_pObserverServices"_hash ) );
					if ( observer_services )
					{
						const auto observer_target_handle = memory::read<std::uint32_t>( observer_services + SCHEMA( "CPlayer_ObserverServices", "m_hObserverTarget"_hash ) );
						if ( observer_target_handle )
						{
							const auto observer_target = g_entities.lookup( observer_target_handle );
							if ( observer_target )
							{
								s.observer_pawn = observer_target;
								s.view_team = memory::read<int>( observer_target + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );

								const auto observer_target_controller_handle = memory::read<std::uint32_t>( observer_target + SCHEMA( "C_BasePlayerPawn", "m_hController"_hash ) );
								if ( observer_target_controller_handle )
								{
									const auto observer_target_controller = g_entities.lookup( observer_target_controller_handle );
									if ( observer_target_controller )
									{
										s.observer_controller = observer_target_controller;
									}
								}
							}
						}
					}
				}
			}

			if ( !s.observer_pawn )
			{
				s.view_team = s.team;
			}
		}

		{
			const auto game_type = CONVAR ("game_type")->get<int> ();
			const auto game_mode = CONVAR ("game_mode")->get<int>( );
			const auto is_ffa = ( game_type == 1 && game_mode == 2 ) || ( game_type == 2 && game_mode == 0 );

			s.is_team_mode = !is_ffa;

			this->m_is_deathmatch.store( game_type == 1 && game_mode == 2 );
		}

		{
			const auto game_rules = memory::read<std::uintptr_t>( addresses::globals::game_rules );
			const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );

			auto cinematic{ false };
			auto freezetime{ false };

			if ( game_rules && global_vars )
			{
				if ( memory::read<bool>( game_rules + SCHEMA( "C_CSGameRules", "m_bTeamIntroPeriod"_hash ) ) )
				{
					cinematic = true;
				}
				else if ( memory::read<int>( game_rules + SCHEMA( "C_CSGameRules", "m_gamePhase"_hash ) ) >= 4 )
				{
					cinematic = true;
				}

				if ( memory::read<bool>( game_rules + SCHEMA( "C_CSGameRules", "m_bFreezePeriod"_hash ) ) )
				{
					freezetime = true;
				}
				else
				{
					const auto round_start_time = memory::read<float>( game_rules + SCHEMA( "C_CSGameRules", "m_fRoundStartTime"_hash ) );
					const auto current_time = memory::read<float>( global_vars + 0x30 );

					if ( round_start_time > current_time )
					{
						freezetime = true;
					}
				}
			}

			this->m_is_in_cinematic.store( cinematic );
			this->m_is_in_time_freeze.store( freezetime );
		}

		{
			std::unique_lock lock( this->m_mtx );
			this->m_snapshot = s;
		}
	}

	void local::reset( )
	{
		{
			std::unique_lock lock( this->m_mtx );
			this->m_snapshot = {};
		}

		this->m_is_deathmatch.store( false );
		this->m_is_in_cinematic.store( false );
		this->m_is_in_time_freeze.store( false );
	}

} // namespace systems
