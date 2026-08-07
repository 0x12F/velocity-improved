#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>

namespace features::changer {

	void agents::on_frame_stage_notify( )
	{
		const auto local = systems::g_local.get( );
		if ( !local.is_alive || systems::g_local.is_in_cinematic( ) || !local.pawn )
		{
			return;
		}

		const auto team = memory::read<int>( local.pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
		const auto selected_def_index = ( team == 3 ) ? settings::g_changer.agents.ct_def : ( team == 2 ) ? settings::g_changer.agents.t_def : static_cast< std::int16_t >( 0 );

		const econ_item_system::item_def* selected{ nullptr };
		if ( selected_def_index != 0 )
		{
			selected = g_econ_item_system.find_def( selected_def_index );
		}

		if ( this->m_tracked_pawn != local.pawn )
		{
			this->m_original_model.clear( );
			this->m_overridden = false;
			this->m_applied_handle = 0;
			this->m_applied_def = 0;
			this->m_tracked_team = 0;
			this->m_tracked_pawn = local.pawn;
		}

		const auto game_scene_node = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		if ( !game_scene_node )
		{
			return;
		}

		const auto model_state = game_scene_node + SCHEMA( "CSkeletonInstance", "m_modelState"_hash );

		if ( !selected || selected->model_player.empty( ) )
		{
			if ( this->m_overridden && !this->m_original_model.empty( ) )
			{
				memory::call<void>(PATTERN (patterns::set_player_model), local.pawn, this->m_original_model.c_str( ) );

				//const auto controller = memory::read<std::uintptr_t>( local.pawn + 0x3268 );
				//if ( controller )
				//{
				//    memory::call<void>( addresses::functions::pawn_graph_unbind_all, controller );
				//    memory::call<void>( addresses::functions::pawn_graph_rebind_all, controller );
				//}

				this->cycle_weapon_owners( local.pawn );

				this->m_applied_handle = 0;
				this->m_applied_def = 0;
				this->m_tracked_team = 0;
				this->m_overridden = false;
			}
			return;
		}

		if ( !this->m_overridden && this->m_original_model.empty( ) )
		{
			const auto model_name_ptr = memory::read<std::uintptr_t>( model_state + SCHEMA( "CModelState", "m_ModelName"_hash ) );
			if ( model_name_ptr )
			{
				this->m_original_model = memory::read_string( model_name_ptr );
			}
		}

		const auto current_handle = memory::read<std::uintptr_t>( model_state + SCHEMA( "CModelState", "m_hModel"_hash ) );
		const auto selection_matches = ( this->m_applied_def == selected->def_index );
		const auto team_matches = ( this->m_tracked_team == team );
		const auto handle_matches = ( this->m_applied_handle != 0 && current_handle == this->m_applied_handle );

		if ( this->m_overridden && selection_matches && team_matches && handle_matches )
		{
			return;
		}

		if ( this->m_overridden && !team_matches )
		{
			this->m_original_model.clear( );
			this->m_overridden = false;

			const auto model_name_ptr = memory::read<std::uintptr_t>( model_state + SCHEMA( "CModelState", "m_ModelName"_hash ) );
			if ( model_name_ptr )
			{
				this->m_original_model = memory::read_string( model_name_ptr );
			}
		}

		memory::call<void>(PATTERN (patterns::set_player_model), local.pawn, selected->model_player.c_str( ) );

		//const auto controller = memory::read<std::uintptr_t>( local_pawn + 0x3268 );
		//if ( controller )
		//{
		//    memory::call<void>( addresses::functions::pawn_graph_unbind_all, controller );
		//    memory::call<void>( addresses::functions::pawn_graph_rebind_all, controller );
		//}

		const auto collision = local.pawn + SCHEMA( "C_BaseModelEntity", "m_Collision"_hash );
		memory::write<math::vector3>( collision + SCHEMA( "CCollisionProperty", "m_vecMins"_hash ), math::vector3( -16.0f, -16.0f, 0.0f ) );
		memory::write<math::vector3>( collision + SCHEMA( "CCollisionProperty", "m_vecMaxs"_hash ), math::vector3( 16.0f, 16.0f, 72.0f ) );

		this->cycle_weapon_owners( local.pawn );

		this->m_applied_handle = memory::read<std::uintptr_t>( model_state + SCHEMA( "CModelState", "m_hModel"_hash ) );
		this->m_applied_def = selected->def_index;
		this->m_tracked_team = team;
		this->m_overridden = true;
	}

	void agents::cycle_weapon_owners( std::uintptr_t pawn )
	{
		const auto weapon_services = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BasePlayerPawn", "m_pWeaponServices"_hash ) );
		if ( !weapon_services )
		{
			return;
		}

		const auto weapons_base = weapon_services + SCHEMA( "CPlayer_WeaponServices", "m_hMyWeapons"_hash );
		const auto weapons_size = memory::read<int>( weapons_base );
		const auto weapons_data = memory::read<std::uintptr_t>( weapons_base + 0x8 );

		if ( !weapons_data || weapons_size <= 0 )
		{
			return;
		}

		for ( auto i = 0; i < weapons_size; ++i )
		{
			const auto handle = memory::read<std::uint32_t>( weapons_data + i * sizeof( std::uint32_t ) );
			const auto weapon = systems::g_entities.lookup( handle );

			if ( !weapon )
			{
				continue;
			}

			const auto owner_off = SCHEMA( "C_BaseEntity", "m_hOwnerEntity"_hash );
			const auto saved_owner = memory::read<std::uint32_t>( weapon + owner_off );

			memory::write<std::uint32_t>( weapon + owner_off, 0xffffffff );
			memory::write<std::uint32_t>( weapon + owner_off, saved_owner );
		}
	}

} // namespace features::changer