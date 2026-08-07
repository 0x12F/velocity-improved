#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>

#include "../systems.hpp"

namespace systems {

	void entities::on_add_entity( std::uintptr_t entity, std::uint32_t handle )
	{
		if ( !entity )
		{
			return;
		}

		const auto index = static_cast< std::int16_t >( handle & 0x7fff );
		if ( index < 0 || index > 0x3fff )
		{
			return;
		}

		const auto schema_name = this->get_schema_name( entity );
		if ( !schema_name || !schema_name[ 0 ] )
		{
			return;
		}

		const auto hashed = fnv1a::runtime_hash( schema_name );
		const auto entity_type = this->classify_entity( hashed );

		if ( entity_type == type::unknown )
		{
			return;
		}

		cached entry{};
		entry.ptr = entity;
		entry.index = index;
		entry.schema_hash = hashed;
		entry.type = entity_type;

		std::unique_lock lock( this->m_cache_mtx );

		for ( const auto& c : this->m_cached )
		{
			if ( c.index == index )
			{
				return;
			}
		}

		this->m_cached.emplace_back( entry );
	}

	void entities::on_remove_entity( std::uintptr_t entity, std::uint32_t handle )
	{
		if ( !entity )
		{
			return;
		}

		const auto index = static_cast< std::int16_t >( handle & 0x7fff );
		if ( index < 0 || index > 0x3fff )
		{
			return;
		}

		std::unique_lock lock( this->m_cache_mtx );

		for ( auto it = this->m_cached.begin( ); it != this->m_cached.end( ); ++it )
		{
			if ( it->index == index )
			{
				if ( it != this->m_cached.end( ) - 1 )
				{
					*it = this->m_cached.back( );
				}

				this->m_cached.pop_back( );
				return;
			}
		}
	}

	void entities::force_update( )
	{
		{
			std::unique_lock lock( this->m_cache_mtx );
			this->m_cached.clear( );
			this->m_cached.reserve( 128 );
		}

		for ( auto i = 0; i < 2048; ++i )
		{
			const auto entity = this->get_by_index( i );
			if ( !entity )
			{
				continue;
			}

			this->on_add_entity( entity, static_cast< std::uint32_t >( i ) );
		}
	}

	bool entities::exists( std::uintptr_t entity_ptr ) const
	{
		std::shared_lock lock( this->m_cache_mtx );

		for ( const auto& c : this->m_cached )
		{
			if ( c.ptr == entity_ptr )
			{
				return true;
			}
		}

		return false;
	}

	const char* entities::get_schema_name( std::uintptr_t entity ) const
	{
		const auto identity = memory::safe_read<std::uintptr_t>( entity + 0x10 ).value_or( 0 );
		if ( !identity )
		{
			return nullptr;
		}

		const auto entity_class = memory::safe_read<std::uintptr_t>( identity + 0x8 ).value_or( 0 );
		if ( !entity_class )
		{
			return nullptr;
		}

		// CEntityClass owns the current SchemaClassInfo pointer at 0x58.
		const auto class_info = memory::safe_read<std::uintptr_t>( entity_class + 0x58 ).value_or( 0 );
		if ( !class_info )
		{
			return nullptr;
		}

		return memory::safe_read<const char*>( class_info + 0x8 ).value_or( nullptr );
	}

	std::uintptr_t entities::get_by_index( std::int32_t index )
	{
		const auto entity_list = memory::read<std::uintptr_t>( addresses::globals::entity_list );
		if ( !entity_list )
		{
			return 0;
		}

		if ( this->m_cached_entity_list != entity_list )
		{
			this->m_cached_entity_list = entity_list;
			this->m_cached_list_entries.fill( 0 );
		}

		const auto chunk_index = index >> 9;

		if ( !this->m_cached_list_entries[ chunk_index ] )
		{
			this->m_cached_list_entries[ chunk_index ] = memory::read<std::uintptr_t>( entity_list + ( static_cast< std::uintptr_t >( chunk_index ) * 8 ) + 0x10 );
		}

		const auto list_entry = this->m_cached_list_entries[ chunk_index ];
		if ( !list_entry )
		{
			return 0;
		}

		return memory::read<std::uintptr_t>( list_entry + ( static_cast< std::uintptr_t >( index & 0x1ff ) * 112 ) );
	}

	std::uintptr_t entities::lookup( std::uint32_t handle ) const
	{
		if ( !handle || handle == 0xffffffff )
		{
			return 0;
		}

		const auto entity_list = memory::read<std::uintptr_t>( addresses::globals::entity_list );
		if ( !entity_list )
		{
			return 0;
		}

		const auto list_entry = memory::read<std::uintptr_t>( entity_list + ( static_cast< std::uintptr_t >( ( handle & 0x7fff ) >> 9 ) * 8 ) + 0x10 );
		if ( !list_entry || list_entry == 0xffffffffffffffff )
		{
			return 0;
		}

		const auto entity = memory::read<std::uintptr_t>( list_entry + ( static_cast< std::uintptr_t >( handle & 0x1ff ) * 112 ) );
		if ( !entity || entity == 0xffffffffffffffff || entity < 0x10000 )
		{
			return 0;
		}

		return entity;
	}

	std::vector<entities::cached> entities::get_by_type( type type ) const
	{
		std::shared_lock lock( this->m_cache_mtx );

		std::vector<cached> result{};
		result.reserve( this->m_cached.size( ) );

		for ( const auto& c : this->m_cached )
		{
			if ( c.type == type )
			{
				result.emplace_back( c );
			}
		}

		return result;
	}

	bool entities::is_empty( ) const
	{
		std::shared_lock lock( this->m_cache_mtx );
		return this->m_cached.empty( );
	}

	entities::type entities::classify_entity( std::uint32_t schema_hash ) const
	{
		switch ( schema_hash )
		{
		case "CCSPlayerController"_hash:
			return type::player;

		case "C_AK47"_hash:
		case "C_WeaponM4A1"_hash:
		case "C_WeaponM4A1Silencer"_hash:
		case "C_WeaponAWP"_hash:
		case "C_WeaponAug"_hash:
		case "C_WeaponFamas"_hash:
		case "C_WeaponGalilAR"_hash:
		case "C_WeaponSG556"_hash:
		case "C_WeaponG3SG1"_hash:
		case "C_WeaponSCAR20"_hash:
		case "C_WeaponSSG08"_hash:
		case "C_WeaponMAC10"_hash:
		case "C_WeaponMP5SD"_hash:
		case "C_WeaponMP7"_hash:
		case "C_WeaponMP9"_hash:
		case "C_WeaponBizon"_hash:
		case "C_WeaponP90"_hash:
		case "C_WeaponUMP45"_hash:
		case "C_WeaponNOVA"_hash:
		case "C_WeaponSawedoff"_hash:
		case "C_WeaponXM1014"_hash:
		case "C_WeaponMag7"_hash:
		case "C_WeaponM249"_hash:
		case "C_WeaponNegev"_hash:
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
		case "C_WeaponTaser"_hash:
		case "C_Knife"_hash:
		case "C_C4"_hash:
		case "C_Item_Healthshot"_hash:
		case "C_HEGrenade"_hash:
		case "C_Flashbang"_hash:
		case "C_SmokeGrenade"_hash:
		case "C_MolotovGrenade"_hash:
		case "C_IncendiaryGrenade"_hash:
		case "C_DecoyGrenade"_hash:
			return type::item;

		case "C_HEGrenadeProjectile"_hash:
		case "C_FlashbangProjectile"_hash:
		case "C_SmokeGrenadeProjectile"_hash:
		case "C_MolotovProjectile"_hash:
		case "C_Inferno"_hash:
		case "C_DecoyProjectile"_hash:
			return type::projectile;

		default:
			return type::unknown;
		}
	}

} // namespace systems
