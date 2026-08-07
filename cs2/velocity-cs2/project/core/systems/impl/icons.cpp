#include <pch/pch.hpp>
#include <utilities/game_path.hpp>
#include "../systems.hpp"

namespace systems {

	namespace detail {

		static const std::pair<std::uint32_t, const char*> hash_map[ ]
		{
			{ "C_DEagle"_hash, "deagle" },
			{ "C_WeaponElite"_hash, "elite" },
			{ "C_WeaponFiveSeven"_hash, "fiveseven" },
			{ "C_WeaponGlock"_hash, "glock" },
			{ "C_WeaponHKP2000"_hash, "hkp2000" },
			{ "C_WeaponUSPSilencer"_hash, "usp_silencer" },
			{ "C_WeaponP250"_hash, "p250" },
			{ "C_WeaponCZ75a"_hash, "cz75a" },
			{ "C_WeaponTec9"_hash, "tec9" },
			{ "C_WeaponRevolver"_hash, "revolver" },
			{ "C_WeaponMAC10"_hash, "mac10" },
			{ "C_WeaponMP5SD"_hash, "mp5sd" },
			{ "C_WeaponMP7"_hash, "mp7" },
			{ "C_WeaponMP9"_hash, "mp9" },
			{ "C_WeaponBizon"_hash, "bizon" },
			{ "C_WeaponP90"_hash, "p90" },
			{ "C_WeaponUMP45"_hash, "ump45" },
			{ "C_AK47"_hash, "ak47" },
			{ "C_WeaponM4A1"_hash, "m4a1" },
			{ "C_WeaponM4A1Silencer"_hash, "m4a1_silencer" },
			{ "C_WeaponAug"_hash, "aug" },
			{ "C_WeaponFamas"_hash, "famas" },
			{ "C_WeaponGalilAR"_hash, "galilar" },
			{ "C_WeaponSG556"_hash, "sg556" },
			{ "C_WeaponNOVA"_hash, "nova" },
			{ "C_WeaponSawedoff"_hash, "sawedoff" },
			{ "C_WeaponXM1014"_hash, "xm1014" },
			{ "C_WeaponMag7"_hash, "mag7" },
			{ "C_WeaponAWP"_hash, "awp" },
			{ "C_WeaponG3SG1"_hash, "g3sg1" },
			{ "C_WeaponSCAR20"_hash, "scar20" },
			{ "C_WeaponSSG08"_hash, "ssg08" },
			{ "C_WeaponM249"_hash, "m249" },
			{ "C_WeaponNegev"_hash, "negev" },
			{ "C_HEGrenade"_hash, "hegrenade" },
			{ "C_Flashbang"_hash, "flashbang" },
			{ "C_SmokeGrenade"_hash, "smokegrenade" },
			{ "C_MolotovGrenade"_hash, "molotov" },
			{ "C_IncendiaryGrenade"_hash, "incgrenade" },
			{ "C_DecoyGrenade"_hash, "decoy" },
			{ "C_HEGrenadeProjectile"_hash, "hegrenade" },
			{ "C_FlashbangProjectile"_hash, "flashbang" },
			{ "C_SmokeGrenadeProjectile"_hash, "smokegrenade" },
			{ "C_MolotovProjectile"_hash, "molotov" },
			{ "C_DecoyProjectile"_hash, "decoy" },
			{ "C_WeaponTaser"_hash, "taser" },
			{ "C_Knife"_hash, "knife" },
			{ "C_C4"_hash, "c4" },
			{ "C_Item_Healthshot"_hash, "healthshot" },
		};

	} // namespace detail

	bool icons::initialize( )
	{
		for ( const auto& [hash, name] : detail::hash_map )
		{
			this->m_hash_to_name[ hash ] = name;
		}

		const auto csgo_directory = game_path::csgo_directory( );
		return csgo_directory && this->load_vpk_directory( *csgo_directory / L"pak01_dir.vpk" );
	}

	void icons::shutdown( )
	{
		this->m_icons.clear( );
		this->m_pending_svgs.clear( );
		this->m_hash_to_name.clear( );
	}

	const icons::icon* icons::get( const std::string& name, float scale )
	{
		const icon_key key{ name, std::bit_cast< std::uint32_t >( scale ) };

		const auto it = this->m_icons.find( key );
		if ( it != this->m_icons.end( ) )
		{
			return &it->second;
		}

		const auto pending_it = this->m_pending_svgs.find( name );
		if ( pending_it == this->m_pending_svgs.end( ) )
		{
			return nullptr;
		}

		const auto& svg_data = pending_it->second;

		icon ico{};
		ico.texture = xdraw::load_svg( std::span<const std::byte>( svg_data.data( ), svg_data.size( ) ), scale, &ico.width, &ico.height );

		if ( !ico.texture )
		{
			return nullptr;
		}

		const auto [new_it, inserted] = this->m_icons.emplace( key, std::move( ico ) );
		return &new_it->second;
	}

	const icons::icon* icons::get( std::uint32_t schema_hash, float scale )
	{
		const auto name_it = this->m_hash_to_name.find( schema_hash );
		if ( name_it == this->m_hash_to_name.end( ) )
		{
			return nullptr;
		}

		return this->get( name_it->second, scale );
	}

	bool icons::load_vpk_directory( const std::filesystem::path& path )
	{
		std::ifstream file( path, std::ios::binary );
		if ( !file.is_open( ) )
		{
			return false;
		}

#pragma pack( push, 1 )
		struct vpk_header
		{
			std::uint32_t signature;
			std::uint32_t version;
			std::uint32_t tree_size;
			std::uint32_t file_data_section_size;
			std::uint32_t archive_md5_section_size;
			std::uint32_t other_md5_section_size;
			std::uint32_t signature_section_size;
		};

		struct vpk_entry
		{
			std::uint32_t crc;
			std::uint16_t preload_bytes;
			std::uint16_t archive_index;
			std::uint32_t entry_offset;
			std::uint32_t entry_length;
			std::uint16_t terminator;
		};
#pragma pack( pop )

		vpk_header header{};
		file.read( reinterpret_cast< char* >( &header ), sizeof( header ) );

		if ( header.signature != 0x55AA1234 )
		{
			return false;
		}

		if ( header.version != 2 )
		{
			return false;
		}

		const auto tree_end = static_cast< std::streamoff >( sizeof( vpk_header ) ) + static_cast< std::streamoff >( header.tree_size );
		const auto base_dir = path.parent_path( );

		std::unordered_set<std::string> targets;
		for ( const auto& [hash, name] : this->m_hash_to_name )
		{
			targets.insert( name );
		}

		while ( file.tellg( ) < tree_end )
		{
			std::string extension;
			std::getline( file, extension, '\0' );

			if ( extension.empty( ) )
			{
				break;
			}

			const auto is_svg = extension == "vsvg_c" || extension == "vsvg";

			while ( true )
			{
				std::string dir_path;
				std::getline( file, dir_path, '\0' );

				if ( dir_path.empty( ) )
				{
					break;
				}

				const auto is_equipment = is_svg && dir_path.find( "equipment" ) != std::string::npos;

				while ( true )
				{
					std::string filename;
					std::getline( file, filename, '\0' );

					if ( filename.empty( ) )
					{
						break;
					}

					vpk_entry entry{};
					file.read( reinterpret_cast< char* >( &entry ), sizeof( entry ) );

					if ( entry.preload_bytes > 0 )
					{
						file.seekg( entry.preload_bytes, std::ios::cur );
					}

					if ( !is_equipment )
					{
						continue;
					}

					if ( targets.find( filename ) == targets.end( ) )
					{
						continue;
					}

					char archive_name[ 32 ];
					std::snprintf( archive_name, sizeof( archive_name ), "pak01_%03d.vpk", entry.archive_index );

					this->cache_svg_bytes( base_dir / archive_name, filename, entry.entry_offset, entry.entry_length );
				}
			}
		}

		return !this->m_pending_svgs.empty( );
	}

	bool icons::cache_svg_bytes( const std::filesystem::path& vpk_path, const std::string& icon_name, std::uint32_t entry_offset, std::uint32_t entry_length )
	{
		std::ifstream archive( vpk_path, std::ios::binary );
		if ( !archive.is_open( ) )
		{
			return false;
		}

		archive.seekg( entry_offset );

		std::vector<std::byte> raw( entry_length );
		archive.read( reinterpret_cast< char* >( raw.data( ) ), entry_length );

		auto svg_data = this->decompile_vsvg( raw );
		if ( svg_data.empty( ) )
		{
			return false;
		}

		this->m_pending_svgs[ icon_name ] = std::move( svg_data );
		return true;
	}

	std::vector<std::byte> icons::decompile_vsvg( std::span<const std::byte> data ) const
	{
		const auto raw = reinterpret_cast< const std::uint8_t* >( data.data( ) );
		const auto size = data.size( );

		if ( size < 16 )
		{
			return {};
		}

		const auto header_version = *reinterpret_cast< const std::uint16_t* >( raw + 4 );
		const auto block_count = *reinterpret_cast< const std::uint32_t* >( raw + 12 );

		if ( header_version != 12 || block_count == 0 || block_count > 64 )
		{
			return {};
		}

		constexpr auto header_size{ 16u };
		constexpr auto block_entry_size{ 12u };
		constexpr auto data_fourcc{ 'D' | ( 'A' << 8 ) | ( 'T' << 16 ) | ( 'A' << 24 ) };

		for ( auto i = 0u; i < block_count; ++i )
		{
			const auto entry_pos = header_size + i * block_entry_size;
			if ( static_cast< std::size_t >( entry_pos ) + block_entry_size > size )
			{
				break;
			}

			const auto type = *reinterpret_cast< const std::uint32_t* >( raw + entry_pos );
			const auto offset = *reinterpret_cast< const std::uint32_t* >( raw + entry_pos + 4 );
			const auto block_size = *reinterpret_cast< const std::uint32_t* >( raw + entry_pos + 8 );

			if ( type != data_fourcc )
			{
				continue;
			}

			const auto data_start = static_cast< std::size_t >( entry_pos + 4 ) + offset;
			if ( data_start + block_size > size )
			{
				break;
			}

			const auto block_ptr = reinterpret_cast< const char* >( raw + data_start );

			for ( auto j = 0u; j + 4 < block_size; ++j )
			{
				if ( block_ptr[ j ] != '<' )
				{
					continue;
				}

				if ( block_ptr[ j + 1 ] == 's' && block_ptr[ j + 2 ] == 'v' && block_ptr[ j + 3 ] == 'g' )
				{
					for ( auto k = block_size; k > j + 5; --k )
					{
						if ( block_ptr[ k - 1 ] == '>' && block_ptr[ k - 2 ] == 'g' && block_ptr[ k - 3 ] == 'v' && block_ptr[ k - 4 ] == 's' )
						{
							return std::vector<std::byte>( data.begin( ) + data_start + j, data.begin( ) + data_start + k );
						}
					}
				}
			}

			break;
		}

		return {};
	}

} // namespace systems
