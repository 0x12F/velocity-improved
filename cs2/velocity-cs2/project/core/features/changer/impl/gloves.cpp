#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>

namespace features::changer {

	namespace detail {

		constexpr std::array<std::uint16_t, 3> glove_attribute_indices{ 6, 7, 8 };
		constexpr std::array<const char*, 3> glove_attribute_names
		{
			"set item texture prefab",
			"set item texture seed",
			"set item texture wear"
		};

		// Current C_EconItemView local-attribute vector layout, mirrored from Artisan.
		constexpr std::ptrdiff_t item_view_attribute_count_offset{ 0x210 };
		constexpr std::ptrdiff_t item_view_attribute_data_offset{ 0x218 };
		constexpr std::ptrdiff_t item_attribute_stride{ 0x48 };
		constexpr std::ptrdiff_t item_attribute_definition_offset{ 0x30 };
		constexpr std::ptrdiff_t item_attribute_value_offset{ 0x34 };
		constexpr auto maximum_attribute_count{ 16384 };
		constexpr std::size_t post_data_update_index{ 10 };
		constexpr std::uint64_t faux_item_id{ 0xf000000000000010ull };

		[[nodiscard]] std::uint32_t attribute_value_bits( float value )
		{
			std::uint32_t result{};
			static_assert( sizeof( result ) == sizeof( value ) );
			std::memcpy( &result, &value, sizeof( result ) );
			return result;
		}

	} // namespace detail

	void gloves::on_frame_stage_notify( )
	{
		const auto local = systems::g_local.get( );
		if ( !local.is_alive || !local.pawn || !local.controller )
		{
			this->reset( );
			return;
		}

		if ( systems::g_local.is_in_cinematic( ) )
		{
			return;
		}

		if ( local.team != 2 && local.team != 3 )
		{
			return;
		}

		if ( this->m_tracked_pawn != local.pawn )
		{
			this->reset( );
			this->m_tracked_pawn = local.pawn;
		}

		const settings::changer::applied_skin* selected_skin{ nullptr };
		const econ_item_system::item_def* selected_glove_def{ nullptr };

		for ( const auto& [def_index, skin] : settings::g_changer.skins.data )
		{
			const auto def = g_econ_item_system.find_def( def_index );
			if ( !def || def->category != econ_item_system::item_category::glove )
			{
				continue;
			}

			selected_skin = &skin;
			selected_glove_def = def;
			break;
		}

		const auto item_view = local.pawn + SCHEMA( "C_CSPlayerPawn", "m_EconGloves"_hash );
		if ( !selected_glove_def )
		{
			if ( this->m_overridden )
			{
				this->restore( local.pawn, item_view, local.team );
			}

			return;
		}

		if ( !this->m_original.captured && !this->capture_original( item_view ) )
		{
			return;
		}

		const auto current_def = memory::read<std::uint16_t>( item_view + SCHEMA( "C_EconItemView", "m_iItemDefinitionIndex"_hash ) );
		const auto current_id = memory::read<std::uint64_t>( item_view + SCHEMA( "C_EconItemView", "m_iItemID"_hash ) );
		const auto needs_reapply = memory::read<bool>( local.pawn + SCHEMA( "C_CSPlayerPawn", "m_bNeedToReApplyGloves"_hash ) );

		if ( current_def == static_cast< std::uint16_t >( selected_glove_def->def_index ) &&
			current_id == detail::faux_item_id &&
			this->paint_attributes_match( item_view, *selected_skin ) &&
			!needs_reapply )
		{
			return;
		}

		const auto steam_id = memory::read<std::uint64_t>( local.controller + SCHEMA( "CBasePlayerController", "m_steamID"_hash ) );
		this->apply( local.pawn, item_view, local.team, *selected_glove_def, *selected_skin, static_cast< std::uint32_t >( steam_id ) );
	}

	bool gloves::capture_original( std::uintptr_t item_view )
	{
		if ( !this->read_paint_attributes( item_view, this->m_original_attributes ) )
		{
			return false;
		}

		this->m_original.def_index = memory::read<std::uint16_t>( item_view + SCHEMA( "C_EconItemView", "m_iItemDefinitionIndex"_hash ) );
		this->m_original.item_id = memory::read<std::uint64_t>( item_view + SCHEMA( "C_EconItemView", "m_iItemID"_hash ) );
		this->m_original.id_high = memory::read<std::uint32_t>( item_view + SCHEMA( "C_EconItemView", "m_iItemIDHigh"_hash ) );
		this->m_original.id_low = memory::read<std::uint32_t>( item_view + SCHEMA( "C_EconItemView", "m_iItemIDLow"_hash ) );
		this->m_original.account_id = memory::read<std::uint32_t>( item_view + SCHEMA( "C_EconItemView", "m_iAccountID"_hash ) );
		this->m_original.restore_custom_material = memory::read<bool>( item_view + SCHEMA( "C_EconItemView", "m_bRestoreCustomMaterialAfterPrecache"_hash ) );
		this->m_original.initialized = memory::read<bool>( item_view + SCHEMA( "C_EconItemView", "m_bInitialized"_hash ) );
		this->m_original.disallow_soc = memory::read<bool>( item_view + SCHEMA( "C_EconItemView", "m_bDisallowSOC"_hash ) );
		this->m_original.captured = true;
		return true;
	}

	bool gloves::read_paint_attributes( std::uintptr_t item_view, std::array<attribute_state, 3>& attributes ) const
	{
		attributes = {};

		const auto count = memory::read<int>( item_view + detail::item_view_attribute_count_offset );
		if ( count < 0 || count > detail::maximum_attribute_count )
		{
			return false;
		}

		const auto data = memory::read<std::uintptr_t>( item_view + detail::item_view_attribute_data_offset );
		if ( count && !data )
		{
			return false;
		}

		for ( auto i = 0; i < count; ++i )
		{
			const auto attribute = data + static_cast< std::ptrdiff_t >( i ) * detail::item_attribute_stride;
			const auto definition = memory::read<std::uint16_t>( attribute + detail::item_attribute_definition_offset );

			for ( std::size_t slot = 0; slot < detail::glove_attribute_indices.size( ); ++slot )
			{
				if ( definition == detail::glove_attribute_indices[ slot ] && !attributes[ slot ].present )
				{
					attributes[ slot ].value = memory::read<float>( attribute + detail::item_attribute_value_offset );
					attributes[ slot ].present = true;
					break;
				}
			}
		}

		return true;
	}

	bool gloves::restore_paint_attributes( std::uintptr_t item_view ) const
	{
		const auto set_attribute = PATTERN( patterns::econ_item_view_set_attribute );
		const auto remove_attribute = PATTERN( patterns::econ_item_view_remove_attribute );
		if ( !set_attribute || !remove_attribute )
		{
			return false;
		}

		for ( std::size_t slot = 0; slot < detail::glove_attribute_indices.size( ); ++slot )
		{
			if ( this->m_original_attributes[ slot ].present )
			{
				memory::call<void>( set_attribute, item_view, detail::glove_attribute_names[ slot ], this->m_original_attributes[ slot ].value );
			}
			else
			{
				memory::call<void>( remove_attribute, item_view, static_cast< int >( detail::glove_attribute_indices[ slot ] ) );
			}
		}

		return true;
	}

	bool gloves::paint_attributes_match( std::uintptr_t item_view, const settings::changer::applied_skin& skin ) const
	{
		std::array<attribute_state, 3> attributes{};
		if ( !this->read_paint_attributes( item_view, attributes ) )
		{
			return false;
		}

		const std::array expected
		{
			static_cast< float >( skin.paint_kit_id ),
			static_cast< float >( skin.seed ),
			skin.wear
		};

		for ( std::size_t slot = 0; slot < expected.size( ); ++slot )
		{
			if ( !attributes[ slot ].present ||
				detail::attribute_value_bits( attributes[ slot ].value ) != detail::attribute_value_bits( expected[ slot ] ) )
			{
				return false;
			}
		}

		return true;
	}

	void gloves::apply( std::uintptr_t pawn, std::uintptr_t item_view, int team, const econ_item_system::item_def& def, const settings::changer::applied_skin& skin, std::uint32_t account_id )
	{
		const auto set_attribute = PATTERN( patterns::econ_item_view_set_attribute );
		if ( !set_attribute )
		{
			return;
		}

		memory::write<std::uint16_t>( item_view + SCHEMA( "C_EconItemView", "m_iItemDefinitionIndex"_hash ), static_cast< std::uint16_t >( def.def_index ) );
		memory::write<std::uint64_t>( item_view + SCHEMA( "C_EconItemView", "m_iItemID"_hash ), detail::faux_item_id );
		memory::write<std::uint32_t>( item_view + SCHEMA( "C_EconItemView", "m_iItemIDHigh"_hash ), static_cast< std::uint32_t >( detail::faux_item_id >> 32 ) );
		memory::write<std::uint32_t>( item_view + SCHEMA( "C_EconItemView", "m_iItemIDLow"_hash ), static_cast< std::uint32_t >( detail::faux_item_id ) );
		memory::write<std::uint32_t>( item_view + SCHEMA( "C_EconItemView", "m_iAccountID"_hash ), account_id );
		memory::write<bool>( item_view + SCHEMA( "C_EconItemView", "m_bRestoreCustomMaterialAfterPrecache"_hash ), true );
		memory::write<bool>( item_view + SCHEMA( "C_EconItemView", "m_bInitialized"_hash ), true );
		memory::write<bool>( item_view + SCHEMA( "C_EconItemView", "m_bDisallowSOC"_hash ), true );

		// Definitions 6/7/8 are semantic floats; bit-casting the paint kit makes the composite invalid.
		memory::call<void>( set_attribute, item_view, detail::glove_attribute_names[ 0 ], static_cast< float >( skin.paint_kit_id ) );
		memory::call<void>( set_attribute, item_view, detail::glove_attribute_names[ 1 ], static_cast< float >( skin.seed ) );
		memory::call<void>( set_attribute, item_view, detail::glove_attribute_names[ 2 ], skin.wear );

		this->m_overridden = true;
		this->refresh( pawn, item_view, team );
	}

	void gloves::restore( std::uintptr_t pawn, std::uintptr_t item_view, int team )
	{
		if ( !this->m_original.captured || !this->restore_paint_attributes( item_view ) )
		{
			return;
		}

		memory::write<std::uint16_t>( item_view + SCHEMA( "C_EconItemView", "m_iItemDefinitionIndex"_hash ), this->m_original.def_index );
		memory::write<std::uint64_t>( item_view + SCHEMA( "C_EconItemView", "m_iItemID"_hash ), this->m_original.item_id );
		memory::write<std::uint32_t>( item_view + SCHEMA( "C_EconItemView", "m_iItemIDHigh"_hash ), this->m_original.id_high );
		memory::write<std::uint32_t>( item_view + SCHEMA( "C_EconItemView", "m_iItemIDLow"_hash ), this->m_original.id_low );
		memory::write<std::uint32_t>( item_view + SCHEMA( "C_EconItemView", "m_iAccountID"_hash ), this->m_original.account_id );
		memory::write<bool>( item_view + SCHEMA( "C_EconItemView", "m_bRestoreCustomMaterialAfterPrecache"_hash ), this->m_original.restore_custom_material );
		memory::write<bool>( item_view + SCHEMA( "C_EconItemView", "m_bInitialized"_hash ), this->m_original.initialized );
		memory::write<bool>( item_view + SCHEMA( "C_EconItemView", "m_bDisallowSOC"_hash ), this->m_original.disallow_soc );

		this->refresh( pawn, item_view, team );
		this->m_original = {};
		this->m_original_attributes = {};
		this->m_overridden = false;
	}

	void gloves::refresh( std::uintptr_t pawn, std::uintptr_t item_view, int team ) const
	{
		const auto invalidate = PATTERN( patterns::econ_item_view_invalidate_description );
		if ( invalidate )
		{
			memory::call<void>( invalidate, item_view );
		}

		memory::write<bool>( pawn + SCHEMA( "C_CSPlayerPawn", "m_bNeedToReApplyGloves"_hash ), true );
		memory::call_vfunc<void>( pawn, detail::post_data_update_index, 1 );

		const auto set_bodygroup = PATTERN( patterns::set_bodygroup );
		if ( set_bodygroup )
		{
			memory::call<void>( set_bodygroup, pawn, 0, 1u );
			memory::call<void>( set_bodygroup, pawn, team == 2 ? 0 : 1, 1u );
		}
	}

	void gloves::reset( )
	{
		this->m_original = {};
		this->m_original_attributes = {};
		this->m_tracked_pawn = 0;
		this->m_overridden = false;
	}

} // namespace features::changer
