#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>

namespace features::esp::item {

	bool glow::on_is_glowing( std::uintptr_t owner_entity, std::uint32_t owner_hash ) const
	{
		const auto& glow_cfg = settings::g_esp.m_item.m_glow;
		if ( !glow_cfg.enabled.value )
		{
			return false;
		}

		const auto group_id = this->get_item_group( owner_hash );
		if ( group_id == UINT32_MAX )
		{
			return false;
		}

		const auto owner_handle = memory::read<std::uint32_t>( owner_entity + SCHEMA( "C_BaseEntity", "m_hOwnerEntity"_hash ) );
		if ( owner_handle && owner_handle != 0xffffffff )
		{
			return false;
		}

		const auto game_scene_node = memory::read<std::uintptr_t>( owner_entity + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		if ( game_scene_node )
		{
			const auto parent_node = memory::read<std::uintptr_t>( game_scene_node + SCHEMA( "CGameSceneNode", "m_pParent"_hash ) );
			if ( parent_node )
			{
				return false;
			}
		}

		return glow_cfg.get_group( group_id ).enabled.value;
	}

	bool glow::on_get_glow_color( std::uintptr_t owner_entity, std::uint32_t owner_hash, float* color ) const
	{
		const auto& glow_cfg = settings::g_esp.m_item.m_glow;
		if ( !glow_cfg.enabled.value )
		{
			return false;
		}

		const auto group_id = this->get_item_group( owner_hash );
		if ( group_id == UINT32_MAX )
		{
			return false;
		}

		const auto owner_handle = memory::read<std::uint32_t>( owner_entity + SCHEMA( "C_BaseEntity", "m_hOwnerEntity"_hash ) );
		if ( owner_handle && owner_handle != 0xffffffff )
		{
			return false;
		}

		const auto game_scene_node = memory::read<std::uintptr_t>( owner_entity + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		if ( game_scene_node )
		{
			const auto parent_node = memory::read<std::uintptr_t>( game_scene_node + SCHEMA( "CGameSceneNode", "m_pParent"_hash ) );
			if ( parent_node )
			{
				return false;
			}
		}

		const auto& cfg = glow_cfg.get_group( group_id );
		if ( !cfg.enabled.value )
		{
			return false;
		}

		color[ 0 ] = cfg.color.value.r / 255.0f;
		color[ 1 ] = cfg.color.value.g / 255.0f;
		color[ 2 ] = cfg.color.value.b / 255.0f;
		color[ 3 ] = cfg.color.value.a / 255.0f;

		return true;
	}

	std::uint32_t glow::get_item_group( std::uint32_t schema_hash ) const
	{
		switch ( schema_hash )
		{
		case "C_DEagle"_hash:
		case "C_WeaponElite"_hash:
		case "C_WeaponFiveSeven"_hash:
		case "C_WeaponGlock"_hash:
		case "C_WeaponHKP2000"_hash:
		case "C_WeaponUSPSilencer"_hash:
		case "C_WeaponP250"_hash:
		case "C_WeaponCZ75a"_hash:
		case "C_WeaponTec9"_hash:
		case "C_WeaponRevolver"_hash:
			return 0;

		case "C_WeaponMAC10"_hash:
		case "C_WeaponMP5SD"_hash:
		case "C_WeaponMP7"_hash:
		case "C_WeaponMP9"_hash:
		case "C_WeaponBizon"_hash:
		case "C_WeaponP90"_hash:
		case "C_WeaponUMP45"_hash:
			return 1;

		case "C_AK47"_hash:
		case "C_WeaponM4A1"_hash:
		case "C_WeaponM4A1Silencer"_hash:
		case "C_WeaponAug"_hash:
		case "C_WeaponFamas"_hash:
		case "C_WeaponGalilAR"_hash:
		case "C_WeaponSG556"_hash:
			return 2;

		case "C_WeaponNOVA"_hash:
		case "C_WeaponSawedoff"_hash:
		case "C_WeaponXM1014"_hash:
		case "C_WeaponMag7"_hash:
			return 3;

		case "C_WeaponAWP"_hash:
		case "C_WeaponG3SG1"_hash:
		case "C_WeaponSCAR20"_hash:
		case "C_WeaponSSG08"_hash:
		case "C_WeaponM249"_hash:
		case "C_WeaponNegev"_hash:
			return 4;

		case "C_HEGrenade"_hash:
		case "C_Flashbang"_hash:
		case "C_SmokeGrenade"_hash:
		case "C_MolotovGrenade"_hash:
		case "C_IncendiaryGrenade"_hash:
		case "C_DecoyGrenade"_hash:
		case "C_C4"_hash:
		case "C_WeaponTaser"_hash:
		case "C_Item_Healthshot"_hash:
		case "C_Knife"_hash:
			return 5;

		default:
			return UINT32_MAX;
		}
	}

} // namespace features::esp::item