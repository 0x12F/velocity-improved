#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>
#include <utilities//addresses/addresses.hpp>
#include <external/xdraw/xui/xui.hpp>

namespace features::misc {

	void hud::on_render( xdraw::draw_list& draw_list )
	{
		if ( systems::g_local.is_in_cinematic( ) || systems::g_local.is_in_time_freeze( ) )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.pawn || !local.controller || !systems::g_entities.exists( local.controller ) || !local.is_alive )
		{
			return;
		}

		const auto [screen_w, screen_h] = xdraw::viewport_size( );
		const auto cx = static_cast< float >( screen_w ) * 0.5f;
		const auto cy = static_cast< float >( screen_h ) * 0.5f;

		this->do_scope( draw_list, cx, cy, static_cast< float >( screen_h ), local.pawn );
		this->do_crosshair( draw_list, cx, cy );
		this->do_hat( draw_list, local.pawn );
		this->do_velocity( draw_list, cx, static_cast< float >( screen_h ), local.pawn );
	}

	void hud::do_crosshair( xdraw::draw_list& draw_list, float cx, float cy ) const
	{
		const auto& cfg = settings::g_misc.m_hud.m_crosshair;
		if ( !cfg.enabled.value )
		{
			return;
		}

		if ( this->m_scope_anim > 0.01f )
		{
			return;
		}

		const auto s = cfg.size;
		const auto o = cfg.outline;

		if ( o > 0.0f )
		{
			draw_list.rect_filled( cx - s - o, cy - s - o, ( s + o ) * 2.0f, ( s + o ) * 2.0f, cfg.outline_color );
		}

		draw_list.rect_filled( cx - s, cy - s, s * 2.0f, s * 2.0f, cfg.color );
	}

	void hud::do_scope( xdraw::draw_list& draw_list, float cx, float cy, float screen_h, std::uintptr_t local_pawn )
	{
		const auto& cfg = settings::g_misc.m_hud.m_scope;
		if ( !cfg.enabled.value )
		{
			return;
		}

		const auto is_scoped = memory::read<bool>( local_pawn + SCHEMA( "C_CSPlayerPawn", "m_bIsScoped"_hash ) );

		this->m_scope_anim = std::lerp( this->m_scope_anim, is_scoped ? 1.0f : 0.0f, std::min( xdraw::delta_time( ) * cfg.anim_speed, 1.0f ) );
		if ( this->m_scope_anim < 0.01f )
		{
			this->m_cached_spread_pixels = 0.0f;
			this->m_scope_update_frame = 0;
			return;
		}

		++this->m_scope_update_frame;
		const auto should_recalc = this->m_scope_update_frame >= 2 || this->m_cached_spread_pixels <= 0.0f;

		if ( should_recalc )
		{
			this->m_scope_update_frame = 0;

			const auto weapon_services = memory::read<std::uintptr_t>( local_pawn + SCHEMA( "C_BasePlayerPawn", "m_pWeaponServices"_hash ) );
			if ( weapon_services )
			{
				const auto weapon_handle = memory::read<std::uint32_t>( weapon_services + SCHEMA( "CPlayer_WeaponServices", "m_hActiveWeapon"_hash ) );
				const auto weapon = ( weapon_handle && weapon_handle != 0xffffffff ) ? systems::g_entities.lookup( weapon_handle ) : 0ull;

				if ( weapon )
				{
					const auto weapon_vdata = memory::read<std::uintptr_t>( weapon + SCHEMA( "C_BaseEntity", "m_nSubclassID"_hash ) + 0x8 );

					if ( weapon_vdata )
					{
						const auto vel = memory::read<math::vector3>( local_pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_hash ) );
						const auto fire_mode = memory::read<int>( weapon + SCHEMA( "C_CSWeaponBase", "m_weaponMode"_hash ) );
						const auto accuracy_penalty = memory::read<float>( weapon + SCHEMA( "C_CSWeaponBase", "m_fAccuracyPenalty"_hash ) );
						const auto turning_inaccuracy = memory::read<float>( weapon + SCHEMA( "C_CSWeaponBase", "m_flTurningInaccuracy"_hash ) );

						const auto max_speed_pair = memory::read<std::pair<float, float>>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flMaxSpeed"_hash ) );
						const auto inaccuracy_move_pair = memory::read<std::pair<float, float>>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyMove"_hash ) );
						const auto inaccuracy_jump_initial = memory::read<float>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyJumpInitial"_hash ) );
						const auto inaccuracy_jump_apex = memory::read<float>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyJumpApex"_hash ) );

						const auto max_speed = fire_mode ? max_speed_pair.second : max_speed_pair.first;
						const auto inaccuracy_move = fire_mode ? inaccuracy_move_pair.second : inaccuracy_move_pair.first;

						const auto speed = vel.length_2d( );
						const auto flags = memory::read<std::uint32_t>( local_pawn + SCHEMA( "C_BaseEntity", "m_fFlags"_hash ) );
						const auto is_walking = memory::read<bool>( local_pawn + SCHEMA( "C_CSPlayerPawn", "m_bIsWalking"_hash ) );
						const auto on_ground = ( flags & 1 ) != 0;

						const auto edge0 = max_speed * 0.34f;
						const auto edge1 = max_speed * 0.95f;

						auto move_factor = ( edge0 == edge1 ) ? ( speed >= edge1 ? 1.0f : 0.0f ) : std::clamp( ( speed - edge0 ) / ( edge1 - edge0 ), 0.0f, 1.0f );
						auto move_inac{ 0.0f };

						if ( move_factor > 0.0f )
						{
							if ( !is_walking )
							{
								move_factor = std::powf( move_factor, 0.25f );
							}

							move_inac = move_factor * inaccuracy_move;
						}

						auto air_inac{ 0.0f };

						if ( !on_ground )
						{
							const auto jump_impulse = CONVAR ("sv_jump_impulse")->get<float>( );
							const auto sqrt_threshold = std::sqrtf( std::fabsf( jump_impulse ) );
							const auto sqrt_vertical = std::sqrtf( std::fabsf( vel.z ) );
							const auto lo = sqrt_threshold * 0.25f;

							if ( lo == sqrt_threshold )
							{
								air_inac = ( sqrt_vertical >= sqrt_threshold ) ? inaccuracy_jump_initial : inaccuracy_jump_apex;
							}
							else
							{
								const auto frac = ( sqrt_vertical - lo ) / ( sqrt_threshold - lo );
								air_inac = inaccuracy_jump_apex + frac * ( inaccuracy_jump_initial - inaccuracy_jump_apex );
							}

							air_inac = std::clamp( air_inac, 0.0f, inaccuracy_jump_initial * 2.0f );
						}

						const auto inaccuracy = std::fminf( 1.0f, turning_inaccuracy + move_inac + air_inac + accuracy_penalty );

						const auto fov_rad = systems::g_view.fov( ) * ( std::numbers::pi_v<float> / 180.0f );
						this->m_cached_spread_pixels = inaccuracy * 320.0f / std::tanf( fov_rad * 0.5f ) * ( screen_h / 480.0f );
					}
				}
			}
		}

		this->m_spread_smooth = std::lerp( this->m_spread_smooth, this->m_cached_spread_pixels, std::min( xdraw::delta_time( ) * 50.0f, 1.0f ) );

		const auto gap = std::max( cfg.gap.value, this->m_spread_smooth ) + ( 40.0f * ( 1.0f - this->m_scope_anim ) );
		const auto length = cfg.line_length * this->m_scope_anim;
		const auto alpha = static_cast< std::uint8_t >( cfg.color.value.a * this->m_scope_anim );

		const auto col_clear = xdraw::color{ cfg.color.value.r, cfg.color.value.g, cfg.color.value.b, 0 };
		const auto col_solid = xdraw::color{ cfg.color.value.r, cfg.color.value.g, cfg.color.value.b, alpha };
		const auto& col_start = cfg.fade_in ? col_clear : col_solid;

		if ( cfg.glow && alpha > 0 )
		{
			auto& glow = xdraw::get_glow( );
			const auto glow_a = static_cast< std::uint8_t >( static_cast< float >( alpha ) * cfg.glow_strength );
			const auto glow_col = xdraw::color{ cfg.color.value.r, cfg.color.value.g, cfg.color.value.b, glow_a };

			const auto glow_draw_line = [ & ]( float x1, float y1, float x2, float y2 )
				{
					const auto mx = ( x1 + x2 ) * 0.5f;
					const auto my = ( y1 + y2 ) * 0.5f;

					const auto glow_thickness = cfg.thickness + 0.5f;

					const auto gc_clear = xdraw::color{ cfg.color.value.r, cfg.color.value.g, cfg.color.value.b, 0 };
					const auto& gc_start = cfg.fade_in ? gc_clear : glow_col;

					const float pts[ ]{ x1, y1, mx, my, x2, y2 };
					const xdraw::color cols[ ]{ gc_start, glow_col, glow_col };

					glow.polyline_gradient( pts, cols, false, glow_thickness );
				};

			glow_draw_line( cx, cy - gap, cx, cy - gap - length );
			glow_draw_line( cx, cy + gap, cx, cy + gap + length );
			glow_draw_line( cx - gap, cy, cx - gap - length, cy );
			glow_draw_line( cx + gap, cy, cx + gap + length, cy );
		}

		const auto draw_line = [ & ]( float x1, float y1, float x2, float y2 )
			{
				const auto mx = ( x1 + x2 ) * 0.5f;
				const auto my = ( y1 + y2 ) * 0.5f;

				const float pts[ ]{ x1, y1, mx, my, x2, y2 };
				const xdraw::color cols[ ]{ col_start, col_solid, col_solid };

				draw_list.polyline_gradient( pts, cols, false, cfg.thickness );
			};

		draw_line( cx, cy - gap, cx, cy - gap - length );
		draw_line( cx, cy + gap, cx, cy + gap + length );
		draw_line( cx - gap, cy, cx - gap - length, cy );
		draw_line( cx + gap, cy, cx + gap + length, cy );
	}

	void hud::do_hat( xdraw::draw_list& draw_list, std::uintptr_t local_pawn ) const
	{
		const auto& cfg = settings::g_misc.m_hud.m_hat;
		if ( !cfg.enabled || !systems::g_frame_data.valid( ) || !settings::g_misc.m_camera.thirdperson.value )
		{
			return;
		}

		const auto& primary_col = cfg.color.value;
		const auto& secondary_col = cfg.secondary_color.value;

		const auto game_scene_node = memory::read<std::uintptr_t>( local_pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		if ( !game_scene_node )
		{
			return;
		}

		const auto hitbox_set = systems::g_hitboxes.query( game_scene_node, false );
		if ( hitbox_set.count < 1 || hitbox_set.entries[ 0 ].bone < 0 || hitbox_set.entries[ 0 ].bone >= 27 )
		{
			return;
		}

		const auto& head_hb = hitbox_set.entries[ 0 ];
		if ( head_hb.bone < 0 || head_hb.bone >= 27 )
		{
			return;
		}

		const auto head_bone = systems::g_bones.get( local_pawn, cstypes::bone_ids::head );
		if ( head_bone.position.length_sqr( ) < 1.0f )
		{
			return;
		}

		const auto hb_mid = ( head_hb.mins + head_hb.maxs ) * 0.5f;
		const auto center = head_bone.rotation.rotate_vector( hb_mid ) + head_bone.position;

		const auto capsule_a = head_bone.rotation.rotate_vector( head_hb.mins - hb_mid ) + center;
		const auto capsule_b = head_bone.rotation.rotate_vector( head_hb.maxs - hb_mid ) + center;
		const auto top_cap = ( capsule_a.z > capsule_b.z ) ? capsule_a : capsule_b;
		const auto up_world = ( top_cap - center ).normalized( );

		auto right_world = up_world.cross( math::vector3{ 0.0f, 1.0f, 0.0f } );
		if ( right_world.length_sqr( ) < 0.001f )
		{
			right_world = math::vector3{ 1.0f, 0.0f, 0.0f };
		}
		else
		{
			right_world = right_world.normalized( );
		}

		const auto forward_world = up_world.cross( right_world ).normalized( );
		const auto hat_origin = top_cap + up_world * 0.55f;

		constexpr auto segments{ 32 };

		auto project_ring = [ & ]( const math::vector3& ring_center, float radius, math::vector2* out, int count ) -> bool
			{
				for ( auto i = 0; i < count; ++i )
				{
					const auto angle = ( static_cast< float >( i ) / static_cast< float >( count ) ) * 2.0f * std::numbers::pi_v< float >;
					const auto world_pt = ring_center + right_world * ( std::cosf( angle ) * radius ) + forward_world * ( std::sinf( angle ) * radius );

					const auto sp = systems::g_view.project( world_pt );
					if ( !systems::g_view.projection_valid( sp ) )
					{
						return false;
					}

					out[ i ] = { sp.x, sp.y };
				}

				return true;
			};

		auto draw_ring = [ & ]( const math::vector2* pts, int count, const xdraw::color& col, float thickness )
			{
				for ( auto i = 0; i < count; ++i )
				{
					const auto next = ( i + 1 ) % count;
					draw_list.line( pts[ i ].x, pts[ i ].y, pts[ next ].x, pts[ next ].y, col, thickness );
				}
			};

		if ( cfg.type == settings::misc::hud::hat::hat_type::kasa )
		{
			constexpr auto base_radius{ 10.0f };
			constexpr auto rim_points{ 24 };
			constexpr auto spokes{ 24 };

			const auto peak_world = hat_origin + up_world * 7.0f;
			const auto peak_sp = systems::g_view.project( peak_world );

			if ( !systems::g_view.projection_valid( peak_sp ) )
			{
				return;
			}

			const math::vector2 peak{ peak_sp.x, peak_sp.y };
			math::vector2 base[ rim_points ];

			for ( auto i = 0; i < rim_points; ++i )
			{
				const auto angle = ( static_cast< float >( i ) / static_cast< float >( rim_points ) ) * 2.0f * std::numbers::pi_v< float >;
				const auto world_pt = hat_origin + right_world * ( std::cosf( angle ) * base_radius ) + forward_world * ( std::sinf( angle ) * base_radius );

				const auto sp = systems::g_view.project( world_pt );
				if ( !systems::g_view.projection_valid( sp ) )
				{
					return;
				}

				base[ i ] = { sp.x, sp.y };
			}

			if ( cfg.glow )
			{
				auto& glow = xdraw::get_glow( );
				const auto ga = static_cast< std::uint8_t >( static_cast< float >( primary_col.a ) * cfg.glow_strength );
				const auto glow_col = xdraw::color{ primary_col.r, primary_col.g, primary_col.b, ga };

				for ( auto i = 0; i < rim_points; ++i )
				{
					const auto next = ( i + 1 ) % rim_points;
					glow.line( base[ i ].x, base[ i ].y, base[ next ].x, base[ next ].y, glow_col, 3.0f );
				}

				const auto spoke_ga = static_cast< std::uint8_t >( static_cast< float >( secondary_col.a ) * cfg.glow_strength );
				const auto spoke_glow_col = xdraw::color{ secondary_col.r, secondary_col.g, secondary_col.b, spoke_ga };

				for ( auto i = 0; i < spokes; ++i )
				{
					const auto idx = ( i * rim_points ) / spokes;
					glow.line( base[ idx ].x, base[ idx ].y, peak.x, peak.y, spoke_glow_col, 2.0f );
				}
			}

			for ( auto i = 0; i < rim_points; ++i )
			{
				const auto next = ( i + 1 ) % rim_points;
				draw_list.line( base[ i ].x, base[ i ].y, base[ next ].x, base[ next ].y, primary_col, 1.2f );
			}

			for ( auto i = 0; i < spokes; ++i )
			{
				const auto idx = ( i * rim_points ) / spokes;
				draw_list.line( base[ idx ].x, base[ idx ].y, peak.x, peak.y, secondary_col, 0.75f );
			}

			return;
		}

		constexpr auto brim_radius{ 6.5f };
		constexpr auto crown_base_radius{ 5.0f };
		constexpr auto crown_top_radius{ 4.2f };
		constexpr auto crown_height{ 3.0f };
		constexpr auto brim_droop{ 1.5f };
		constexpr auto dome_height{ 1.8f };

		constexpr auto crown_stitches{ 2 };
		constexpr auto brim_stitches{ 4 };
		constexpr auto stitch_dash_on{ 3.0f };
		constexpr auto stitch_dash_off{ 3.0f };

		const auto junction_center = hat_origin;
		const auto crown_top_center = junction_center + up_world * crown_height;
		const auto brim_edge_center = junction_center - up_world * brim_droop;

		auto draw_ring_dashed = [ & ]( const math::vector2* pts, int count, const xdraw::color& col, float thickness, float dash_on, float dash_off )
			{
				auto accum{ 0.0f };
				auto drawing{ true };

				for ( auto i = 0; i < count; ++i )
				{
					const auto next = ( i + 1 ) % count;
					const auto dx = pts[ next ].x - pts[ i ].x;
					const auto dy = pts[ next ].y - pts[ i ].y;
					const auto seg_len = std::sqrtf( dx * dx + dy * dy );

					if ( seg_len < 0.001f )
					{
						continue;
					}

					auto t_start{ 0.0f };

					while ( t_start < 1.0f )
					{
						const auto budget = drawing ? dash_on : dash_off;
						const auto remaining = budget - accum;
						const auto t_step = remaining / seg_len;
						const auto t_end = std::min( t_start + t_step, 1.0f );

						if ( drawing )
						{
							const auto x0 = pts[ i ].x + dx * t_start;
							const auto y0 = pts[ i ].y + dy * t_start;
							const auto x1 = pts[ i ].x + dx * t_end;
							const auto y1 = pts[ i ].y + dy * t_end;
							draw_list.line( x0, y0, x1, y1, col, thickness );
						}

						accum += ( t_end - t_start ) * seg_len;

						if ( accum >= budget - 0.01f )
						{
							drawing = !drawing;
							accum = 0.0f;
						}

						t_start = t_end;
					}
				}
			};

		auto find_extremes = [ & ]( const math::vector2* pts, int count, int& out_left, int& out_right )
			{
				auto min_x = std::numeric_limits< float >::max( );
				auto max_x = std::numeric_limits< float >::lowest( );
				out_left = 0;
				out_right = 0;

				for ( auto i = 0; i < count; ++i )
				{
					if ( pts[ i ].x < min_x ) { min_x = pts[ i ].x; out_left = i; }
					if ( pts[ i ].x > max_x ) { max_x = pts[ i ].x; out_right = i; }
				}
			};

		math::vector2 brim_pts[ segments ];
		math::vector2 junction_pts[ segments ];
		math::vector2 crown_top_pts[ segments ];

		if ( !project_ring( brim_edge_center, brim_radius, brim_pts, segments ) )
		{
			return;
		}

		if ( !project_ring( junction_center, crown_base_radius, junction_pts, segments ) )
		{
			return;
		}

		if ( !project_ring( crown_top_center, crown_top_radius, crown_top_pts, segments ) )
		{
			return;
		}

		const auto pole_world = crown_top_center + up_world * dome_height;
		const auto pole_sp = systems::g_view.project( pole_world );

		if ( !systems::g_view.projection_valid( pole_sp ) )
		{
			return;
		}

		int brim_l{}, brim_r{}, junc_l{}, junc_r{}, top_l{}, top_r{};
		find_extremes( brim_pts, segments, brim_l, brim_r );
		find_extremes( junction_pts, segments, junc_l, junc_r );
		find_extremes( crown_top_pts, segments, top_l, top_r );

		constexpr auto arc_steps{ 16 };
		math::vector2 arc_left[ arc_steps + 1 ];
		math::vector2 arc_right[ arc_steps + 1 ];

		const auto left_azimuth = ( static_cast< float >( top_l ) / static_cast< float >( segments ) ) * 2.0f * std::numbers::pi_v< float >;
		const auto right_azimuth = ( static_cast< float >( top_r ) / static_cast< float >( segments ) ) * 2.0f * std::numbers::pi_v< float >;

		for ( auto i = 0; i <= arc_steps; ++i )
		{
			const auto t = static_cast< float >( i ) / static_cast< float >( arc_steps );
			const auto phi = t * ( std::numbers::pi_v< float > *0.5f );
			const auto r = crown_top_radius * std::cosf( phi );
			const auto h = std::sinf( phi ) * dome_height;

			const auto left_world = crown_top_center + up_world * h + right_world * ( std::cosf( left_azimuth ) * r ) + forward_world * ( std::sinf( left_azimuth ) * r );
			const auto right_world_pt = crown_top_center + up_world * h + right_world * ( std::cosf( right_azimuth ) * r ) + forward_world * ( std::sinf( right_azimuth ) * r );

			const auto lsp = systems::g_view.project( left_world );
			const auto rsp = systems::g_view.project( right_world_pt );

			arc_left[ i ] = { lsp.x, lsp.y };
			arc_right[ i ] = { rsp.x, rsp.y };
		}

		if ( cfg.glow )
		{
			auto& glow = xdraw::get_glow( );
			const auto ga = static_cast< std::uint8_t >( static_cast< float >( primary_col.a ) * cfg.glow_strength );
			const auto glow_col = xdraw::color{ primary_col.r, primary_col.g, primary_col.b, ga };

			for ( auto i = 0; i < segments; ++i )
			{
				const auto next = ( i + 1 ) % segments;
				glow.line( brim_pts[ i ].x, brim_pts[ i ].y, brim_pts[ next ].x, brim_pts[ next ].y, glow_col, 3.0f );
			}

			for ( auto i = 0; i < arc_steps; ++i )
			{
				glow.line( arc_left[ i ].x, arc_left[ i ].y, arc_left[ i + 1 ].x, arc_left[ i + 1 ].y, glow_col, 3.0f );
				glow.line( arc_right[ i ].x, arc_right[ i ].y, arc_right[ i + 1 ].x, arc_right[ i + 1 ].y, glow_col, 3.0f );
			}
		}

		draw_ring( brim_pts, segments, primary_col, 1.4f );
		draw_ring( junction_pts, segments, primary_col, 1.2f );

		draw_list.line( brim_pts[ brim_l ].x, brim_pts[ brim_l ].y, junction_pts[ junc_l ].x, junction_pts[ junc_l ].y, primary_col, 1.4f );
		draw_list.line( brim_pts[ brim_r ].x, brim_pts[ brim_r ].y, junction_pts[ junc_r ].x, junction_pts[ junc_r ].y, primary_col, 1.4f );

		draw_list.line( junction_pts[ junc_l ].x, junction_pts[ junc_l ].y, crown_top_pts[ top_l ].x, crown_top_pts[ top_l ].y, primary_col, 1.2f );
		draw_list.line( junction_pts[ junc_r ].x, junction_pts[ junc_r ].y, crown_top_pts[ top_r ].x, crown_top_pts[ top_r ].y, primary_col, 1.2f );

		for ( auto i = 0; i < arc_steps; ++i )
		{
			draw_list.line( arc_left[ i ].x, arc_left[ i ].y, arc_left[ i + 1 ].x, arc_left[ i + 1 ].y, primary_col, 1.2f );
			draw_list.line( arc_right[ i ].x, arc_right[ i ].y, arc_right[ i + 1 ].x, arc_right[ i + 1 ].y, primary_col, 1.2f );
		}

		const auto stitch_a = static_cast< std::uint8_t >( static_cast< float >( secondary_col.a ) * 0.7f );
		const auto stitch_draw_col = xdraw::color{ secondary_col.r, secondary_col.g, secondary_col.b, stitch_a };

		for ( auto s = 1; s <= crown_stitches; ++s )
		{
			const auto t = static_cast< float >( s ) / static_cast< float >( crown_stitches + 1 );
			const auto lerp_center = junction_center + ( crown_top_center - junction_center ) * t;
			const auto lerp_radius = crown_base_radius + ( crown_top_radius - crown_base_radius ) * t;

			math::vector2 stitch_ring[ segments ];
			if ( !project_ring( lerp_center, lerp_radius, stitch_ring, segments ) )
			{
				continue;
			}

			draw_ring_dashed( stitch_ring, segments, stitch_draw_col, 0.6f, stitch_dash_on, stitch_dash_off );
		}

		for ( auto s = 1; s <= brim_stitches; ++s )
		{
			const auto t = static_cast< float >( s ) / static_cast< float >( brim_stitches + 1 );
			const auto lerp_center = junction_center + ( brim_edge_center - junction_center ) * t;
			const auto lerp_radius = crown_base_radius + ( brim_radius - crown_base_radius ) * t;

			math::vector2 stitch_ring[ segments ];
			if ( !project_ring( lerp_center, lerp_radius, stitch_ring, segments ) )
			{
				continue;
			}

			draw_ring_dashed( stitch_ring, segments, stitch_draw_col, 0.6f, stitch_dash_on, stitch_dash_off );
		}
	}

	void hud::do_velocity( xdraw::draw_list& draw_list, float cx, float screen_h, std::uintptr_t local_pawn )
	{
		const auto& cfg = settings::g_misc.m_hud.m_velocity;
		if ( !cfg.counter.value && !cfg.chart.value )
		{
			return;
		}

		const auto velocity = memory::read<math::vector3>( local_pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_hash ) );
		const auto speed = velocity.length_2d( );
		const auto dt = xdraw::delta_time( );

		this->m_velocity_smoothed += ( speed - this->m_velocity_smoothed ) * std::min( 14.0f * dt, 1.0f );
		this->m_velocity_history[ this->m_velocity_history_head ] = this->m_velocity_smoothed;
		this->m_velocity_history_head = ( this->m_velocity_history_head + 1 ) % k_velocity_history;
		this->m_velocity_history_count = std::min( this->m_velocity_history_count + 1, k_velocity_history );

		const auto& s = xui::ctx( ).style;
		const xdraw::color accent = cfg.color;
		const auto accent_dim = xdraw::color{ accent.r, accent.g, accent.b, static_cast< std::uint8_t >( accent.a * 0.45f ) };
		const auto accent_fill = xdraw::color{ accent.r, accent.g, accent.b, static_cast< std::uint8_t >( accent.a * 0.18f ) };

		constexpr auto panel_r{ 10.0f };
		constexpr auto inner_pad{ 4.0f };
		constexpr auto inner_r{ 7.0f };
		constexpr auto text_pad_x{ 8.0f };
		constexpr auto text_nudge{ -1.0f };
		constexpr auto section_gap{ 4.0f };

		const auto chart_w = std::clamp( cfg.chart_width.value, 120.0f, 320.0f );
		const auto chart_h = std::clamp( cfg.chart_height.value, 24.0f, 80.0f );
		const auto chart_inner_h = chart_h - inner_pad * 2.0f;
		const auto chart_inner_w = chart_w - inner_pad * 2.0f;

		char speed_buf[ 16 ]{};
		std::snprintf( speed_buf, sizeof( speed_buf ), "%.0f", this->m_velocity_smoothed );

		const auto [ speed_vw, speed_vh ] = xdraw::measure_text( speed_buf );
		const auto [ speed_uw, speed_uh ] = xdraw::measure_text( " u/s" );
		const auto counter_pill_w = speed_vw + speed_uw + text_pad_x * 2.0f;
		const auto counter_pill_h = speed_vh + inner_pad * 2.0f;

		const auto panel_w = cfg.chart.value ? chart_w : counter_pill_w + inner_pad * 2.0f;
		const auto counter_block_h = cfg.counter.value ? counter_pill_h + inner_pad * 2.0f : 0.0f;
		const auto chart_block_h = cfg.chart.value ? chart_h : 0.0f;
		const auto stack_gap = ( cfg.counter.value && cfg.chart.value ) ? section_gap : 0.0f;
		const auto panel_h = counter_block_h + stack_gap + chart_block_h;

		const auto bottom_offset = std::clamp( cfg.bottom_offset.value, 20.0f, 220.0f );
		const auto panel_x = std::floor( cx - panel_w * 0.5f );
		const auto panel_y = std::floor( screen_h - bottom_offset - panel_h );

		draw_list.rect_filled_blurred( panel_x, panel_y, panel_w, panel_h, xdraw::corner_radius{ panel_r } );
		draw_list.rect_filled( panel_x, panel_y, panel_w, panel_h, s.window_bg, xdraw::corner_radius{ panel_r } );

		auto content_y = panel_y;

		if ( cfg.counter.value )
		{
			const auto pill_x = std::floor( cx - counter_pill_w * 0.5f );
			const auto pill_y = content_y + inner_pad;
			draw_list.rect_filled( pill_x, pill_y, counter_pill_w, counter_pill_h, s.child_bg, xdraw::corner_radius{ inner_r } );
			draw_list.text( pill_x + text_pad_x, pill_y + ( counter_pill_h - speed_vh ) * 0.5f + text_nudge, speed_buf, accent );
			draw_list.text( pill_x + text_pad_x + speed_vw, pill_y + ( counter_pill_h - speed_uh ) * 0.5f + text_nudge, " u/s", accent_dim );
			content_y += counter_block_h + stack_gap;
		}

		if ( !cfg.chart.value || this->m_velocity_history_count < 2 || chart_inner_w <= 1.0f || chart_inner_h <= 1.0f )
		{
			return;
		}

		const auto chart_x = panel_x + inner_pad;
		const auto chart_y = content_y + inner_pad;
		draw_list.rect_filled( chart_x, chart_y, chart_w - inner_pad * 2.0f, chart_inner_h, s.child_bg, xdraw::corner_radius{ inner_r } );

		auto peak = 50.0f;
		for ( std::size_t i = 0; i < this->m_velocity_history_count; ++i )
		{
			peak = std::max( peak, this->m_velocity_history[ i ] );
		}

		const auto target_scale = std::ceil( std::max( peak, this->m_velocity_smoothed ) / 50.0f ) * 50.0f;
		this->m_velocity_scale += ( target_scale - this->m_velocity_scale ) * std::min( 6.0f * dt, 1.0f );
		const auto scale = std::max( this->m_velocity_scale, 50.0f );

		const auto plot_x = chart_x + inner_pad;
		const auto plot_y = chart_y + inner_pad;
		const auto plot_w = chart_inner_w - inner_pad * 2.0f;
		const auto plot_h = chart_inner_h - inner_pad * 2.0f;
		const auto baseline_y = std::floor( plot_y + plot_h );

		draw_list.line( plot_x, baseline_y, plot_x + plot_w, baseline_y, xdraw::color{ 255, 255, 255, 18 }, 1.0f, false );

		const auto sample_count = this->m_velocity_history_count;
		const auto step_x = plot_w / static_cast< float >( sample_count - 1 );

		std::array<float, k_velocity_history * 2> points{};
		std::array<xdraw::color, k_velocity_history> point_colors{};

		for ( std::size_t i = 0; i < sample_count; ++i )
		{
			const auto idx = ( this->m_velocity_history_head + k_velocity_history - sample_count + i ) % k_velocity_history;
			const auto value = this->m_velocity_history[ idx ];
			const auto nx = plot_x + step_x * static_cast< float >( i );
			const auto ny = std::floor( plot_y + plot_h - ( value / scale ) * plot_h );

			points[ i * 2 ] = nx;
			points[ i * 2 + 1 ] = ny;
			point_colors[ i ] = accent;
		}

		for ( std::size_t i = 0; i + 1 < sample_count; ++i )
		{
			const auto x0 = points[ i * 2 ];
			const auto y0 = points[ i * 2 + 1 ];
			const auto x1 = points[ ( i + 1 ) * 2 ];
			const auto y1 = points[ ( i + 1 ) * 2 + 1 ];

			const auto fill_top = std::min( y0, y1 );
			const auto fill_h = std::max( 0.0f, baseline_y - fill_top );
			if ( fill_h > 0.0f )
			{
				const auto seg_w = std::max( 1.0f, x1 - x0 );
				draw_list.rect_filled_gradient(
					x0,
					fill_top,
					seg_w,
					fill_h,
					accent_fill,
					accent_fill,
					xdraw::color{ accent_fill.r, accent_fill.g, accent_fill.b, 0 },
					xdraw::color{ accent_fill.r, accent_fill.g, accent_fill.b, 0 }
				);
			}
		}

		draw_list.polyline_gradient(
			std::span<const float>{ points.data( ), sample_count * 2 },
			std::span<const xdraw::color>{ point_colors.data( ), sample_count },
			false,
			1.5f,
			false
		);

		const auto dot_x = points[ ( sample_count - 1 ) * 2 ];
		const auto dot_y = points[ ( sample_count - 1 ) * 2 + 1 ];
		draw_list.circle_filled( dot_x, dot_y, 2.0f, accent );
	}

} // namespace features::misc