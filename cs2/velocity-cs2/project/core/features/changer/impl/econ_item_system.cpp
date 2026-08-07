#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <utilities/threadpool/threadpool.hpp>
#include <utilities/game_path.hpp>
#include "../changer.hpp"

namespace features::changer {

	bool econ_item_system::initialize( )
	{
		std::uintptr_t schema{};
		auto schema_ready{ false };
		constexpr auto max_attempts{ 300 };
		constexpr auto retry_delay{ std::chrono::milliseconds( 100 ) };

		// Early injection can precede the schema's item-definition and paint-kit
		// tables. Wait until both are populated, then parse exactly once.
		for ( auto attempt = 0; attempt < max_attempts; ++attempt )
		{
			const auto system = memory::call<std::uintptr_t>( addresses::globals::item_system );
			schema = system
				? memory::safe_read<std::uintptr_t>( system + 0x8 ).value_or( 0 )
				: 0;

			if ( schema )
			{
				const auto item_count = memory::safe_read<int>( schema + 0x128 ).value_or( 0 );
				const auto item_array = memory::safe_read<std::uintptr_t>( schema + 0x130 ).value_or( 0 );
				const auto paint_count = memory::safe_read<int>( schema + 0x2F0 ).value_or( 0 );
				const auto paint_nodes = memory::safe_read<std::uintptr_t>( schema + 0x2F8 ).value_or( 0 );

				if ( item_count > 0 && item_count <= 10000 && item_array
					&& paint_count > 0 && paint_count <= 10000 && paint_nodes )
				{
					schema_ready = true;
					break;
				}
			}

			if ( attempt == 0 )
			{
				logging::console::print( xs( "[econ] waiting for item schema" ) );
			}

			std::this_thread::sleep_for( retry_delay );
		}

		if ( !schema_ready )
		{
			logging::console::print( xs( "[econ] timed out waiting for item schema" ) );
			return false;
		}

		if ( !this->parse_item_defs( schema ) )
		{
			logging::console::print( xs( "[econ] failed to parse item definitions" ) );
			return false;
		}

		if ( !this->parse_paint_kits( schema ) )
		{
			logging::console::print( xs( "[econ] failed to parse paint kits" ) );
			return false;
		}

		this->build_indices( );
		this->resolve_localized_names( );

		if ( !this->build_vpk_index( ) )
		{
			logging::console::print( xs( "[econ] failed to build VPK index" ) );
			return false;
		}

		this->build_skin_index( );

		return true;
	}

	const econ_item_system::item_def* econ_item_system::find_def( std::int16_t def_index ) const
	{
		const auto it = this->m_def_index_map.find( def_index );
		if ( it == this->m_def_index_map.end( ) )
		{
			return nullptr;
		}

		return &this->m_item_defs[ it->second ];
	}

	const econ_item_system::paint_kit* econ_item_system::find_paint_kit( int id ) const
	{
		const auto it = this->m_paint_kit_map.find( id );
		if ( it == this->m_paint_kit_map.end( ) )
		{
			return nullptr;
		}

		return &this->m_paint_kits[ it->second ];
	}

	const econ_item_system::skin_image* econ_item_system::get_skin_image( const std::string& image_inventory )
	{
		if ( image_inventory.empty( ) )
		{
			return nullptr;
		}

		std::lock_guard lock( this->m_image_mutex );

		auto it = this->m_image_cache.find( image_inventory );
		if ( it == this->m_image_cache.end( ) )
		{
			auto entry = std::make_unique<image_entry>( );
			it = this->m_image_cache.emplace( image_inventory, std::move( entry ) ).first;
			this->request_decode( image_inventory );
			return nullptr;
		}

		const auto state = it->second->state.load( std::memory_order_acquire );
		if ( state == image_state::ready )
		{
			return &it->second->image;
		}

		if ( state == image_state::decoded )
		{
			if ( this->finalize_texture( *it->second ) )
			{
				return &it->second->image;
			}
		}

		return nullptr;
	}

	const econ_item_system::skin_image* econ_item_system::get_skin_image( std::int16_t def_index, int paint_kit_id )
	{
		const auto def = this->find_def( def_index );
		if ( !def )
		{
			return nullptr;
		}

		const auto pk = ( paint_kit_id != 0 ) ? this->find_paint_kit( paint_kit_id ) : nullptr;
		const auto path = this->build_skin_image_path( def, pk );

		return this->get_skin_image( path );
	}

	int econ_item_system::combined_rarity( std::int16_t def_index, int paint_kit_id ) const
	{
		auto weapon_rarity{ 0 };
		auto paint_rarity{ 0 };
		auto category{ item_category::other };

		if ( const auto def = this->find_def( def_index ) )
		{
			weapon_rarity = def->rarity;
			category = def->category;
		}

		if ( const auto pk = this->find_paint_kit( paint_kit_id ) )
		{
			paint_rarity = pk->rarity;
		}

		if ( category == item_category::knife )
		{
			return 5;
		}

		return std::clamp( weapon_rarity + paint_rarity - 2, 0, 7 );
	}

	void econ_item_system::flush_skin_images( )
	{
		{
			std::lock_guard lock( this->m_image_mutex );
			this->m_image_cache.clear( );
		}

		{
			std::lock_guard lock( this->m_vpk_mutex );
			this->m_archive_handles.clear( );
		}
	}

	bool econ_item_system::parse_item_defs( std::uintptr_t schema )
	{
		const auto count = memory::read<int>( schema + 0x128 );
		const auto array = memory::read<std::uintptr_t>( schema + 0x130 );

		if ( !array || count <= 0 || count > 10000 )
		{
			return false;
		}

		this->m_item_defs.reserve( count );

		for ( auto i = 0; i < count; i++ )
		{
			const auto entry_base = array + static_cast< std::uintptr_t >( 32 * i );
			const auto def_index = memory::read<int>( entry_base + 16 );

			if ( def_index < -1 )
			{
				continue;
			}

			const auto def_ptr = memory::read<std::uintptr_t>( entry_base + 24 );
			if ( !def_ptr )
			{
				continue;
			}

			item_def item{};
			item.def_index = memory::read<std::int16_t>( def_ptr + 0x10 );
			item.loadout_slot = memory::read<int>( def_ptr + 0x338 );
			item.used_by_classes = memory::read<std::uint32_t>( def_ptr + 0x368 );
			item.rarity = memory::read<std::uint8_t>( def_ptr + 0x42 );

			if ( const auto name_ptr = memory::read<std::uintptr_t>( def_ptr + 0x260 ); name_ptr )
			{
				item.name = memory::read_string( name_ptr );
				item.item_class = item.name;
			}

			if ( const auto model_ptr = memory::read<std::uintptr_t>( def_ptr + 0x148 ); model_ptr )
			{
				item.model_player = memory::read_string( model_ptr );
			}

			if ( const auto img_ptr = memory::read<std::uintptr_t>( def_ptr + 0xA8 ); img_ptr )
			{
				item.image_inventory = memory::read_string( img_ptr );
			}

			if ( const auto token_ptr = memory::read<std::uintptr_t>( def_ptr + 0x70 ); token_ptr )
			{
				const auto token = memory::read_string( token_ptr );
				const auto localized = memory::call_vfunc<const char*>( addresses::globals::localize, 17, token.c_str( ) );

				if ( localized && *localized && std::strcmp( localized, token.c_str( ) ) != 0 )
				{
					item.localized_name = localized;
				}
				else
				{
					item.localized_name = item.name;
				}
			}
			else
			{
				item.localized_name = item.name;
			}

			item.category = this->classify( item.item_class.c_str( ), item.loadout_slot );
			this->m_item_defs.push_back( std::move( item ) );
		}

		return !this->m_item_defs.empty( );
	}

	bool econ_item_system::parse_paint_kits( std::uintptr_t schema )
	{
		const auto tree_base = schema + 0x2F0;

		const auto count = memory::read<int>( tree_base + 0x00 );
		const auto nodes = memory::read<std::uintptr_t>( tree_base + 0x08 );

		if ( !nodes || count <= 0 || count > 10000 )
		{
			return false;
		}

		this->m_paint_kits.reserve( count );

		for ( auto i = 0; i < count; i++ )
		{
			const auto node_base = nodes + static_cast< std::uintptr_t >( 32 * i );
			const auto pk_ptr = memory::read<std::uintptr_t>( node_base + 24 );

			if ( !pk_ptr )
			{
				continue;
			}

			paint_kit pk{};
			pk.id = memory::read<int>( pk_ptr + 0x00 );
			pk.wear_min = memory::read<float>( pk_ptr + 0x6C );
			pk.wear_max = memory::read<float>( pk_ptr + 0x70 );
			pk.legacy_model = memory::read<bool>( pk_ptr + 0xAE );
			pk.rarity = static_cast< std::uint8_t >( memory::read<int>( pk_ptr + 0x44 ) & 0xFF );

			if ( const auto name_ptr = memory::read<std::uintptr_t>( pk_ptr + 0x08 ); name_ptr )
			{
				pk.name = memory::read_string( name_ptr );
			}

			if ( const auto desc_ptr = memory::read<std::uintptr_t>( pk_ptr + 0x10 ); desc_ptr )
			{
				pk.desc_token = memory::read_string( desc_ptr );
			}

			if ( const auto name_token_ptr = memory::read<std::uintptr_t>( pk_ptr + 0x18 ); name_token_ptr )
			{
				pk.name_token = memory::read_string( name_token_ptr );
			}

			this->m_paint_kits.push_back( std::move( pk ) );
		}

		return !this->m_paint_kits.empty( );
	}

	void econ_item_system::build_indices( )
	{
		for ( auto i = 0ull; i < this->m_item_defs.size( ); i++ )
		{
			const auto& def = this->m_item_defs[ i ];
			this->m_def_index_map[ def.def_index ] = i;

			switch ( def.category )
			{
			case item_category::knife:
				this->m_knives.push_back( &def );
				break;
			case item_category::glove:
				this->m_gloves.push_back( &def );
				break;
			case item_category::agent:
				this->m_agents.push_back( &def );
				break;
			case item_category::gun:
				this->m_guns.push_back( &def );
				break;
			default:
				break;
			}
		}

		for ( auto i = 0ull; i < this->m_paint_kits.size( ); i++ )
		{
			this->m_paint_kit_map[ this->m_paint_kits[ i ].id ] = i;
		}
	}

	void econ_item_system::resolve_localized_names( )
	{
		auto resolved{ 0 };
		auto fallback{ 0 };

		for ( auto& pk : this->m_paint_kits )
		{
			if ( !pk.name_token.empty( ) )
			{
				const auto localized = memory::call_vfunc<const char*>( addresses::globals::localize, 17, pk.name_token.c_str( ) );
				if ( localized && *localized && std::strcmp( localized, pk.name_token.c_str( ) ) != 0 )
				{
					pk.localized_name = localized;
					resolved++;
					continue;
				}
			}

			pk.localized_name = pk.name;
			fallback++;
		}
	}

	bool econ_item_system::build_vpk_index( )
	{
		if ( this->m_vpk_indexed )
		{
			return !this->m_vpk_index.empty( );
		}

		this->m_vpk_indexed = true;

		const auto csgo_directory = game_path::csgo_directory( );
		if ( !csgo_directory )
		{
			return false;
		}

		std::ifstream file( *csgo_directory / L"pak01_dir.vpk", std::ios::binary );

		if ( !file.is_open( ) )
		{
			return false;
		}

		this->m_vpk_directory = *csgo_directory;

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

		if ( header.signature != 0x55AA1234 || header.version != 2 )
		{
			return false;
		}

		const auto tree_end = static_cast< std::streamoff >( sizeof( vpk_header ) ) + static_cast< std::streamoff >( header.tree_size );

		while ( file.tellg( ) < tree_end )
		{
			std::string extension;
			std::getline( file, extension, '\0' );

			if ( extension.empty( ) )
			{
				break;
			}

			const auto is_vtex = extension == xs( "vtex_c" );

			while ( true )
			{
				std::string dir_path;
				std::getline( file, dir_path, '\0' );

				if ( dir_path.empty( ) )
				{
					break;
				}

				const auto is_econ = is_vtex && dir_path.find( xs( "panorama/images/econ" ) ) != std::string::npos;

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

					if ( !is_econ )
					{
						continue;
					}

					constexpr auto prefix_len = std::string_view( "panorama/images/" ).size( );
					auto key = dir_path.substr( prefix_len ) + "/" + filename;

					this->m_vpk_index[ std::move( key ) ] = vpk_file_entry
					{
						entry.archive_index,
						entry.entry_offset,
						entry.entry_length
					};
				}
			}
		}

		return !this->m_vpk_index.empty( );
	}

	void econ_item_system::build_skin_index( )
	{
		std::unordered_map<std::string, int> pk_by_name;
		pk_by_name.reserve( this->m_paint_kits.size( ) );

		for ( const auto& pk : this->m_paint_kits )
		{
			pk_by_name.emplace( pk.name, pk.id );
		}

		std::unordered_map<std::string, std::int16_t> def_by_name;
		def_by_name.reserve( this->m_item_defs.size( ) );

		for ( const auto& d : this->m_item_defs )
		{
			if ( !d.name.empty( ) )
			{
				def_by_name.emplace( d.name, d.def_index );
			}
		}

		constexpr std::string_view prefix{ "econ/default_generated/" };
		constexpr std::string_view suffix{ "_light_png" };

		for ( const auto& [path, _] : this->m_vpk_index )
		{
			if ( !path.starts_with( prefix ) || !path.ends_with( suffix ) )
			{
				continue;
			}

			std::string_view stem( path );
			stem.remove_prefix( prefix.size( ) );
			stem.remove_suffix( suffix.size( ) );

			std::int16_t def_idx{ -1 };
			std::string_view pk_name;

			for ( auto i = stem.find( '_', 1 ); i != std::string_view::npos; i = stem.find( '_', i + 1 ) )
			{
				const auto candidate = std::string( stem.substr( 0, i ) );
				const auto it = def_by_name.find( candidate );

				if ( it == def_by_name.end( ) )
				{
					continue;
				}

				def_idx = it->second;
				pk_name = stem.substr( i + 1 );
			}

			if ( def_idx == -1 || pk_name.empty( ) )
			{
				continue;
			}

			const auto pk_it = pk_by_name.find( std::string( pk_name ) );
			if ( pk_it == pk_by_name.end( ) )
			{
				continue;
			}

			this->m_skins.push_back( { def_idx, pk_it->second } );
		}
	}

	void econ_item_system::request_decode( const std::string& image_inventory )
	{
		const auto key = image_inventory + xs( "_png" );

		std::vector<std::byte> data;
		{
			std::lock_guard lock( this->m_vpk_mutex );
			data = this->read_vpk( key );
		}

		if ( data.empty( ) )
		{
			this->m_image_cache[ image_inventory ]->state.store( image_state::failed, std::memory_order_release );
			return;
		}

		this->m_image_cache[ image_inventory ]->state.store( image_state::loading, std::memory_order_release );

		threadpool::run( [ this, inv = image_inventory, buf = std::move( data ) ]( )
			{
				std::lock_guard lock( this->m_image_mutex );

				const auto it = this->m_image_cache.find( inv );
				if ( it == this->m_image_cache.end( ) )
				{
					return;
				}

				if ( this->decode_vtex( std::span<const std::byte>( buf.data( ), buf.size( ) ), *it->second ) )
				{
					it->second->state.store( image_state::decoded, std::memory_order_release );
				}
				else
				{
					it->second->state.store( image_state::failed, std::memory_order_release );
				}
			} );
	}

	bool econ_item_system::finalize_texture( image_entry& entry )
	{
		const auto device = xdraw::device( );
		if ( !device )
		{
			entry.state.store( image_state::failed, std::memory_order_release );
			return false;
		}

		if ( entry.mip_buffers.empty( ) )
		{
			entry.state.store( image_state::failed, std::memory_order_release );
			return false;
		}

		auto upload_format = entry.format;
		std::vector<std::uint8_t> rgba_pixels;

		if ( entry.format == DXGI_FORMAT_BC7_UNORM )
		{
			rgba_pixels.resize( static_cast< std::size_t >( entry.width ) * entry.height * 4 );
			bc7::decode_image( entry.mip_buffers[ 0 ].data( ), rgba_pixels.data( ), static_cast< int >( entry.width ), static_cast< int >( entry.height ) );
			upload_format = DXGI_FORMAT_R8G8B8A8_UNORM;
		}

		const auto upload_data = upload_format == DXGI_FORMAT_R8G8B8A8_UNORM && !rgba_pixels.empty( ) ? rgba_pixels.data( ) : entry.mip_buffers[ 0 ].data( );
		const auto upload_pitch = entry.width * 4;

		D3D11_TEXTURE2D_DESC td{};
		td.Width = entry.width;
		td.Height = entry.height;
		td.MipLevels = 0;
		td.ArraySize = 1;
		td.Format = upload_format;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

		Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
		if ( FAILED( device->CreateTexture2D( &td, nullptr, &tex ) ) )
		{
			entry.state.store( image_state::failed, std::memory_order_release );
			return false;
		}

		Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
		device->GetImmediateContext( &ctx );

		ctx->UpdateSubresource( tex.Get( ), 0, nullptr, upload_data, upload_pitch, 0 );

		D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
		sv.Format = upload_format;
		sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		sv.Texture2D.MipLevels = static_cast< UINT >( -1 );

		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
		if ( FAILED( device->CreateShaderResourceView( tex.Get( ), &sv, &srv ) ) )
		{
			entry.state.store( image_state::failed, std::memory_order_release );
			return false;
		}

		ctx->GenerateMips( srv.Get( ) );

		entry.image.srv = std::move( srv );
		entry.image.width = static_cast< int >( entry.width );
		entry.image.height = static_cast< int >( entry.height );
		entry.mip_buffers.clear( );
		entry.mip_buffers.shrink_to_fit( );
		entry.state.store( image_state::ready, std::memory_order_release );

		return true;
	}

	econ_item_system::item_category econ_item_system::classify( const char* item_class, int loadout_slot )
	{
		if ( loadout_slot == 38 )
		{
			return item_category::agent;
		}

		if ( loadout_slot == 41 )
		{
			return item_category::glove;
		}

		if ( std::strncmp( item_class, xs( "weapon_knife" ), 12 ) == 0 )
		{
			return item_category::knife;
		}

		if ( std::strncmp( item_class, xs( "weapon_" ), 7 ) == 0 )
		{
			if (
				std::strcmp( item_class, xs( "weapon_flashbang" ) ) == 0 ||
				std::strcmp( item_class, xs( "weapon_hegrenade" ) ) == 0 ||
				std::strcmp( item_class, xs( "weapon_smokegrenade" ) ) == 0 ||
				std::strcmp( item_class, xs( "weapon_molotov" ) ) == 0 ||
				std::strcmp( item_class, xs( "weapon_decoy" ) ) == 0 ||
				std::strcmp( item_class, xs( "weapon_incgrenade" ) ) == 0 ||
				std::strcmp( item_class, xs( "weapon_c4" ) ) == 0 ||
				std::strcmp( item_class, xs( "weapon_healthshot" ) ) == 0 ||
				std::strcmp( item_class, xs( "weapon_taser" ) ) == 0 ||
				std::strcmp( item_class, xs( "weapon_knifegg" ) ) == 0
				)
			{
				return item_category::other;
			}

			return item_category::gun;
		}

		return item_category::other;
	}

	std::vector<std::byte> econ_item_system::read_vpk( const std::string& path )
	{
		const auto it = this->m_vpk_index.find( path );
		if ( it == this->m_vpk_index.end( ) )
		{
			return {};
		}

		const auto& entry = it->second;
		auto& stream = this->m_archive_handles[ entry.archive_index ];

		if ( !stream.is_open( ) )
		{
			char archive_name[ 32 ];
			std::snprintf( archive_name, sizeof( archive_name ), xs( "pak01_%03d.vpk" ), entry.archive_index );

			stream.open( this->m_vpk_directory / archive_name, std::ios::binary );
			if ( !stream.is_open( ) )
			{
				return {};
			}
		}

		stream.seekg( entry.offset );

		std::vector<std::byte> data( entry.length );
		stream.read( reinterpret_cast< char* >( data.data( ) ), entry.length );

		return data;
	}

	bool econ_item_system::decode_vtex( std::span<const std::byte> data, image_entry& out )
	{
		const auto raw = reinterpret_cast< const std::uint8_t* >( data.data( ) );
		const auto size = data.size( );

		if ( size < 28 )
		{
			return false;
		}

		const auto file_size = *reinterpret_cast< const std::uint32_t* >( raw + 0x00 );
		const auto header_version = *reinterpret_cast< const std::uint16_t* >( raw + 0x04 );
		const auto block_count = *reinterpret_cast< const std::uint32_t* >( raw + 0x0C );

		if ( header_version != 12 || block_count == 0 || block_count > 64 )
		{
			return false;
		}

		constexpr auto block_header_size{ 16u };
		constexpr auto block_entry_size{ 12u };
		constexpr auto data_fourcc{ 'D' | ( 'A' << 8 ) | ( 'T' << 16 ) | ( 'A' << 24 ) };

		const std::uint8_t* data_block{ nullptr };
		auto data_block_offset{ 0ull };

		for ( auto i = 0u; i < block_count; i++ )
		{
			const auto entry_pos = block_header_size + i * block_entry_size;
			if ( static_cast< std::size_t >( entry_pos ) + block_entry_size > size )
			{
				break;
			}

			const auto type = *reinterpret_cast< const std::uint32_t* >( raw + entry_pos );
			const auto offset = *reinterpret_cast< const std::uint32_t* >( raw + entry_pos + 4 );

			if ( type != data_fourcc )
			{
				continue;
			}

			const auto data_start = entry_pos + 4 + offset;
			if ( static_cast< std::size_t >( data_start ) + 0x28 > size )
			{
				return false;
			}

			data_block = raw + data_start;
			data_block_offset = data_start;
			break;
		}

		if ( !data_block )
		{
			return false;
		}

		const auto width = static_cast< std::uint32_t >( *reinterpret_cast< const std::uint16_t* >( data_block + 0x14 ) );
		const auto height = static_cast< std::uint32_t >( *reinterpret_cast< const std::uint16_t* >( data_block + 0x16 ) );
		const auto format = *reinterpret_cast< const std::uint8_t* >( data_block + 0x1A );
		const auto mip_count = static_cast< std::uint32_t >( *reinterpret_cast< const std::uint8_t* >( data_block + 0x1B ) );
		const auto extra_data_offset = *reinterpret_cast< const std::uint32_t* >( data_block + 0x20 );
		const auto extra_data_count = *reinterpret_cast< const std::uint32_t* >( data_block + 0x24 );

		if ( width == 0 || height == 0 || mip_count == 0 )
		{
			return false;
		}

		auto dxgi_format{ DXGI_FORMAT_UNKNOWN };
		auto block_bytes{ 0u };
		auto bytes_per_pixel{ 0u };

		switch ( format )
		{
		case 1:
			dxgi_format = DXGI_FORMAT_BC1_UNORM;
			block_bytes = 8;
			break;
		case 2:
			dxgi_format = DXGI_FORMAT_BC3_UNORM;
			block_bytes = 16;
			break;
		case 4:
			dxgi_format = DXGI_FORMAT_R8G8B8A8_UNORM;
			bytes_per_pixel = 4;
			break;
		case 19:
			dxgi_format = DXGI_FORMAT_BC6H_UF16;
			block_bytes = 16;
			break;
		case 20:
			dxgi_format = DXGI_FORMAT_BC7_UNORM;
			block_bytes = 16;
			break;
		case 27:
			dxgi_format = DXGI_FORMAT_BC4_UNORM;
			block_bytes = 8;
			break;
		case 28:
			dxgi_format = DXGI_FORMAT_B8G8R8A8_UNORM;
			bytes_per_pixel = 4;
			break;
		default:
			return false;
		}

		auto calc_mip_size = [ & ]( std::uint32_t w, std::uint32_t h ) -> std::uint32_t
			{
				if ( block_bytes > 0 )
				{
					const auto bw = std::max( 4u, ( w + 3u ) & ~3u );
					const auto bh = std::max( 4u, ( h + 3u ) & ~3u );
					return ( bw / 4 ) * ( bh / 4 ) * block_bytes;
				}

				return w * h * bytes_per_pixel;
			};

		constexpr auto extra_compressed_mip_size{ 4u };
		auto is_compressed{ false };
		const std::uint32_t* compressed_sizes{ nullptr };
		auto compressed_sizes_count{ 0u };

		if ( extra_data_count > 0 )
		{
			const auto table_pos = static_cast< std::size_t >( 0x20 ) + extra_data_offset;

			for ( auto i = 0u; i < extra_data_count; i++ )
			{
				const auto entry_pos = table_pos + static_cast< std::size_t >( i ) * 12;
				if ( data_block_offset + entry_pos + 12 > size )
				{
					return false;
				}

				const auto etype = *reinterpret_cast< const std::uint32_t* >( data_block + entry_pos );
				const auto eoff = *reinterpret_cast< const std::uint32_t* >( data_block + entry_pos + 4 );
				const auto esize = *reinterpret_cast< const std::uint32_t* >( data_block + entry_pos + 8 );

				if ( etype != extra_compressed_mip_size )
				{
					continue;
				}

				const auto body_pos = entry_pos + 4 + eoff;
				if ( data_block_offset + body_pos + 12 > size || esize < 12 )
				{
					return false;
				}

				const auto int1 = *reinterpret_cast< const std::uint32_t* >( data_block + body_pos );
				const auto mips_offset = *reinterpret_cast< const std::uint32_t* >( data_block + body_pos + 4 );
				const auto mips_count_in_table = *reinterpret_cast< const std::uint32_t* >( data_block + body_pos + 8 );

				if ( int1 > 1 || mips_count_in_table != mip_count )
				{
					return false;
				}

				const auto array_pos = body_pos + 4 + mips_offset;
				if ( data_block_offset + array_pos + mips_count_in_table * 4u > size )
				{
					return false;
				}

				is_compressed = ( int1 == 1 );
				compressed_sizes = reinterpret_cast< const std::uint32_t* >( data_block + array_pos );
				compressed_sizes_count = mips_count_in_table;
				break;
			}
		}

		const auto pixel_start = static_cast< std::size_t >( file_size );
		if ( pixel_start >= size )
		{
			return false;
		}

		auto on_disk_size_for = [ & ]( std::uint32_t mip_level ) -> std::uint32_t
			{
				const auto mw = std::max( 1u, width >> mip_level );
				const auto mh = std::max( 1u, height >> mip_level );
				const auto uncompressed = calc_mip_size( mw, mh );

				if ( !is_compressed || compressed_sizes == nullptr || mip_level >= compressed_sizes_count )
				{
					return uncompressed;
				}

				const auto compressed = compressed_sizes[ mip_level ];
				return ( compressed >= uncompressed ) ? uncompressed : compressed;
			};

		std::vector<std::vector<std::uint8_t>> mip_buffers( 1 );
		auto cursor = pixel_start;

		for ( auto j = mip_count; j-- > 0u; )
		{
			const auto on_disk = on_disk_size_for( j );

			if ( cursor + on_disk > size )
			{
				return false;
			}

			if ( j == 0 )
			{
				const auto uncompressed = calc_mip_size( width, height );
				mip_buffers[ 0 ].resize( uncompressed );

				if ( !is_compressed || on_disk >= uncompressed )
				{
					if ( on_disk != uncompressed )
					{
						return false;
					}

					std::memcpy( mip_buffers[ 0 ].data( ), raw + cursor, uncompressed );
				}
				else
				{
					const auto decoded = LZ4_decompress_safe( reinterpret_cast< const char* >( raw + cursor ), reinterpret_cast< char* >( mip_buffers[ 0 ].data( ) ), static_cast< int >( on_disk ), static_cast< int >( uncompressed ) );
					if ( decoded != static_cast< int >( uncompressed ) )
					{
						return false;
					}
				}
			}

			cursor += on_disk;
		}

		out.mip_buffers = std::move( mip_buffers );
		out.width = width;
		out.height = height;
		out.format = dxgi_format;

		return true;
	}

	std::string econ_item_system::build_skin_image_path( const item_def* def, const paint_kit* pk ) const
	{
		if ( !pk || pk->id == 0 )
		{
			return def->image_inventory;
		}

		std::string_view base = def->image_inventory;
		if ( base.empty( ) )
		{
			base = def->name;
		}

		auto slash = base.find_last_of( '/' );
		std::string_view stem = ( slash == std::string_view::npos ) ? base : base.substr( slash + 1 );

		return std::string( xs( "econ/default_generated/" ) ) + std::string( stem ) + "_" + pk->name + xs( "_light" );
	}

} // namespace features::changer
