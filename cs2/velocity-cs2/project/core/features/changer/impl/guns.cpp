#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>
namespace features::changer {

	void guns::on_frame_stage_notify( )
	{
		this->process_hud_clear( );

		const auto local = systems::g_local.get( );
		if ( !local.is_alive || systems::g_local.is_in_cinematic( ) || !local.pawn || !local.controller )
		{
			return;
		}

		const auto weapon_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pWeaponServices"_hash ) );
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

		const auto steam_id = memory::read<std::uintptr_t>( local.controller + SCHEMA( "CBasePlayerController", "m_steamID"_hash ) );
		const auto account_id = static_cast< std::uint32_t >( steam_id & 0xffffffff );
		const auto active_handle = memory::read<std::uint32_t>( weapon_services + SCHEMA( "CPlayer_WeaponServices", "m_hActiveWeapon"_hash ) );
		const auto active_weapon = systems::g_entities.lookup( active_handle );

		if ( this->m_tracked_pawn != local.pawn )
		{
			this->m_applied_weapons.clear( );
			this->m_last_active_handle = 0;
			this->m_tracked_pawn = local.pawn;
		}

		for ( auto i = 0; i < weapons_size; ++i )
		{
			const auto handle = memory::read<std::uint32_t>( weapons_data + i * sizeof( std::uint32_t ) );
			const auto weapon = systems::g_entities.lookup( handle );

			if ( !weapon )
			{
				continue;
			}

			const auto iv = weapon + SCHEMA( "C_EconEntity", "m_AttributeManager"_hash ) + SCHEMA( "C_AttributeContainer", "m_Item"_hash );
			const auto current_def_index = memory::read<std::uint16_t>( iv + SCHEMA( "C_EconItemView", "m_iItemDefinitionIndex"_hash ) );
			const auto current_def = g_econ_item_system.find_def( static_cast< std::int16_t >( current_def_index ) );

			if ( !current_def || current_def->category != econ_item_system::item_category::gun )
			{
				continue;
			}

			const auto skin_it = settings::g_changer.skins.data.find( current_def_index );
			if ( skin_it == settings::g_changer.skins.data.end( ) )
			{
				continue;
			}

			const auto& skin = skin_it->second;
			const auto applied_it = this->m_applied_weapons.find( handle );

			if ( applied_it != this->m_applied_weapons.end( ) && applied_it->second == skin.paint_kit_id )
			{
				continue;
			}

			if ( !memory::read<std::uintptr_t>( weapon + SCHEMA( "C_BaseEntity", "m_nSubclassID"_hash ) + 0x8 ) )
			{
				continue;
			}

			this->apply( weapon, iv, handle, active_handle, local.pawn, &skin, account_id );
			this->m_applied_weapons[ handle ] = skin.paint_kit_id;
		}

		if ( active_handle != this->m_last_active_handle )
		{
			this->m_last_active_handle = active_handle;

			if ( active_weapon )
			{
				const auto iv = active_weapon + SCHEMA( "C_EconEntity", "m_AttributeManager"_hash ) + SCHEMA( "C_AttributeContainer", "m_Item"_hash );
				const auto def_index = memory::read<std::uint16_t>( iv + SCHEMA( "C_EconItemView", "m_iItemDefinitionIndex"_hash ) );
				const auto def = g_econ_item_system.find_def( static_cast< std::int16_t >( def_index ) );

				if ( def && def->category == econ_item_system::item_category::gun )
				{
					const auto paint_kit_id = memory::read<int>( active_weapon + SCHEMA( "C_EconEntity", "m_nFallbackPaintKit"_hash ) );
					const auto pk = g_econ_item_system.find_paint_kit( paint_kit_id );
					this->update_view_model( local.pawn, pk );
				}
			}
		}
	}

	void guns::apply( std::uintptr_t weapon, std::uintptr_t iv, std::uint32_t handle, std::uint32_t active_handle, std::uintptr_t pawn, const settings::changer::applied_skin* skin, std::uint32_t account_id )
	{
		this->m_pending_hud_iv = 0;

		memory::write<std::uint32_t>( iv + SCHEMA( "C_EconItemView", "m_iItemIDHigh"_hash ), 0xf0000000 );
		memory::write<std::uint32_t>( iv + SCHEMA( "C_EconItemView", "m_iItemIDLow"_hash ), 0x10 );
		memory::write<std::uint32_t>( iv + SCHEMA( "C_EconItemView", "m_iAccountID"_hash ), account_id );
		memory::write<bool>( iv + SCHEMA( "C_EconItemView", "m_bInitialized"_hash ), true );

		memory::write<int>( weapon + SCHEMA( "C_EconEntity", "m_nFallbackPaintKit"_hash ), skin->paint_kit_id );
		memory::write<int>( weapon + SCHEMA( "C_EconEntity", "m_nFallbackSeed"_hash ), skin->seed );
		memory::write<float>( weapon + SCHEMA( "C_EconEntity", "m_flFallbackWear"_hash ), skin->wear );
		memory::write<int>( weapon + SCHEMA( "C_EconEntity", "m_nFallbackStatTrak"_hash ), skin->stattrak ? 0 : -1 );

		const auto pk = g_econ_item_system.find_paint_kit( skin->paint_kit_id );

		this->rebuild_paint( weapon, handle, active_handle, pawn, pk );
		this->schedule_hud_clear( iv );
	}

	void guns::rebuild_paint( std::uintptr_t weapon, std::uint32_t handle, std::uint32_t active_handle, std::uintptr_t pawn, const econ_item_system::paint_kit* pk )
	{
		const auto is_legacy = pk && pk->legacy_model;
		const auto mesh_group = is_legacy ? std::uint64_t{ 2 } : std::uint64_t{ 1 };

		if ( handle == active_handle )
		{
			this->update_view_model( pawn, pk );
		}

		const auto weapon_scene_node = memory::read<std::uintptr_t>( weapon + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		if ( weapon_scene_node )
		{
			memory::call<void>( PATTERN( patterns::weapon_set_mesh_group_mask ), weapon_scene_node, mesh_group );
		}

		memory::call<void>( PATTERN( patterns::weapon_update_composite_material ), weapon + 0x608, true );
		memory::call_vfunc<void>( weapon, 10, 1 );
		memory::call<void>( PATTERN( patterns::weapon_update_skin ), weapon, true );
	}

	void guns::update_view_model( std::uintptr_t pawn, const econ_item_system::paint_kit* pk )
	{
		const auto view_model = this->find_hud_model_weapon( pawn );
		if ( !view_model )
		{
			return;
		}

		const auto view_model_scene_node = memory::read<std::uintptr_t>( view_model + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		if ( !view_model_scene_node )
		{
			return;
		}

		const auto is_legacy = pk && pk->legacy_model;
		memory::call<void>( PATTERN( patterns::weapon_set_mesh_group_mask ), view_model_scene_node, is_legacy ? std::uint64_t{ 2 } : std::uint64_t{ 1 } );
	}

	std::uintptr_t guns::find_hud_model_weapon( std::uintptr_t pawn )
	{
		const auto arms_handle = memory::read<std::uint32_t>( pawn + SCHEMA( "C_CSPlayerPawn", "m_hHudModelArms"_hash ) );
		if ( !arms_handle )
		{
			return 0;
		}

		const auto arms = systems::g_entities.lookup( arms_handle );
		if ( !arms )
		{
			return 0;
		}

		const auto arms_scene_node = memory::read<std::uintptr_t>( arms + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		if ( !arms_scene_node )
		{
			return 0;
		}

		auto child = memory::read<std::uintptr_t>( arms_scene_node + SCHEMA( "CGameSceneNode", "m_pChild"_hash ) );

		while ( child && child > 0x10000 )
		{
			const auto owner = memory::read<std::uintptr_t>( child + SCHEMA( "CGameSceneNode", "m_pOwner"_hash ) );
			if ( owner && owner > 0x10000 )
			{
				const auto name = systems::g_entities.get_schema_name( owner );
				if ( name && fnv1a::runtime_hash( name ) == "C_CS2HudModelWeapon"_hash )
				{
					return owner;
				}
			}

			child = memory::read<std::uintptr_t>( child + SCHEMA( "CGameSceneNode", "m_pNextSibling"_hash ) );
		}

		return 0;
	}

	void guns::clear_hud_icon( std::uintptr_t iv )
	{
		const auto invalidate = PATTERN( patterns::econ_item_view_invalidate_description );
		if ( iv && invalidate )
		{
			memory::call<void>( invalidate, iv );
		}
	}

	void guns::schedule_hud_clear( std::uintptr_t iv )
	{
		this->clear_hud_icon( iv );
		this->m_pending_hud_iv = 0;
	}

	void guns::process_hud_clear( )
	{
		this->m_pending_hud_iv = 0;
	}

} // namespace features::changer
