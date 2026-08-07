#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <core/rendering/rendering.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>

namespace features::esp::player {

	void overlay::on_render( xdraw::draw_list& draw_list )
	{
		if ( !settings::g_esp.m_player.m_overlay[ 0 ].enabled.value && !settings::g_esp.m_player.m_overlay[ 1 ].enabled.value )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.is_valid( ) || !systems::g_entities.exists( local.view_controller( ) ) )
		{
			return;
		}

		auto players = systems::g_entities.get_by_type( systems::entities::type::player );
		{
			const auto camera = systems::g_view.origin( );

			std::sort( players.begin( ), players.end( ), [ & ]( const systems::entities::cached& a, const systems::entities::cached& b )
				{
					const auto node_a = memory::read<std::uintptr_t>( a.ptr + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
					const auto node_b = memory::read<std::uintptr_t>( b.ptr + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );

					if ( !node_a || !node_b )
					{
						return !node_a && node_b;
					}

					const auto da = node_a ? ( memory::read<math::vector3>( node_a + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) ) - camera ).length_sqr( ) : 0.0f;
					const auto db = node_b ? ( memory::read<math::vector3>( node_b + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) ) - camera ).length_sqr( ) : 0.0f;

					return da > db;
				} );
		}

		for ( const auto& player : players )
		{
			const auto info = this->get_info( player, local );
			if ( !info.valid( ) )
			{
				continue;
			}

			const auto& cfg = settings::g_esp.m_player.m_overlay[ info.is_other_team ? 0 : 1 ];
			if ( !cfg.enabled.value )
			{
				continue;
			}

			if ( cfg.m_oof_arrow.enabled.value )
			{
				this->add_oof_arrow( draw_list, info, cfg.m_oof_arrow );
			}

			const auto bounds = systems::g_bounds.get( info.pawn );
			if ( !bounds.valid )
			{
				continue;
			}

			overlay::draw_offsets offsets{};

			if ( cfg.m_box.enabled.value )
			{
				this->add_box( draw_list, bounds, cfg.m_box, info.is_visible );
			}

			if ( cfg.m_skeleton.enabled.value )
			{
				this->add_skeleton( draw_list, info, cfg.m_skeleton, info.is_visible, local );
			}

			if ( cfg.m_health_bar.enabled.value )
			{
				this->add_health_bar( draw_list, bounds, info, cfg.m_health_bar, offsets );
			}

			if ( cfg.m_ammo_bar.enabled.value && info.weapon.valid( ) )
			{
				this->add_ammo_bar( draw_list, bounds, info, cfg.m_ammo_bar, offsets );
			}

			if ( cfg.m_name.enabled.value && !info.name.empty( ) )
			{
				this->add_name( draw_list, bounds, info, cfg.m_name, offsets );
			}

			if ( cfg.m_weapon.enabled.value && !info.weapon.name.empty( ) )
			{
				this->add_weapon( draw_list, bounds, info, cfg.m_weapon, offsets );
			}

			if ( cfg.m_info_flags.enabled.value )
			{
				this->add_flags( draw_list, bounds, info, cfg.m_info_flags, offsets );
			}
		}
	}

	void overlay::add_box( xdraw::draw_list& draw_list, const systems::bounds::data& bounds, const settings::esp::player::overlay::box& cfg, bool visible )
	{
		const auto& color = visible ? cfg.visible_color : cfg.occluded_color;

		const auto x = std::floorf( bounds.min.x );
		const auto y = std::floorf( bounds.min.y );
		const auto w = std::floorf( bounds.max.x - bounds.min.x );
		const auto h = std::floorf( bounds.max.y - bounds.min.y );

		if ( cfg.fill.value )
		{
			constexpr auto edge_alpha{ 0.5f };
			constexpr auto center_alpha{ 0.08f };
			constexpr auto center_brightness{ 0.4f };
			constexpr auto desaturation{ 0.7f };

			const auto r = color.value.r / 255.0f;
			const auto g = color.value.g / 255.0f;
			const auto b = color.value.b / 255.0f;
			const auto avg = ( r + g + b ) / 3.0f;

			const auto edge_r = r * desaturation + avg * ( 1.0f - desaturation );
			const auto edge_g = g * desaturation + avg * ( 1.0f - desaturation );
			const auto edge_b = b * desaturation + avg * ( 1.0f - desaturation );
			const auto edge_color = xdraw::color( static_cast< std::uint8_t >( edge_r * 255 ), static_cast< std::uint8_t >( edge_g * 255 ), static_cast< std::uint8_t >( edge_b * 255 ), static_cast< std::uint8_t >( 255 * edge_alpha ) );
			const auto center_color = xdraw::color( static_cast< std::uint8_t >( edge_r * 255 * center_brightness ), static_cast< std::uint8_t >( edge_g * 255 * center_brightness ), static_cast< std::uint8_t >( edge_b * 255 * center_brightness ), static_cast< std::uint8_t >( 255 * center_alpha ) );

			const auto mid_y = y + h * 0.5f;

			draw_list.rect_filled_gradient( x + 1, y + 1, w - 2, mid_y - y - 1, edge_color, edge_color, center_color, center_color );
			draw_list.rect_filled_gradient( x + 1, mid_y, w - 2, y + h - mid_y - 1, center_color, center_color, edge_color, edge_color );
		}

		if ( cfg.style == settings::esp::player::overlay::box::style_type::full )
		{
			auto draw_full_rect = [ & ]( float rx, float ry, float rw, float rh, const xdraw::color& col, float thickness )
				{
					const auto t = std::clamp( thickness, 0.0f, std::min( rw, rh ) * 0.5f );

					if ( t <= 0.0f )
					{
						return;
					}

					draw_list.ensure_cmd( nullptr );

					auto v = draw_list.emit_vtx( rx, ry, 0, 0, col );
					draw_list.emit_vtx( rx + rw, ry, 0, 0, col );
					draw_list.emit_vtx( rx + rw, ry + t, 0, 0, col );
					draw_list.emit_vtx( rx, ry + t, 0, 0, col );
					draw_list.emit_quad( v, v + 1, v + 2, v + 3 );

					v = draw_list.emit_vtx( rx, ry + rh - t, 0, 0, col );
					draw_list.emit_vtx( rx + rw, ry + rh - t, 0, 0, col );
					draw_list.emit_vtx( rx + rw, ry + rh, 0, 0, col );
					draw_list.emit_vtx( rx, ry + rh, 0, 0, col );
					draw_list.emit_quad( v, v + 1, v + 2, v + 3 );

					v = draw_list.emit_vtx( rx, ry + t, 0, 0, col );
					draw_list.emit_vtx( rx + t, ry + t, 0, 0, col );
					draw_list.emit_vtx( rx + t, ry + rh - t, 0, 0, col );
					draw_list.emit_vtx( rx, ry + rh - t, 0, 0, col );
					draw_list.emit_quad( v, v + 1, v + 2, v + 3 );

					v = draw_list.emit_vtx( rx + rw - t, ry + t, 0, 0, col );
					draw_list.emit_vtx( rx + rw, ry + t, 0, 0, col );
					draw_list.emit_vtx( rx + rw, ry + rh - t, 0, 0, col );
					draw_list.emit_vtx( rx + rw - t, ry + rh - t, 0, 0, col );
					draw_list.emit_quad( v, v + 1, v + 2, v + 3 );
				};

			if ( cfg.outline.value )
			{
				draw_full_rect( x - 1, y - 1, w + 2, h + 2, xdraw::color( 0, 0, 0, 180 ), 1.0f );
				draw_full_rect( x, y, w, h, xdraw::color( 0, 0, 0, 200 ), 2.0f );
			}

			draw_full_rect( x, y, w, h, color, 1.0f );
		}
		else
		{
			const auto corner = std::min( cfg.corner_length.value, std::min( w, h ) * 0.4f );

			auto draw_cornered_rect = [ & ]( float rx, float ry, float rw, float rh, const xdraw::color& col, float corner_len, float thickness )
				{
					const auto max_corner = std::min( rw, rh ) * 0.5f;
					const auto cl = std::min( corner_len, max_corner );
					const auto t = std::clamp( thickness, 0.0f, std::min( rw, rh ) * 0.5f );

					if ( t <= 0.0f )
					{
						return;
					}

					draw_list.ensure_cmd( nullptr );

					auto v = draw_list.emit_vtx( rx, ry, 0, 0, col );
					draw_list.emit_vtx( rx + cl, ry, 0, 0, col );
					draw_list.emit_vtx( rx + cl, ry + t, 0, 0, col );
					draw_list.emit_vtx( rx, ry + t, 0, 0, col );
					draw_list.emit_quad( v, v + 1, v + 2, v + 3 );

					v = draw_list.emit_vtx( rx, ry + t, 0, 0, col );
					draw_list.emit_vtx( rx + t, ry + t, 0, 0, col );
					draw_list.emit_vtx( rx + t, ry + cl, 0, 0, col );
					draw_list.emit_vtx( rx, ry + cl, 0, 0, col );
					draw_list.emit_quad( v, v + 1, v + 2, v + 3 );

					v = draw_list.emit_vtx( rx + rw - cl, ry, 0, 0, col );
					draw_list.emit_vtx( rx + rw, ry, 0, 0, col );
					draw_list.emit_vtx( rx + rw, ry + t, 0, 0, col );
					draw_list.emit_vtx( rx + rw - cl, ry + t, 0, 0, col );
					draw_list.emit_quad( v, v + 1, v + 2, v + 3 );

					v = draw_list.emit_vtx( rx + rw - t, ry + t, 0, 0, col );
					draw_list.emit_vtx( rx + rw, ry + t, 0, 0, col );
					draw_list.emit_vtx( rx + rw, ry + cl, 0, 0, col );
					draw_list.emit_vtx( rx + rw - t, ry + cl, 0, 0, col );
					draw_list.emit_quad( v, v + 1, v + 2, v + 3 );

					v = draw_list.emit_vtx( rx + rw - t, ry + rh - cl, 0, 0, col );
					draw_list.emit_vtx( rx + rw, ry + rh - cl, 0, 0, col );
					draw_list.emit_vtx( rx + rw, ry + rh - t, 0, 0, col );
					draw_list.emit_vtx( rx + rw - t, ry + rh - t, 0, 0, col );
					draw_list.emit_quad( v, v + 1, v + 2, v + 3 );

					v = draw_list.emit_vtx( rx + rw - cl, ry + rh - t, 0, 0, col );
					draw_list.emit_vtx( rx + rw, ry + rh - t, 0, 0, col );
					draw_list.emit_vtx( rx + rw, ry + rh, 0, 0, col );
					draw_list.emit_vtx( rx + rw - cl, ry + rh, 0, 0, col );
					draw_list.emit_quad( v, v + 1, v + 2, v + 3 );

					v = draw_list.emit_vtx( rx, ry + rh - cl, 0, 0, col );
					draw_list.emit_vtx( rx + t, ry + rh - cl, 0, 0, col );
					draw_list.emit_vtx( rx + t, ry + rh - t, 0, 0, col );
					draw_list.emit_vtx( rx, ry + rh - t, 0, 0, col );
					draw_list.emit_quad( v, v + 1, v + 2, v + 3 );

					v = draw_list.emit_vtx( rx, ry + rh - t, 0, 0, col );
					draw_list.emit_vtx( rx + cl, ry + rh - t, 0, 0, col );
					draw_list.emit_vtx( rx + cl, ry + rh, 0, 0, col );
					draw_list.emit_vtx( rx, ry + rh, 0, 0, col );
					draw_list.emit_quad( v, v + 1, v + 2, v + 3 );
				};

			if ( cfg.outline )
			{
				draw_cornered_rect( x - 1, y - 1, w + 2, h + 2, xdraw::color( 0, 0, 0, 180 ), corner + 1, 1.0f );
				draw_cornered_rect( x, y, w, h, xdraw::color( 0, 0, 0, 200 ), corner, 2.0f );
			}

			draw_cornered_rect( x, y, w, h, color, corner, 1.0f );
		}
	}

	void overlay::add_skeleton( xdraw::draw_list& draw_list, const info& info, const settings::esp::player::overlay::skeleton& cfg, bool visible, const systems::local::snapshot& local )
	{
		const auto& color = visible ? cfg.visible_color : cfg.occluded_color;

		constexpr auto none{ ~0u };
		constexpr std::array chains
		{
			std::array{ cstypes::bone_ids::head, cstypes::bone_ids::neck, cstypes::bone_ids::spine_4, cstypes::bone_ids::spine_3, cstypes::bone_ids::spine_2, cstypes::bone_ids::spine_1, cstypes::bone_ids::pelvis },
			std::array{ cstypes::bone_ids::left_hand, cstypes::bone_ids::left_elbow, cstypes::bone_ids::left_shoulder, cstypes::bone_ids::left_clavicle, cstypes::bone_ids::spine_4, none, none },
			std::array{ cstypes::bone_ids::right_hand, cstypes::bone_ids::right_elbow, cstypes::bone_ids::right_shoulder, cstypes::bone_ids::right_clavicle, cstypes::bone_ids::spine_4, none, none },
			std::array{ cstypes::bone_ids::left_foot, cstypes::bone_ids::left_knee, cstypes::bone_ids::left_hip, cstypes::bone_ids::pelvis, none, none, none },
			std::array{ cstypes::bone_ids::right_foot, cstypes::bone_ids::right_knee, cstypes::bone_ids::right_hip, cstypes::bone_ids::pelvis, none, none, none },
		};

		std::array<math::vector3, 27> positions{};
		auto valid_bones{ false };

		if ( cfg.type == settings::esp::player::overlay::skeleton::mode::backtrack && local.is_alive )
		{
			const auto record = combat::g_shared.lc( ).get_oldest_was_valid_visual( info.pawn );
			if ( record )
			{
				if ( record->origin.distance( info.origin ) >= 0.25f )
				{
					for ( auto i = 0; i < 27; ++i )
					{
						positions[ i ] = record->bones[ i ].position;
					}

					valid_bones = true;
				}
			}
		}

		if ( !valid_bones )
		{
			if ( cfg.type == settings::esp::player::overlay::skeleton::mode::backtrack )
			{
				return;
			}

			for ( auto i = 0ull; i < info.bones.size( ) && i < 27; ++i )
			{
				positions[ i ] = info.bones[ i ].position;
			}
		}

		constexpr auto step{ 0.08f };

		const auto flush_segment = [ & ]( std::vector<math::vector2>& screen_points )
			{
				if ( screen_points.size( ) < 2 )
				{
					screen_points.clear( );
					return;
				}

				screen_points.insert( screen_points.begin( ), screen_points.front( ) );
				screen_points.push_back( screen_points.back( ) );

				std::vector<float> spline_points;
				spline_points.reserve( ( screen_points.size( ) * static_cast< std::size_t >( 1.f / step ) + 1 ) * 2 );

				for ( auto i = 0ull; i + 3 < screen_points.size( ); ++i )
				{
					const auto& p0 = screen_points[ i ];
					const auto& p1 = screen_points[ i + 1 ];
					const auto& p2 = screen_points[ i + 2 ];
					const auto& p3 = screen_points[ i + 3 ];

					for ( float t = 0.f; t <= 1.f; t += step )
					{
						const auto t2 = t * t;
						const auto t3 = t2 * t;

						spline_points.push_back( 0.5f * ( ( 2.f * p1.x ) + ( -p0.x + p2.x ) * t + ( 2.f * p0.x - 5.f * p1.x + 4.f * p2.x - p3.x ) * t2 + ( -p0.x + 3.f * p1.x - 3.f * p2.x + p3.x ) * t3 ) );
						spline_points.push_back( 0.5f * ( ( 2.f * p1.y ) + ( -p0.y + p2.y ) * t + ( 2.f * p0.y - 5.f * p1.y + 4.f * p2.y - p3.y ) * t2 + ( -p0.y + 3.f * p1.y - 3.f * p2.y + p3.y ) * t3 ) );
					}
				}

				if ( spline_points.size( ) >= 4 )
				{
					draw_list.polyline( spline_points, color, false, cfg.thickness );
				}

				screen_points.clear( );
			};

		for ( const auto& chain : chains )
		{
			std::vector<math::vector2> screen_points;
			screen_points.reserve( chain.size( ) );

			for ( const auto& b : chain )
			{
				if ( b == none )
				{
					break;
				}

				const auto idx = static_cast< std::size_t >( b );
				if ( idx >= 28 || positions[ idx ].length_sqr( ) < 1.0f )
				{
					flush_segment( screen_points );
					continue;
				}

				const auto projected = systems::g_view.project( positions[ idx ] );
				if ( !systems::g_view.projection_valid( projected ) )
				{
					flush_segment( screen_points );
					continue;
				}

				math::vector2 pt{ projected.x, projected.y };

				if ( !screen_points.empty( ) )
				{
					const auto& prev = screen_points.back( );
					const auto dx = pt.x - prev.x;
					const auto dy = pt.y - prev.y;

					if ( dx * dx + dy * dy > 250000.0f )
					{
						flush_segment( screen_points );
					}
				}

				screen_points.push_back( pt );
			}

			flush_segment( screen_points );
		}
	}

	void overlay::add_health_bar( xdraw::draw_list& draw_list, const systems::bounds::data& bounds, const info& info, const settings::esp::player::overlay::health_bar& cfg, draw_offsets& offsets )
	{
		auto& anim = this->m_animations[ info.controller ];

		constexpr auto bar_size = 3.5f, padding = 4.0f;
		const auto clamped_health = std::clamp( info.health, 0, 100 );
		const auto target_fraction = clamped_health / 100.0f;

		if ( !anim.initialized || ( target_fraction - anim.health.value( ) > 0.5f ) )
		{
			anim.health.snap( target_fraction );
			anim.initialized = true;
		}
		else
		{
			anim.health.set_target( target_fraction );
			anim.health.update( );
		}

		const auto fraction = anim.health.value( );
		const auto outline_size = cfg.outline_setting.value ? 1.0f : 0.0f;
		const auto vertical = cfg.position == settings::esp::player::overlay::health_bar::position_type::left;

		const auto bar_w = vertical ? bar_size : std::floorf( bounds.width( ) );
		const auto bar_h = vertical ? std::floorf( bounds.height( ) ) : bar_size;
		const auto filled = ( clamped_health >= 100 ) ? ( vertical ? bar_h : bar_w ) : std::floorf( ( vertical ? bar_h : bar_w ) * fraction );

		const auto x = [ & ]( )
			{
				if ( cfg.position == settings::esp::player::overlay::health_bar::position_type::left )
				{
					return std::floorf( bounds.min.x - bar_size - padding - offsets.left - outline_size );
				}

				return std::floorf( bounds.min.x );
			}( );

		const auto y = [ & ]( )
			{
				switch ( cfg.position )
				{
				case settings::esp::player::overlay::health_bar::position_type::left: return std::floorf( bounds.min.y );
				case settings::esp::player::overlay::health_bar::position_type::top: return std::floorf( bounds.min.y - bar_size - padding - offsets.top - outline_size );
				case settings::esp::player::overlay::health_bar::position_type::bottom: return std::floorf( bounds.max.y + padding + offsets.bottom + outline_size );
				}
				return 0.0f;
			}( );

		switch ( cfg.position )
		{
		case settings::esp::player::overlay::health_bar::position_type::left: offsets.left += bar_size + padding + ( outline_size * 2.0f );
			break;
		case settings::esp::player::overlay::health_bar::position_type::top: offsets.top += bar_size + padding + ( outline_size * 2.0f );
			break;
		case settings::esp::player::overlay::health_bar::position_type::bottom: offsets.bottom += bar_size + padding + ( outline_size * 2.0f );
			break;
		}

		if ( cfg.glow )
		{
			auto& glow = xdraw::get_glow( );
			const auto gc = cfg.glow_color.value;
			const auto glow_a = static_cast< std::uint8_t >( static_cast< float >( gc.a ) * cfg.glow_strength );
			const auto glow_col = xdraw::color{ gc.r, gc.g, gc.b, glow_a };

			for ( auto t = 0; t < 3; ++t )
			{
				const auto expand = static_cast< float >( t ) * 0.75f;
				glow.rect_filled( x - expand, y - expand, bar_w + expand * 2.0f, bar_h + expand * 2.0f, glow_col );
			}
		}

		if ( cfg.outline_setting.value )
		{
			draw_list.rect_filled( x - 1, y - 1, bar_w + 2, bar_h + 2, cfg.outline_color );
		}

		draw_list.rect_filled( x, y, bar_w, bar_h, cfg.background_color );

		if ( filled > 0 )
		{
			if ( cfg.gradient )
			{
				if ( vertical )
				{
					draw_list.rect_filled_gradient( x, y + bar_h - filled, bar_w, filled, cfg.full_color, cfg.full_color, cfg.low_color, cfg.low_color );
				}
				else
				{
					draw_list.rect_filled_gradient( x, y, filled, bar_h, cfg.low_color, cfg.full_color, cfg.full_color, cfg.low_color );
				}
			}
			else
			{
				if ( vertical )
				{
					draw_list.rect_filled( x, y + bar_h - filled, bar_w, filled, cfg.full_color );
				}
				else
				{
					draw_list.rect_filled( x, y, filled, bar_h, cfg.full_color );
				}
			}
		}

		if ( cfg.show_value && clamped_health < 100 )
		{
			xdraw::push_font( rendering::g_fonts.smallest_pixel7[ rendering::fonts::size::normal ] );

			const auto text = std::to_string( clamped_health );
			const auto [text_w, text_h] = xdraw::measure_text( text );
			const auto text_x = std::floorf( x + ( bar_w * 0.5f ) - ( text_w * 0.5f ) );
			const auto text_y = vertical ? std::floorf( y + bar_h - filled - text_h - 2.0f ) : std::floorf( y - text_h - 2.0f );

			draw_list.text( text_x, text_y, text, cfg.text_color, xdraw::text_style::outlined );
			xdraw::pop_font( );
		}
	}

	void overlay::add_ammo_bar( xdraw::draw_list& draw_list, const systems::bounds::data& bounds, const info& info, const settings::esp::player::overlay::ammo_bar& cfg, draw_offsets& offsets )
	{
		if ( info.weapon.max_ammo <= 0 )
		{
			return;
		}

		auto& anim = this->m_animations[ info.controller ];

		constexpr auto bar_size = 3.5f, padding = 4.0f;
		const auto clamped_ammo = std::clamp( info.weapon.ammo, 0, info.weapon.max_ammo );
		const auto target_fraction = static_cast< float >( clamped_ammo ) / info.weapon.max_ammo;

		if ( !anim.initialized || ( target_fraction - anim.ammo.value( ) > 0.5f ) )
		{
			anim.ammo.snap( target_fraction );
			anim.initialized = true;
		}
		else
		{
			anim.ammo.set_target( target_fraction );
			anim.ammo.update( );
		}

		const auto fraction = anim.ammo.value( );
		const auto outline_size = cfg.outline_setting.value ? 1.0f : 0.0f;
		const auto vertical = cfg.position == settings::esp::player::overlay::ammo_bar::position_type::left;

		const auto bar_w = vertical ? bar_size : std::floorf( bounds.width( ) );
		const auto bar_h = vertical ? std::floorf( bounds.height( ) ) : bar_size;
		const auto filled = ( clamped_ammo == info.weapon.max_ammo ) ? ( vertical ? bar_h : bar_w ) : std::floorf( ( vertical ? bar_h : bar_w ) * fraction );

		const auto x = [ & ]( )
			{
				if ( cfg.position == settings::esp::player::overlay::ammo_bar::position_type::left )
				{
					return std::floorf( bounds.min.x - bar_size - padding - offsets.left - outline_size );
				}

				return std::floorf( bounds.min.x );
			}( );

		const auto y = [ & ]( )
			{
				switch ( cfg.position )
				{
				case settings::esp::player::overlay::ammo_bar::position_type::left: return std::floorf( bounds.min.y );
				case settings::esp::player::overlay::ammo_bar::position_type::top: return std::floorf( bounds.min.y - bar_size - padding - offsets.top - outline_size );
				case settings::esp::player::overlay::ammo_bar::position_type::bottom: return std::floorf( bounds.max.y + padding + offsets.bottom + outline_size );
				}
				return 0.0f;
			}( );

		switch ( cfg.position )
		{
		case settings::esp::player::overlay::ammo_bar::position_type::left:
			offsets.left += bar_size + padding + ( outline_size * 2.0f );
			break;
		case settings::esp::player::overlay::ammo_bar::position_type::top:
			offsets.top += bar_size + padding + ( outline_size * 2.0f );
			break;
		case settings::esp::player::overlay::ammo_bar::position_type::bottom:
			offsets.bottom += bar_size + padding + ( outline_size * 2.0f );
			break;
		}

		if ( cfg.glow )
		{
			auto& glow = xdraw::get_glow( );
			const auto gc = cfg.glow_color.value;
			const auto glow_a = static_cast< std::uint8_t >( static_cast< float >( gc.a ) * cfg.glow_strength );
			const auto glow_col = xdraw::color{ gc.r, gc.g, gc.b, glow_a };

			for ( auto t = 0; t < 3; ++t )
			{
				const auto expand = static_cast< float >( t ) * 0.75f;
				glow.rect_filled( x - expand, y - expand, bar_w + expand * 2.0f, bar_h + expand * 2.0f, glow_col );
			}
		}

		if ( cfg.outline_setting.value )
		{
			draw_list.rect_filled( x - 1, y - 1, bar_w + 2, bar_h + 2, cfg.outline_color );
		}

		draw_list.rect_filled( x, y, bar_w, bar_h, cfg.background_color );

		if ( filled > 0 )
		{
			if ( cfg.gradient )
			{
				if ( vertical )
				{
					draw_list.rect_filled_gradient( x, y + bar_h - filled, bar_w, filled, cfg.full_color, cfg.full_color, cfg.low_color, cfg.low_color );
				}
				else
				{
					draw_list.rect_filled_gradient( x, y, filled, bar_h, cfg.low_color, cfg.full_color, cfg.full_color, cfg.low_color );
				}
			}
			else
			{
				if ( vertical )
				{
					draw_list.rect_filled( x, y + bar_h - filled, bar_w, filled, cfg.full_color );
				}
				else
				{
					draw_list.rect_filled( x, y, filled, bar_h, cfg.full_color );
				}
			}
		}

		if ( cfg.show_value )
		{
			xdraw::push_font( rendering::g_fonts.smallest_pixel7[ rendering::fonts::size::normal ] );

			const auto text = std::format( "{}/{}", clamped_ammo, info.weapon.max_ammo );
			const auto [text_w, text_h] = xdraw::measure_text( text );
			const auto text_x = std::floorf( x + ( bar_w * 0.5f ) - ( text_w * 0.5f ) );
			const auto text_y = vertical ? std::floorf( y + bar_h - filled - text_h - 2.0f ) : std::floorf( y + bar_h + 2.0f );

			draw_list.text( text_x, text_y, text, cfg.text_color, xdraw::text_style::outlined );
			xdraw::pop_font( );
		}
	}

	void overlay::add_name( xdraw::draw_list& draw_list, const systems::bounds::data& bounds, const info& info, const settings::esp::player::overlay::name& cfg, draw_offsets& offsets )
	{
		//xdraw::push_font( rendering::g_fonts.inter_medium[ rendering::fonts::size::normal ] );

		const auto [text_w, text_h] = xdraw::measure_text( info.name );
		const auto text_x = std::floorf( bounds.min.x + ( bounds.width( ) * 0.5f ) - ( text_w * 0.5f ) );
		const auto text_y = std::floorf( bounds.min.y - text_h - 2.0f - offsets.top );

		draw_list.text( text_x, text_y, info.name, cfg.color );
		offsets.top += text_h + 2.0f;

		//xdraw::pop_font( );
	}

	void overlay::add_weapon( xdraw::draw_list& draw_list, const systems::bounds::data& bounds, const info& info, const settings::esp::player::overlay::weapon& cfg, draw_offsets& offsets )
	{
		const auto show_icon = cfg.display == settings::esp::player::overlay::weapon::display_type::icon || cfg.display == settings::esp::player::overlay::weapon::display_type::text_and_icon;
		const auto show_text = cfg.display == settings::esp::player::overlay::weapon::display_type::text || cfg.display == settings::esp::player::overlay::weapon::display_type::text_and_icon;
		auto total_height{ 0.0f };

		if ( show_icon )
		{
			const auto icon_name = ( info.weapon.name == "knife_ct" || info.weapon.name == "knife_t" ) ? std::string{ "knife" } : info.weapon.name;
			const auto ico = systems::g_icons.get( icon_name, 0.35f );

			if ( ico && ico->texture )
			{
				const auto iw = static_cast< float >( ico->width );
				const auto ih = static_cast< float >( ico->height );
				const auto ix = std::floorf( bounds.min.x + ( bounds.width( ) * 0.5f ) - ( iw * 0.5f ) );
				const auto iy = std::floorf( bounds.max.y + 2.0f + offsets.bottom + total_height );
				constexpr auto outline = xdraw::color{ 0, 0, 0, 255 };

				draw_list.image( ix - 1.0f, iy, iw, ih, ico->texture.Get( ), outline );
				draw_list.image( ix + 1.0f, iy, iw, ih, ico->texture.Get( ), outline );
				draw_list.image( ix, iy - 1.0f, iw, ih, ico->texture.Get( ), outline );
				draw_list.image( ix, iy + 1.0f, iw, ih, ico->texture.Get( ), outline );
				draw_list.image( ix, iy, iw, ih, ico->texture.Get( ), cfg.icon_color );

				total_height += ih + 2.0f;
			}
		}

		if ( show_text )
		{
			xdraw::push_font( rendering::g_fonts.smallest_pixel7[ rendering::fonts::size::normal ] );

			const auto [text_w, text_h] = xdraw::measure_text( info.weapon.name );
			const auto text_x = std::floorf( bounds.min.x + ( bounds.width( ) * 0.5f ) - ( text_w * 0.5f ) );
			const auto text_y = std::floorf( bounds.max.y + 2.0f + offsets.bottom + total_height );

			draw_list.text( text_x, text_y, info.weapon.name, cfg.text_color, xdraw::text_style::outlined );
			xdraw::pop_font( );

			total_height += text_h + 2.0f;
		}

		offsets.bottom += total_height;
	}

	void overlay::add_flags( xdraw::draw_list& draw_list, const systems::bounds::data& bounds, const info& info, const settings::esp::player::overlay::info_flags& cfg, draw_offsets& offsets )
	{
		xdraw::push_font( rendering::g_fonts.smallest_pixel7[ rendering::fonts::size::normal ] );

		const auto x = std::floorf( bounds.max.x + 4.0f );
		auto y = std::floorf( bounds.min.y );

		const auto draw_flag = [ & ]( const std::string& text, const xdraw::color& color )
			{
				draw_list.text( x, y, text, color, xdraw::text_style::outlined );
				const auto [text_w, text_h] = xdraw::measure_text( text );
				y += text_h;
			};

		if ( cfg.has( settings::esp::player::overlay::info_flags::flag::money ) )
		{
			draw_flag( std::format( "${}", info.money ), cfg.money_color );
		}

		if ( cfg.has( settings::esp::player::overlay::info_flags::flag::armor ) && info.armor > 0 )
		{
			draw_flag( info.has_helmet ? "hk" : "k", cfg.armor_color );
		}

		if ( cfg.has( settings::esp::player::overlay::info_flags::flag::kit ) && info.has_defuser )
		{
			draw_flag( "kit", cfg.kit_color );
		}

		if ( cfg.has( settings::esp::player::overlay::info_flags::flag::scoped ) && info.is_scoped )
		{
			draw_flag( "zoom", cfg.scoped_color );
		}

		if ( cfg.has( settings::esp::player::overlay::info_flags::flag::defusing ) && info.is_defusing )
		{
			draw_flag( "defusing", cfg.defusing_color );
		}

		if ( cfg.has( settings::esp::player::overlay::info_flags::flag::flashed ) && info.is_flashed )
		{
			draw_flag( "flashed", cfg.flashed_color );
		}

		if ( cfg.has( settings::esp::player::overlay::info_flags::flag::ping ) )
		{
			const auto ping_color = [ & ]( )
				{
					if ( info.ping <= 20 )
					{
						return xdraw::color( 98, 217, 109, 255 );
					}

					if ( info.ping <= 50 )
					{
						return xdraw::color( 230, 206, 137, 255 );
					}

					if ( info.ping <= 70 )
					{
						return xdraw::color( 230, 156, 110, 255 );
					}

					return xdraw::color( 222, 59, 59, 255 );
				}( );

			draw_flag( std::format( "{}ms", info.ping ), ping_color );
		}

		if ( cfg.has( settings::esp::player::overlay::info_flags::flag::distance ) )
		{
			draw_flag( std::format( "{:.0f}m", info.distance ), cfg.distance_color );
		}

		xdraw::pop_font( );
	}

	void overlay::add_oof_arrow( xdraw::draw_list& draw_list, const info& info, const settings::esp::player::overlay::oof_arrow& cfg )
	{
		const auto [screen_w, screen_h] = xdraw::viewport_size( );
		const auto sw = static_cast< float >( screen_w );
		const auto sh = static_cast< float >( screen_h );
		const auto center_x = sw * 0.5f;
		const auto center_y = sh * 0.5f;
		const auto head = info.bones[ cstypes::bone_ids::head ].position;

		const auto proj = systems::g_view.project_full( head );

		if ( proj.on_screen && proj.w > 0.0f )
		{
			return;
		}

		if ( !systems::g_frame_data.valid( ) )
		{
			return;
		}

		const auto to_target = info.origin - systems::g_frame_data.origin( );
		if ( !std::isfinite( to_target.x ) || !std::isfinite( to_target.y ) || to_target.length_2d( ) < 1.0f )
		{
			return;
		}

		// Match the live input yaw and player origins used by the reference OOF implementation.
		// The view system's cached origin/angles describe a different render structure.
		const auto target_yaw = std::atan2f( to_target.y, to_target.x );
		const auto view_yaw = math::helpers::deg_to_rad( systems::g_input.get_view_angles( ).y );
		const auto angle = view_yaw - target_yaw - std::numbers::pi_v<float> *0.5f;
		const auto rx = cfg.radius_x.value;
		const auto ry = cfg.radius_y.value;

		const auto tip_x = center_x + std::cosf( angle ) * rx;
		const auto tip_y = center_y + std::sinf( angle ) * ry;

		const auto& color = info.is_visible ? cfg.visible_color : cfg.occluded_color;
		const auto width = cfg.width.value;
		const auto height = cfg.height.value;

		const auto fx = std::cosf( angle );
		const auto fy = std::sinf( angle );
		const auto px = -fy;
		const auto py = fx;

		const auto base_cx = tip_x - fx * height;
		const auto base_cy = tip_y - fy * height;

		const auto half_w = width * 0.5f;

		const auto bl_x = base_cx - px * half_w;
		const auto bl_y = base_cy - py * half_w;
		const auto br_x = base_cx + px * half_w;
		const auto br_y = base_cy + py * half_w;

		draw_list.triangle_filled( tip_x, tip_y, bl_x, bl_y, br_x, br_y, color );

		if ( cfg.glow )
		{
			auto& glow = xdraw::get_glow( );
			const auto glow_a = static_cast< std::uint8_t >( static_cast< float >( color.value.a ) * cfg.glow_strength );
			const auto glow_col = xdraw::color{ color.value.r, color.value.g, color.value.b, glow_a };

			glow.triangle_filled( tip_x, tip_y, bl_x, bl_y, br_x, br_y, glow_col );
		}
	}

	overlay::info overlay::get_info( const systems::entities::cached& player, const systems::local::snapshot& local )
	{
		info info{};
		info.controller = player.ptr;

		if ( !info.controller )
		{
			return info;
		}

		if ( !memory::read<bool>( info.controller + SCHEMA( "CCSPlayerController", "m_bPawnIsAlive"_hash ) ) )
		{
			return info;
		}

		const auto pawn_handle = memory::read<std::uint32_t>( info.controller + SCHEMA( "CBasePlayerController", "m_hPawn"_hash ) );
		if ( !pawn_handle )
		{
			return info;
		}

		info.pawn = systems::g_entities.lookup( pawn_handle );
		if ( !info.pawn || info.pawn == local.view_pawn( ) )
		{
			return info;
		}

		info.health = memory::read<int>( info.pawn + SCHEMA( "C_BaseEntity", "m_iHealth"_hash ) );
		if ( info.health <= 0 )
		{
			return info;
		}

		info.team = memory::read<int>( info.pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
		info.is_other_team = local.is_this_other_team( info.team );

		const auto game_scene_node = memory::read<std::uintptr_t>( info.pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		if ( !game_scene_node )
		{
			return info;
		}

		// m_bDormant is ambiguous across the current schema scopes and can
		// resolve to unrelated data. Keep the last known transform, as chams do.

		const auto name_ptr = memory::read<std::uintptr_t>( info.controller + SCHEMA( "CCSPlayerController", "m_sSanitizedPlayerName"_hash ) );
		if ( name_ptr )
		{
			info.name = memory::read_string( name_ptr, 128 );
			std::ranges::transform( info.name, info.name.begin( ), [ ]( unsigned char c ) { return std::tolower( c ); } );
		}

		const auto money_services = memory::read<std::uintptr_t>( info.controller + SCHEMA( "CCSPlayerController", "m_pInGameMoneyServices"_hash ) );
		if ( money_services )
		{
			info.money = memory::read<int>( money_services + SCHEMA( "CCSPlayerController_InGameMoneyServices", "m_iAccount"_hash ) );
		}

		const auto item_services = memory::read<std::uintptr_t>( info.pawn + SCHEMA( "C_BasePlayerPawn", "m_pItemServices"_hash ) );
		if ( item_services )
		{
			info.has_helmet = memory::read<bool>( item_services + SCHEMA( "CCSPlayer_ItemServices", "m_bHasHelmet"_hash ) );
			info.has_defuser = memory::read<bool>( item_services + SCHEMA( "CCSPlayer_ItemServices", "m_bHasDefuser"_hash ) );
		}

		info.origin = memory::read<math::vector3>( game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
		info.distance = systems::g_view.origin( ).distance( info.origin ) * 0.01905f;
		info.ping = memory::read<int>( info.controller + SCHEMA( "CCSPlayerController", "m_iPing"_hash ) );
		info.armor = memory::read<int>( info.pawn + SCHEMA( "C_CSPlayerPawn", "m_ArmorValue"_hash ) );
		info.is_scoped = memory::read<bool>( info.pawn + SCHEMA( "C_CSPlayerPawn", "m_bIsScoped"_hash ) );
		info.is_defusing = memory::read<bool>( info.pawn + SCHEMA( "C_CSPlayerPawn", "m_bIsDefusing"_hash ) );
		info.is_flashed = memory::read<float>( info.pawn + SCHEMA( "C_CSPlayerPawnBase", "m_flFlashBangTime"_hash ) ) > 0.0f;
		info.bones = systems::g_bones.get_skeleton( info.pawn );
		info.is_visible = systems::g_tracing.is_visible( systems::g_view.origin( ), info.bones[ cstypes::bone_ids::head ].position, info.pawn, local.view_pawn( ) );

		const auto weapon_services = memory::read<std::uintptr_t>( info.pawn + SCHEMA( "C_BasePlayerPawn", "m_pWeaponServices"_hash ) );
		if ( weapon_services )
		{
			const auto weapon_handle = memory::read<std::uint32_t>( weapon_services + SCHEMA( "CPlayer_WeaponServices", "m_hActiveWeapon"_hash ) );
			if ( weapon_handle )
			{
				info.weapon.ptr = systems::g_entities.lookup( weapon_handle );
				if ( info.weapon.ptr )
				{
					info.weapon.vdata = memory::read<std::uintptr_t>( info.weapon.ptr + SCHEMA( "C_BaseEntity", "m_nSubclassID"_hash ) + 0x8 );
					if ( info.weapon.vdata )
					{
						info.weapon.ammo = memory::read<int>( info.weapon.ptr + SCHEMA( "C_BasePlayerWeapon", "m_iClip1"_hash ) );
						info.weapon.max_ammo = memory::read<int>( info.weapon.vdata + SCHEMA( "CBasePlayerWeaponVData", "m_iMaxClip1"_hash ) );

						const auto weapon_name_ptr = memory::read<const char*>( info.weapon.vdata + SCHEMA( "CCSWeaponBaseVData", "m_szName"_hash ) );
						if ( weapon_name_ptr )
						{
							info.weapon.name = memory::read_string( reinterpret_cast< std::uintptr_t >( weapon_name_ptr ), 64 );
							if ( info.weapon.name.starts_with( "weapon_" ) )
							{
								info.weapon.name.erase( 0, 7 );
							}
						}
					}
				}
			}
		}

		return info;
	}

} // namespace features::esp::player
