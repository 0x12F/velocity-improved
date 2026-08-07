#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/steam/steam.hpp>
#include <core/rendering/rendering.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>

namespace features::esp::other {

	namespace detail {

		struct avatar_cache
		{
			struct entry
			{
				Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texture{};
				bool attempted{};
			};

			std::unordered_map<std::uintptr_t, entry> m_entries{};

			[[nodiscard]] ID3D11ShaderResourceView* get( std::uintptr_t steam_id )
			{
				auto it = this->m_entries.find( steam_id );
				if ( it != this->m_entries.end( ) )
				{
					return it->second.texture.Get( );
				}

				auto& e = this->m_entries[ steam_id ];
				e.attempted = true;

				const auto image_handle = steam::friends::get_medium_friend_avatar( steam_id );
				if ( image_handle <= 0 )
				{
					return nullptr;
				}

				std::uint32_t w{}, h{};
				if ( !steam::utils::get_image_size( image_handle, &w, &h ) || !w || !h )
				{
					return nullptr;
				}

				std::vector<std::uint8_t> rgba( w * h * 4 );
				if ( !steam::utils::get_image_rgba( image_handle, rgba.data( ), static_cast< int >( rgba.size( ) ) ) )
				{
					return nullptr;
				}

				e.texture = xdraw::create_srv_from_rgba( rgba.data( ), static_cast< int >( w ), static_cast< int >( h ) );
				return e.texture.Get( );
			}

			void clear( )
			{
				this->m_entries.clear( );
			}
		};

		constexpr unsigned char spectator_icon[ 1030 ]
		{
			0x3C, 0x73, 0x76, 0x67, 0x20, 0x77, 0x69, 0x64, 0x74, 0x68, 0x3D, 0x22,
			0x31, 0x32, 0x22, 0x20, 0x68, 0x65, 0x69, 0x67, 0x68, 0x74, 0x3D, 0x22,
			0x31, 0x32, 0x22, 0x20, 0x76, 0x69, 0x65, 0x77, 0x42, 0x6F, 0x78, 0x3D,
			0x22, 0x30, 0x20, 0x30, 0x20, 0x31, 0x32, 0x20, 0x31, 0x32, 0x22, 0x20,
			0x66, 0x69, 0x6C, 0x6C, 0x3D, 0x22, 0x6E, 0x6F, 0x6E, 0x65, 0x22, 0x20,
			0x78, 0x6D, 0x6C, 0x6E, 0x73, 0x3D, 0x22, 0x68, 0x74, 0x74, 0x70, 0x3A,
			0x2F, 0x2F, 0x77, 0x77, 0x77, 0x2E, 0x77, 0x33, 0x2E, 0x6F, 0x72, 0x67,
			0x2F, 0x32, 0x30, 0x30, 0x30, 0x2F, 0x73, 0x76, 0x67, 0x22, 0x3E, 0x0D,
			0x0A, 0x3C, 0x67, 0x20, 0x63, 0x6C, 0x69, 0x70, 0x2D, 0x70, 0x61, 0x74,
			0x68, 0x3D, 0x22, 0x75, 0x72, 0x6C, 0x28, 0x23, 0x63, 0x6C, 0x69, 0x70,
			0x30, 0x5F, 0x31, 0x35, 0x35, 0x5F, 0x32, 0x32, 0x33, 0x29, 0x22, 0x3E,
			0x0D, 0x0A, 0x3C, 0x70, 0x61, 0x74, 0x68, 0x20, 0x64, 0x3D, 0x22, 0x4D,
			0x35, 0x20, 0x36, 0x43, 0x35, 0x20, 0x36, 0x2E, 0x32, 0x36, 0x35, 0x32,
			0x32, 0x20, 0x35, 0x2E, 0x31, 0x30, 0x35, 0x33, 0x36, 0x20, 0x36, 0x2E,
			0x35, 0x31, 0x39, 0x35, 0x37, 0x20, 0x35, 0x2E, 0x32, 0x39, 0x32, 0x38,
			0x39, 0x20, 0x36, 0x2E, 0x37, 0x30, 0x37, 0x31, 0x31, 0x43, 0x35, 0x2E,
			0x34, 0x38, 0x30, 0x34, 0x33, 0x20, 0x36, 0x2E, 0x38, 0x39, 0x34, 0x36,
			0x34, 0x20, 0x35, 0x2E, 0x37, 0x33, 0x34, 0x37, 0x38, 0x20, 0x37, 0x20,
			0x36, 0x20, 0x37, 0x43, 0x36, 0x2E, 0x32, 0x36, 0x35, 0x32, 0x32, 0x20,
			0x37, 0x20, 0x36, 0x2E, 0x35, 0x31, 0x39, 0x35, 0x37, 0x20, 0x36, 0x2E,
			0x38, 0x39, 0x34, 0x36, 0x34, 0x20, 0x36, 0x2E, 0x37, 0x30, 0x37, 0x31,
			0x31, 0x20, 0x36, 0x2E, 0x37, 0x30, 0x37, 0x31, 0x31, 0x43, 0x36, 0x2E,
			0x38, 0x39, 0x34, 0x36, 0x34, 0x20, 0x36, 0x2E, 0x35, 0x31, 0x39, 0x35,
			0x37, 0x20, 0x37, 0x20, 0x36, 0x2E, 0x32, 0x36, 0x35, 0x32, 0x32, 0x20,
			0x37, 0x20, 0x36, 0x43, 0x37, 0x20, 0x35, 0x2E, 0x37, 0x33, 0x34, 0x37,
			0x38, 0x20, 0x36, 0x2E, 0x38, 0x39, 0x34, 0x36, 0x34, 0x20, 0x35, 0x2E,
			0x34, 0x38, 0x30, 0x34, 0x33, 0x20, 0x36, 0x2E, 0x37, 0x30, 0x37, 0x31,
			0x31, 0x20, 0x35, 0x2E, 0x32, 0x39, 0x32, 0x38, 0x39, 0x43, 0x36, 0x2E,
			0x35, 0x31, 0x39, 0x35, 0x37, 0x20, 0x35, 0x2E, 0x31, 0x30, 0x35, 0x33,
			0x36, 0x20, 0x36, 0x2E, 0x32, 0x36, 0x35, 0x32, 0x32, 0x20, 0x35, 0x20,
			0x36, 0x20, 0x35, 0x43, 0x35, 0x2E, 0x37, 0x33, 0x34, 0x37, 0x38, 0x20,
			0x35, 0x20, 0x35, 0x2E, 0x34, 0x38, 0x30, 0x34, 0x33, 0x20, 0x35, 0x2E,
			0x31, 0x30, 0x35, 0x33, 0x36, 0x20, 0x35, 0x2E, 0x32, 0x39, 0x32, 0x38,
			0x39, 0x20, 0x35, 0x2E, 0x32, 0x39, 0x32, 0x38, 0x39, 0x43, 0x35, 0x2E,
			0x31, 0x30, 0x35, 0x33, 0x36, 0x20, 0x35, 0x2E, 0x34, 0x38, 0x30, 0x34,
			0x33, 0x20, 0x35, 0x20, 0x35, 0x2E, 0x37, 0x33, 0x34, 0x37, 0x38, 0x20,
			0x35, 0x20, 0x36, 0x5A, 0x22, 0x20, 0x73, 0x74, 0x72, 0x6F, 0x6B, 0x65,
			0x3D, 0x22, 0x23, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x22, 0x20, 0x73,
			0x74, 0x72, 0x6F, 0x6B, 0x65, 0x2D, 0x6C, 0x69, 0x6E, 0x65, 0x63, 0x61,
			0x70, 0x3D, 0x22, 0x72, 0x6F, 0x75, 0x6E, 0x64, 0x22, 0x20, 0x73, 0x74,
			0x72, 0x6F, 0x6B, 0x65, 0x2D, 0x6C, 0x69, 0x6E, 0x65, 0x6A, 0x6F, 0x69,
			0x6E, 0x3D, 0x22, 0x72, 0x6F, 0x75, 0x6E, 0x64, 0x22, 0x2F, 0x3E, 0x0D,
			0x0A, 0x3C, 0x70, 0x61, 0x74, 0x68, 0x20, 0x64, 0x3D, 0x22, 0x4D, 0x37,
			0x2E, 0x35, 0x31, 0x35, 0x20, 0x38, 0x2E, 0x37, 0x33, 0x39, 0x43, 0x37,
			0x2E, 0x30, 0x32, 0x39, 0x32, 0x34, 0x20, 0x38, 0x2E, 0x39, 0x31, 0x34,
			0x32, 0x36, 0x20, 0x36, 0x2E, 0x35, 0x31, 0x36, 0x34, 0x31, 0x20, 0x39,
			0x2E, 0x30, 0x30, 0x32, 0x36, 0x31, 0x20, 0x36, 0x20, 0x39, 0x43, 0x34,
			0x2E, 0x32, 0x20, 0x39, 0x20, 0x32, 0x2E, 0x37, 0x20, 0x38, 0x20, 0x31,
			0x2E, 0x35, 0x20, 0x36, 0x43, 0x32, 0x2E, 0x37, 0x20, 0x34, 0x20, 0x34,
			0x2E, 0x32, 0x20, 0x33, 0x20, 0x36, 0x20, 0x33, 0x43, 0x37, 0x2E, 0x38,
			0x20, 0x33, 0x20, 0x39, 0x2E, 0x33, 0x20, 0x34, 0x20, 0x31, 0x30, 0x2E,
			0x35, 0x20, 0x36, 0x43, 0x31, 0x30, 0x2E, 0x34, 0x35, 0x37, 0x38, 0x20,
			0x36, 0x2E, 0x30, 0x37, 0x30, 0x33, 0x35, 0x20, 0x31, 0x30, 0x2E, 0x34,
			0x31, 0x34, 0x38, 0x20, 0x36, 0x2E, 0x31, 0x34, 0x30, 0x31, 0x39, 0x20,
			0x31, 0x30, 0x2E, 0x33, 0x37, 0x31, 0x20, 0x36, 0x2E, 0x32, 0x30, 0x39,
			0x35, 0x22, 0x20, 0x73, 0x74, 0x72, 0x6F, 0x6B, 0x65, 0x3D, 0x22, 0x23,
			0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x22, 0x20, 0x73, 0x74, 0x72, 0x6F,
			0x6B, 0x65, 0x2D, 0x6C, 0x69, 0x6E, 0x65, 0x63, 0x61, 0x70, 0x3D, 0x22,
			0x72, 0x6F, 0x75, 0x6E, 0x64, 0x22, 0x20, 0x73, 0x74, 0x72, 0x6F, 0x6B,
			0x65, 0x2D, 0x6C, 0x69, 0x6E, 0x65, 0x6A, 0x6F, 0x69, 0x6E, 0x3D, 0x22,
			0x72, 0x6F, 0x75, 0x6E, 0x64, 0x22, 0x2F, 0x3E, 0x0D, 0x0A, 0x3C, 0x70,
			0x61, 0x74, 0x68, 0x20, 0x64, 0x3D, 0x22, 0x4D, 0x39, 0x2E, 0x35, 0x20,
			0x38, 0x56, 0x39, 0x2E, 0x35, 0x22, 0x20, 0x73, 0x74, 0x72, 0x6F, 0x6B,
			0x65, 0x3D, 0x22, 0x23, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x22, 0x20,
			0x73, 0x74, 0x72, 0x6F, 0x6B, 0x65, 0x2D, 0x6C, 0x69, 0x6E, 0x65, 0x63,
			0x61, 0x70, 0x3D, 0x22, 0x72, 0x6F, 0x75, 0x6E, 0x64, 0x22, 0x20, 0x73,
			0x74, 0x72, 0x6F, 0x6B, 0x65, 0x2D, 0x6C, 0x69, 0x6E, 0x65, 0x6A, 0x6F,
			0x69, 0x6E, 0x3D, 0x22, 0x72, 0x6F, 0x75, 0x6E, 0x64, 0x22, 0x2F, 0x3E,
			0x0D, 0x0A, 0x3C, 0x70, 0x61, 0x74, 0x68, 0x20, 0x64, 0x3D, 0x22, 0x4D,
			0x39, 0x2E, 0x35, 0x20, 0x31, 0x31, 0x56, 0x31, 0x31, 0x2E, 0x30, 0x30,
			0x35, 0x22, 0x20, 0x73, 0x74, 0x72, 0x6F, 0x6B, 0x65, 0x3D, 0x22, 0x23,
			0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x22, 0x20, 0x73, 0x74, 0x72, 0x6F,
			0x6B, 0x65, 0x2D, 0x6C, 0x69, 0x6E, 0x65, 0x63, 0x61, 0x70, 0x3D, 0x22,
			0x72, 0x6F, 0x75, 0x6E, 0x64, 0x22, 0x20, 0x73, 0x74, 0x72, 0x6F, 0x6B,
			0x65, 0x2D, 0x6C, 0x69, 0x6E, 0x65, 0x6A, 0x6F, 0x69, 0x6E, 0x3D, 0x22,
			0x72, 0x6F, 0x75, 0x6E, 0x64, 0x22, 0x2F, 0x3E, 0x0D, 0x0A, 0x3C, 0x2F,
			0x67, 0x3E, 0x0D, 0x0A, 0x3C, 0x64, 0x65, 0x66, 0x73, 0x3E, 0x0D, 0x0A,
			0x3C, 0x63, 0x6C, 0x69, 0x70, 0x50, 0x61, 0x74, 0x68, 0x20, 0x69, 0x64,
			0x3D, 0x22, 0x63, 0x6C, 0x69, 0x70, 0x30, 0x5F, 0x31, 0x35, 0x35, 0x5F,
			0x32, 0x32, 0x33, 0x22, 0x3E, 0x0D, 0x0A, 0x3C, 0x72, 0x65, 0x63, 0x74,
			0x20, 0x77, 0x69, 0x64, 0x74, 0x68, 0x3D, 0x22, 0x31, 0x32, 0x22, 0x20,
			0x68, 0x65, 0x69, 0x67, 0x68, 0x74, 0x3D, 0x22, 0x31, 0x32, 0x22, 0x20,
			0x66, 0x69, 0x6C, 0x6C, 0x3D, 0x22, 0x77, 0x68, 0x69, 0x74, 0x65, 0x22,
			0x2F, 0x3E, 0x0D, 0x0A, 0x3C, 0x2F, 0x63, 0x6C, 0x69, 0x70, 0x50, 0x61,
			0x74, 0x68, 0x3E, 0x0D, 0x0A, 0x3C, 0x2F, 0x64, 0x65, 0x66, 0x73, 0x3E,
			0x0D, 0x0A, 0x3C, 0x2F, 0x73, 0x76, 0x67, 0x3E, 0x0D, 0x0A
		};

	} // namespace detail

	void overlay::on_render( xdraw::draw_list& draw_list )
	{
		this->add_spectators( draw_list );
		this->add_bomb( draw_list );
	}

	void overlay::add_bomb( xdraw::draw_list& draw_list )
	{
		const auto local = systems::g_local.get( );
		if ( !local.is_valid( ) || !systems::g_entities.exists( local.view_controller( ) ) )
		{
			return;
		}

		const auto planted_c4 = memory::read<std::uintptr_t>( addresses::globals::planted_c4 );
		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );

		if ( !planted_c4 || !global_vars )
		{
			return;
		}

		const auto current_time = memory::read<float>( global_vars + 0x30 );
		const auto blow_time = memory::read<float>( planted_c4 + SCHEMA( "C_PlantedC4", "m_flC4Blow"_hash ) );
		const auto has_exploded = memory::read<bool>( planted_c4 + SCHEMA( "C_PlantedC4", "m_bHasExploded"_hash ) );
		const auto bomb_defused = memory::read<bool>( planted_c4 + SCHEMA( "C_PlantedC4", "m_bBombDefused"_hash ) );

		if ( bomb_defused )
		{
			return;
		}

		const auto time_remaining = blow_time - current_time;
		const auto is_exploding = has_exploded || time_remaining <= 0.0f;

		if ( is_exploding && time_remaining < -2.0f )
		{
			return;
		}

		const auto bomb_site = memory::read<int>( planted_c4 + SCHEMA( "C_PlantedC4", "m_nBombSite"_hash ) );
		const auto being_defused = memory::read<bool>( planted_c4 + SCHEMA( "C_PlantedC4", "m_bBeingDefused"_hash ) );
		const auto timer_length = memory::read<float>( planted_c4 + SCHEMA( "C_PlantedC4", "m_flTimerLength"_hash ) );

		const auto calculate_bomb_damage = [ & ]( ) -> float
			{
				const auto view_pawn = local.view_pawn( );
				if ( !view_pawn )
				{
					return 0.0f;
				}

				const auto c4_scene_node = memory::read<std::uintptr_t>( planted_c4 + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
				const auto pawn_scene_node = memory::read<std::uintptr_t>( view_pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );

				if ( !c4_scene_node || !pawn_scene_node )
				{
					return 0.0f;
				}

				const auto c4_origin = memory::read<math::vector3>( c4_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
				const auto pawn_origin = memory::read<math::vector3>( pawn_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );

				const auto distance = ( c4_origin - pawn_origin ).length( );

				constexpr auto default_damage{ 650.0f };
				constexpr auto default_radius{ 2275.0f };

				const auto sigma = default_radius / 3.0f;
				auto damage = default_damage * std::exp( -( distance * distance ) / ( 2.0f * sigma * sigma ) );

				const auto armor = memory::read<int>( view_pawn + SCHEMA( "C_CSPlayerPawn", "m_ArmorValue"_hash ) );

				if ( armor > 0 )
				{
					constexpr auto armor_ratio = 0.5f;
					constexpr auto armor_bonus = 0.5f;

					auto armor_absorbed = damage * armor_ratio;
					auto armor_cost = ( damage - armor_absorbed ) * armor_bonus;

					if ( armor_cost > static_cast< float >( armor ) )
					{
						armor_cost = static_cast< float >( armor ) * ( 1.0f / armor_bonus );
						armor_absorbed = damage - armor_cost;
					}

					damage = armor_absorbed;
				}

				return std::floor( damage );
			}( );

		const auto [screen_w, screen_h] = xdraw::viewport_size( );
		const auto& s = xui::ctx( ).style;

		constexpr auto h{ 24.0f };
		constexpr auto top_offset{ 175.0f };
		constexpr auto r{ 8.0f };
		constexpr auto inner_r{ 6.0f };
		constexpr auto inner_pad{ 2.0f };
		constexpr auto text_pad_x{ 8.0f };
		constexpr auto text_nudge{ 0.5f };
		constexpr auto section_spacing{ 2.0f };

		const auto inner_h = h - inner_pad * 2.0f;

		auto timer_color = [ & ]( ) -> xdraw::color
			{
				if ( is_exploding )
				{
					return { 255, 100, 100, 255 };
				}

				const auto frac = timer_length > 0.0f ? time_remaining / timer_length : 1.0f;

				if ( frac > 0.5f )
				{
					return s.accent;
				}
				else if ( frac > 0.2f )
				{
					const auto t = ( frac - 0.2f ) / 0.3f;

					return
					{
						static_cast< std::uint8_t >( 255 ),
						static_cast< std::uint8_t >( 200 + static_cast< int >( ( s.accent.g - 200 ) * t ) ),
						static_cast< std::uint8_t >( 140 + static_cast< int >( ( s.accent.b - 140 ) * t ) ),
						255
					};
				}
				else
				{
					const auto t = frac / 0.2f;

					return
					{
						255,
						static_cast< std::uint8_t >( 120 + static_cast< int >( 80 * t ) ),
						static_cast< std::uint8_t >( 100 + static_cast< int >( 40 * t ) ),
						255
					};
				}
			}( );

		const auto site_label = bomb_site == 0 ? "A plant" : "B plant";
		const auto [site_tw, site_th] = xdraw::measure_text( site_label );
		const auto site_pill_w = site_tw + text_pad_x * 2.0f;

		const auto damage = static_cast< int >( calculate_bomb_damage );
		const auto view_pawn = local.view_pawn( );
		const auto health = view_pawn ? memory::read<int>( view_pawn + SCHEMA( "C_BaseEntity", "m_iHealth"_hash ) ) : 0;
		const auto will_kill = health <= damage;

		char health_buf[ 16 ]{};
		std::snprintf( health_buf, sizeof( health_buf ), "%+d", -damage );

		const auto [health_vw, health_vh] = xdraw::measure_text( health_buf );
		const auto [health_uw, health_uh] = xdraw::measure_text( " health" );
		const auto health_pill_w = health_vw + health_uw + text_pad_x * 2.0f;
		const auto health_col = will_kill ? xdraw::color{ 255, 120, 120, 255 } : xdraw::color{ 160, 210, 140, 255 };

		char timer_buf[ 16 ]{};
		const char* timer_unit{};

		if ( is_exploding )
		{
			strncpy_s( timer_buf, sizeof( timer_buf ), "0.0s", _TRUNCATE );
			timer_unit = " exploding";
		}
		else
		{
			std::snprintf( timer_buf, sizeof( timer_buf ), "%.1fs", time_remaining );
			timer_unit = being_defused ? " defusing" : " explosion";
		}

		const auto [timer_vw, timer_vh] = xdraw::measure_text( timer_buf );
		const auto [timer_uw, timer_uh] = xdraw::measure_text( timer_unit );
		const auto timer_pill_w = timer_vw + timer_uw + text_pad_x * 2.0f;

		const auto total_w = inner_pad + site_pill_w + section_spacing + health_pill_w + section_spacing + timer_pill_w + inner_pad;
		const auto x = ( static_cast< float >( screen_w ) - total_w ) * 0.5f;
		const auto y = top_offset;

		draw_list.rect_filled_blurred( x, y, total_w, h, xdraw::corner_radius{ r } );
		draw_list.rect_filled( x, y, total_w, h, s.window_bg, xdraw::corner_radius{ r } );

		auto cx = x + inner_pad;

		draw_list.rect_filled( cx, y + inner_pad, site_pill_w, inner_h, s.child_bg, xdraw::corner_radius{ inner_r } );
		draw_list.text( cx + text_pad_x, y + ( h - site_th ) * 0.5f + text_nudge, site_label, s.accent );
		cx += site_pill_w + section_spacing;

		const auto health_unit_col = xdraw::color{ health_col.r, health_col.g, health_col.b, 120 };
		draw_list.rect_filled( cx, y + inner_pad, health_pill_w, inner_h, s.child_bg, xdraw::corner_radius{ inner_r } );
		draw_list.text( cx + text_pad_x, y + ( h - health_vh ) * 0.5f + text_nudge, health_buf, health_col );
		draw_list.text( cx + text_pad_x + health_vw, y + ( h - health_uh ) * 0.5f + text_nudge, " health", health_unit_col );
		cx += health_pill_w + section_spacing;

		const auto timer_unit_col = xdraw::color{ timer_color.r, timer_color.g, timer_color.b, 120 };
		draw_list.rect_filled( cx, y + inner_pad, timer_pill_w, inner_h, s.child_bg, xdraw::corner_radius{ inner_r } );
		draw_list.text( cx + text_pad_x, y + ( h - timer_vh ) * 0.5f + text_nudge, timer_buf, timer_color );
		draw_list.text( cx + text_pad_x + timer_vw, y + ( h - timer_uh ) * 0.5f + text_nudge, timer_unit, timer_unit_col );
	}

	void overlay::add_spectators( xdraw::draw_list& draw_list )
	{
		const auto local = systems::g_local.get( );
		if ( !local.is_valid( ) || !systems::g_entities.exists( local.view_controller( ) ) )
		{
			return;
		}

		const auto game_rules = memory::read<std::uintptr_t>( addresses::globals::game_rules );
		if ( !game_rules || memory::read<int>( game_rules + SCHEMA( "C_CSGameRules", "m_gamePhase"_hash ) ) >= 4 )
		{
			return;
		}

		const auto local_controller = local.controller;
		const auto view_controller = local.view_controller( );
		const auto view_pawn = local.view_pawn( );
		if ( !view_pawn )
		{
			return;
		}

		const auto [screen_w, screen_h] = xdraw::viewport_size( );
		const auto& s = xui::ctx( ).style;

		constexpr auto margin{ 10.0f };
		constexpr auto row_spacing{ 3.0f };
		constexpr auto row_h{ 24.0f };
		constexpr auto header_h{ 24.0f };
		constexpr auto r{ 8.0f };
		constexpr auto inner_r{ 6.0f };
		constexpr auto inner_pad{ 2.0f };
		constexpr auto text_pad_x{ 8.0f };
		constexpr auto text_nudge{ 0.5f };
		constexpr auto icon_size{ 20.0f };
		constexpr auto icon_inner_pad{ 4.0f };
		constexpr auto avatar_pill_size{ 20.0f };

		struct spectator_entry
		{
			char name[ 128 ];
			std::uintptr_t steam_id;
		};

		spectator_entry entries[ 32 ]{};
		auto count{ 0 };

		for ( const auto& player : systems::g_entities.get_by_type( systems::entities::type::player ) )
		{
			if ( player.ptr == view_controller || player.ptr == local_controller || count >= 32 )
			{
				continue;
			}

			if ( memory::read<bool>( player.ptr + SCHEMA( "CCSPlayerController", "m_bPawnIsAlive"_hash ) ) )
			{
				continue;
			}

			const auto obs_pawn_handle = memory::read<std::uint32_t>( player.ptr + SCHEMA( "CCSPlayerController", "m_hObserverPawn"_hash ) );
			if ( !obs_pawn_handle || obs_pawn_handle == 0xffffffff )
			{
				continue;
			}

			const auto obs_pawn = systems::g_entities.lookup( obs_pawn_handle );
			if ( !obs_pawn )
			{
				continue;
			}

			const auto observer_services = memory::safe_read<std::uintptr_t>( obs_pawn + SCHEMA( "C_BasePlayerPawn", "m_pObserverServices"_hash ) ).value_or( 0 );
			if ( !observer_services || ( observer_services >> 48 ) != 0 )
			{
				continue;
			}

			const auto observer_target_handle = memory::safe_read<std::uint32_t>( observer_services + SCHEMA( "CPlayer_ObserverServices", "m_hObserverTarget"_hash ) ).value_or( 0 );
			if ( !observer_target_handle )
			{
				continue;
			}

			const auto observer_target = systems::g_entities.lookup( observer_target_handle );
			if ( observer_target != view_pawn )
			{
				continue;
			}

			const auto name_ptr = memory::read<std::uintptr_t>( player.ptr + SCHEMA( "CCSPlayerController", "m_sSanitizedPlayerName"_hash ) );
			if ( !name_ptr )
			{
				continue;
			}

			auto name = memory::read_string( name_ptr, 127 );
			std::ranges::transform( name, name.begin( ), [ ]( unsigned char c ) { return std::tolower( c ); } );

			auto& e = entries[ count++ ];
			strncpy_s( e.name, name.c_str( ), sizeof( e.name ) - 1 );

			e.name[ sizeof( e.name ) - 1 ] = '\0';
			e.steam_id = memory::read<std::uintptr_t>( player.ptr + SCHEMA( "CBasePlayerController", "m_steamID"_hash ) );
		}

		if ( count <= 0 )
		{
			return;
		}

		static detail::avatar_cache avatars{};

		static auto icon_w_px = 0, icon_h_px = 0;
		static const auto eye_icon = xdraw::load_svg( std::span<const std::byte>( reinterpret_cast< const std::byte* >( detail::spectator_icon ), sizeof( detail::spectator_icon ) ), 1.0f, &icon_w_px, &icon_h_px );

		const auto [header_tw, header_th] = xdraw::measure_text( "spectators" );
		const auto inner_h = row_h - inner_pad * 2.0f;
		const auto header_inner_h = header_h - inner_pad * 2.0f;

		const auto header_w = inner_pad + header_tw + text_pad_x * 2.0f + inner_pad + icon_size + inner_pad;
		const auto x = static_cast< float >( screen_w ) - header_w - margin;
		auto ry = ( static_cast< float >( screen_h ) * 0.5f ) - ( ( header_h + row_spacing + static_cast< float >( count ) * ( row_h + row_spacing ) ) * 0.5f );

		draw_list.rect_filled_blurred( x, ry, header_w, header_h, xdraw::corner_radius{ r } );
		draw_list.rect_filled( x, ry, header_w, header_h, s.window_bg, xdraw::corner_radius{ r } );

		const auto htx = x + inner_pad;
		const auto htw = header_tw + text_pad_x * 2.0f;
		draw_list.rect_filled( htx, ry + inner_pad, htw, header_inner_h, s.child_bg, xdraw::corner_radius{ inner_r } );
		draw_list.text( htx + text_pad_x, ry + ( header_h - header_th ) * 0.5f + text_nudge, "spectators", s.accent );

		const auto icon_x = x + inner_pad + htw + inner_pad;
		draw_list.rect_filled( icon_x, ry + inner_pad, icon_size, header_inner_h, s.accent, xdraw::corner_radius{ inner_r } );

		if ( eye_icon )
		{
			const auto icon_draw = icon_size - icon_inner_pad * 2.0f;
			const auto ix = std::floor( icon_x + icon_inner_pad );
			const auto iy = std::floor( ry + inner_pad + ( header_inner_h - icon_draw ) * 0.5f + 1.0f );
			draw_list.image( ix, iy, icon_draw, icon_draw, eye_icon.Get( ), s.checkbox_mark_icon );
		}

		ry += header_h + row_spacing;

		for ( auto i = 0; i < count; ++i )
		{
			const auto& e = entries[ i ];
			const auto [nw, nh] = xdraw::measure_text( e.name );
			const auto avatar_tex = avatars.get( e.steam_id );
			const auto has_avatar = avatar_tex != nullptr;

			const auto name_pill_w = nw + text_pad_x * 2.0f;
			const auto row_w = inner_pad + name_pill_w + ( has_avatar ? inner_pad + avatar_pill_size : 0.0f ) + inner_pad;
			const auto rx = static_cast< float >( screen_w ) - row_w - margin;

			draw_list.rect_filled_blurred( rx, ry, row_w, row_h, xdraw::corner_radius{ r } );
			draw_list.rect_filled( rx, ry, row_w, row_h, s.window_bg, xdraw::corner_radius{ r } );

			auto cx = rx + inner_pad;

			draw_list.rect_filled( cx, ry + inner_pad, name_pill_w, inner_h, s.child_bg, xdraw::corner_radius{ inner_r } );
			draw_list.text( cx + text_pad_x, ry + ( row_h - nh ) * 0.5f + text_nudge, e.name, s.accent );
			cx += name_pill_w;

			if ( has_avatar )
			{
				cx += inner_pad;
				const auto avatar_draw = inner_h;
				const auto ay = ry + inner_pad;
				draw_list.image( cx, ay, avatar_draw, avatar_draw, avatar_tex, xdraw::corner_radius{ inner_r }, xdraw::color{ 255, 255, 255, 255 } );
			}

			ry += row_h + row_spacing;
		}
	}

} // namespace features::esp::other
