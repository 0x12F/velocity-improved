#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/random/random.hpp>
#include <utilities/logging/logging.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>

namespace features::combat {

	void legit::on_create_move( systems::input::usercmd* cmd )
	{
		if ( !settings::g_combat.m_legitbot.enabled.value )
		{
			return;
		}

		auto& ctx = g_shared.ctx( );
		if ( !ctx.valid )
		{
			return;
		}

		if ( ctx.weapon_type < cstypes::weapon_type::pistol || ctx.weapon_type > cstypes::weapon_type::lmg )
		{
			return;
		}

		const auto& config = settings::g_combat.m_legitbot.get_group( ctx.weapon_type );
		const auto view_angles = systems::g_input.get_view_angles( );
		const auto local = systems::g_local.get( );
		const auto aim_punch = g_shared.get_aim_punch( local.pawn );

		this->m_cached_view_angles = view_angles;
		this->m_cached_aim_punch = aim_punch;

		if ( config.standalone_rcs.value )
		{
			this->update_standalone_rcs( view_angles, aim_punch, config.standalone_rcs_strength.value, config.standalone_rcs_min.value, config.standalone_rcs_max.value, !config.aimbot.value, local );
		}

		this->m_target = {};

		if ( !g_shared.can_shoot( cmd, local.controller, false ) )
		{
			return;
		}

		auto shoot_position = g_shared.get_interpolated_shoot_position( local.pawn, false );
		ctx.spread = g_shared.get_spread( );
		ctx.inaccuracy = g_shared.get_inaccuracy( true );

		systems::g_prediction.simulate( cmd, local, [ & ]
			{
				shoot_position = g_shared.get_interpolated_shoot_position( local.pawn, false );

				ctx.spread = g_shared.get_spread( );
				ctx.inaccuracy = g_shared.get_inaccuracy( true );
			} );

		auto detection_angles = view_angles;
		if ( config.rcs.value && aim_punch.length_sqr( ) > 0.0001f )
		{
			detection_angles.x += aim_punch.x;
			detection_angles.y += aim_punch.y;
			math::helpers::normalize_angles( detection_angles );
		}

		if ( config.aimbot.value )
		{
			this->m_target = this->find_target( shoot_position, detection_angles, config, local );
			if ( this->m_target.has_target( ) )
			{
				this->apply_aimbot( cmd, this->m_target, view_angles, aim_punch, config, local );
			}
		}

		if ( !g_shared.can_shoot( cmd, local.controller ) )
		{
			return;
		}

		if ( config.triggerbot.value )
		{
			this->apply_triggerbot( cmd, shoot_position, view_angles, aim_punch, config, local );
		}
	}

	void legit::on_render( xdraw::draw_list& draw_list )
	{
		if ( !settings::g_combat.m_legitbot.enabled.value )
		{
			return;
		}

		const auto& ctx = g_shared.ctx( );
		if ( !ctx.valid )
		{
			return;
		}

		if ( ctx.weapon_type < cstypes::weapon_type::pistol || ctx.weapon_type > cstypes::weapon_type::lmg )
		{
			return;
		}

		const auto& config = settings::g_combat.m_legitbot.get_group( ctx.weapon_type );

		if ( config.visualize_fov.value )
		{
			this->draw_fov( draw_list, this->m_cached_view_angles, this->m_cached_aim_punch, config.fov.value, config.fov_color, config.rcs.value );
		}
	}

	void legit::invalidate_if_needed( )
	{
		const auto local = systems::g_local.get( );
		if ( !local.is_alive || !local.pawn || !local.controller )
		{
			this->m_trigger_release_time = 0.0f;
			this->m_trigger_pending_pawn = 0;
			this->m_trigger_delay_start = 0.0f;
		}
	}

	legit::target_result legit::find_target( const math::vector3& shoot_position, const math::vector3& view_angles, const settings::combat::legitbot::weapon_group& config, const systems::local::snapshot& local ) const
	{
		target_result best{};

		for ( const auto& p : systems::g_entities.get_by_type( systems::entities::type::player ) )
		{
			if ( !p.ptr || p.ptr == local.controller )
			{
				continue;
			}

			if ( !memory::read<bool>( p.ptr + SCHEMA( "CCSPlayerController", "m_bPawnIsAlive"_hash ) ) )
			{
				continue;
			}

			const auto pawn_handle = memory::read<std::uint32_t>( p.ptr + SCHEMA( "CBasePlayerController", "m_hPawn"_hash ) );
			const auto pawn = systems::g_entities.lookup( pawn_handle );

			if ( !pawn || pawn == local.pawn )
			{
				continue;
			}

			const auto team = memory::read<std::int32_t>( pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
			if ( !local.is_this_other_team( team ) )
			{
				continue;
			}

			const auto health = memory::read<std::int32_t>( pawn + SCHEMA( "C_BaseEntity", "m_iHealth"_hash ) );
			if ( health <= 0 )
			{
				continue;
			}

			if ( memory::read<bool>( pawn + SCHEMA( "C_CSPlayerPawn", "m_bGunGameImmunity"_hash ) ) )
			{
				continue;
			}

			const auto records = g_shared.lc( ).get_valid_records( pawn );
			if ( records.empty( ) )
			{
				continue;
			}

			const auto record = records.front( );
			if ( !record || !record->valid )
			{
				continue;
			}

			const auto point = this->scan_player( pawn, record, shoot_position, view_angles, config, local );
			if ( !point.valid )
			{
				continue;
			}

			const auto aim = math::helpers::calculate_angle( shoot_position, point.position );
			const auto fov = math::helpers::angle_distance( view_angles, aim );

			const auto can_kill = point.damage >= static_cast< float >( health );
			const auto best_can_kill = best.has_target( ) && best.best_point.damage >= static_cast< float >( best.health );

			auto score{ 1000.0f };

			if ( can_kill && !best_can_kill )
			{
				score += 10000.0f + point.damage;
			}
			else if ( can_kill == best_can_kill )
			{
				score += point.damage * 100.0f + ( 180.0f - fov );
			}
			else
			{
				continue;
			}

			if ( !best.has_target( ) || score > best.score )
			{
				best.pawn = pawn;
				best.aim_angle = aim;
				best.hitchance = 1.0f;
				best.score = score;
				best.fov = fov;
				best.health = health;
				best.record = record;
				best.best_point = point;
			}
		}

		return best;
	}

	legit::scan_point legit::scan_player( std::uintptr_t pawn, shared::lagcomp::record* record, const math::vector3& shoot_position, const math::vector3& view_angles, const settings::combat::legitbot::weapon_group& config, const systems::local::snapshot& local ) const
	{
		struct hitbox_entry
		{
			std::size_t cfg_index;
			std::uint32_t bone_id;
			int hitgroup;
		};

		constexpr std::array<hitbox_entry, 7> hitbox_map
		{ {
			{ 0, cstypes::bone_ids::head,             1 },
			{ 1, cstypes::bone_ids::spine_3,          2 },
			{ 2, cstypes::bone_ids::spine_2,          3 },
			{ 3, cstypes::bone_ids::left_shoulder,    4 },
			{ 3, cstypes::bone_ids::right_shoulder,   5 },
			{ 4, cstypes::bone_ids::left_knee,        6 },
			{ 4, cstypes::bone_ids::right_knee,       7 },
		} };

		const auto pen_ctx = g_shared.pen( ).prepare_target( pawn, record );
		const auto skeleton = g_shared.lc( ).get_skeleton( *record );

		scan_point best{};
		best.fov = 999.0f;
		best.damage = -1.0f;

		for ( const auto& [cfg_idx, bone_id, hitgroup] : hitbox_map )
		{
			if ( !config.hitboxes.values[ cfg_idx ] )
			{
				continue;
			}

			const auto bone_index = static_cast< std::size_t >( bone_id );
			if ( bone_index >= 27 )
			{
				continue;
			}

			const auto& bone = skeleton[ bone_index ];
			if ( bone.position.length_sqr( ) < 1.0f )
			{
				continue;
			}

			const auto aim = math::helpers::calculate_angle( shoot_position, bone.position );
			const auto fov = math::helpers::angle_distance( view_angles, aim );

			if ( fov > config.fov.value )
			{
				continue;
			}

			shared::penetration::result pen{};
			if ( !g_shared.pen( ).run( shoot_position, bone.position, pen_ctx, local.pawn, local.team, pen ) )
			{
				continue;
			}

			const auto visible = !pen.penetrated;
			if ( !visible && !config.autowall.value )
			{
				continue;
			}

			if ( !visible && pen.damage < static_cast< float >( config.min_damage.value ) )
			{
				continue;
			}

			const auto is_better = ( pen.damage > best.damage ) || ( pen.damage == best.damage && fov < best.fov );
			if ( is_better )
			{
				best.position = bone.position;
				best.damage = pen.damage;
				best.fov = fov;
				best.hitgroup = pen.hitgroup;
				best.cfg_index = cfg_idx;
				best.bone_index = static_cast< int >( bone_index );
				best.visible = visible;
				best.is_center = true;
				best.valid = true;
			}
		}

		return best;
	}

	void legit::apply_aimbot( systems::input::usercmd* cmd, const target_result& tgt, const math::vector3& view_angles, const math::vector3& aim_punch, const settings::combat::legitbot::weapon_group& config, const systems::local::snapshot& local )
	{
		auto aim_angle = tgt.aim_angle;

		if ( config.rcs.value )
		{
			this->apply_rcs( aim_angle, aim_punch, config.rcs_min.value, config.rcs_max.value );
		}

		if ( config.smooth.value > 0 )
		{
			auto delta = aim_angle - view_angles;
			math::helpers::normalize_angles( delta );

			const auto delta_length = std::sqrtf( delta.x * delta.x + delta.y * delta.y );
			if ( delta_length < 0.001f )
			{
				this->m_remainder_x = 0.0f;
				this->m_remainder_y = 0.0f;
				return;
			}

			const auto base_smooth = static_cast< float >( config.smooth.value );
			const auto distance_factor = std::clamp( delta_length / 10.0f, 0.0f, 1.0f );
			const auto ease = 1.0f - std::powf( distance_factor, 2.0f );
			auto smooth_factor = ( 0.3f + ease * 0.7f ) / base_smooth;

			smooth_factor *= random::normal_clamped( 1.0f, 0.06f, 0.85f, 1.15f );

			const auto x_bias = random::normal_clamped( 1.0f, 0.02f, 0.95f, 1.05f );
			const auto y_bias = random::normal_clamped( 0.97f, 0.03f, 0.90f, 1.04f );

			auto move_x = delta.x * smooth_factor * x_bias;
			auto move_y = delta.y * smooth_factor * y_bias;

			if ( delta_length < 2.0f && delta_length > 0.3f && random::floating( 0.0f, 1.0f ) < 0.15f )
			{
				const auto overshoot = random::normal_clamped( 1.2f, 0.08f, 1.05f, 1.4f );
				move_x *= overshoot;
				move_y *= overshoot;
			}

			aim_angle = view_angles + math::vector3{ move_x, move_y, 0.0f };
			math::helpers::normalize_angles( aim_angle );
		}

		auto want_x = aim_angle.x - view_angles.x;
		auto want_y = aim_angle.y - view_angles.y;

		want_x += this->m_remainder_x;
		want_y += this->m_remainder_y;

		const auto sensitivity = CONVAR ("sensitivity")->get<float>( );
		const auto fov_adjust = memory::read<float>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_flFOVSensitivityAdjust"_hash ) );
		const auto deg_per_count = sensitivity * 0.022f * fov_adjust;

		const auto counts_x = std::roundf( want_x / deg_per_count );
		const auto counts_y = std::roundf( want_y / deg_per_count );

		this->m_remainder_x = want_x - counts_x * deg_per_count;
		this->m_remainder_y = want_y - counts_y * deg_per_count;

		systems::g_legit_input.add_mouse_delta( counts_x * deg_per_count, counts_y * deg_per_count );
	}

	void legit::apply_triggerbot( systems::input::usercmd* cmd, const math::vector3& shoot_position, const math::vector3& view_angles, const math::vector3& aim_punch, const settings::combat::legitbot::weapon_group& config, const systems::local::snapshot& local )
	{
		const auto& ctx = g_shared.ctx( );
		const auto seed_mode = config.give_me_your_seed.value;

		if ( this->m_trigger_release_time > 0.0f )
		{
			if ( ctx.current_time >= this->m_trigger_release_time )
			{
				this->m_trigger_release_time = 0.0f;
				this->m_trigger_pending_pawn = 0;
				return;
			}

			cmd->buttons.value |= cstypes::command_buttons::in_attack;
			cmd->buttons.value_changed |= cstypes::command_buttons::in_attack;
			return;
		}

		auto corrected_angles = view_angles;
		if ( config.rcs.value && aim_punch.length_sqr( ) > 0.0001f )
		{
			corrected_angles.x += aim_punch.x;
			corrected_angles.y += aim_punch.y;
			math::helpers::normalize_angles( corrected_angles );
		}

		math::vector3 bullet_dir{};
		std::uint32_t seed{};
		math::vector2 spread{};

		if ( seed_mode )
		{
			const auto tick_base = memory::read<std::int32_t>( local.controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) );
			seed = g_shared.get_spread_seed( corrected_angles, tick_base );
			spread = g_shared.calculate_spread( seed, ctx.inaccuracy, ctx.spread, ctx.recoil_index, ctx.item_def_idx, ctx.num_bullets );

			math::vector3 forward{}, left{}, up{};
			math::helpers::angle_vectors_left( corrected_angles, &forward, &left, &up );

			bullet_dir = ( forward + left * spread.x + up * spread.y ).normalized( );
		}

		auto hit_pawn{ 0ull };
		shared::penetration::result pen{};
		shared::lagcomp::record* hit_record{ nullptr };
		auto found{ false };

		const auto gather_records = [ & ]( std::uintptr_t pawn ) -> std::array<shared::lagcomp::record*, 3>
			{
				std::array<shared::lagcomp::record*, 3> out{ nullptr, nullptr, nullptr };

				const auto records = g_shared.lc( ).get_valid_records( pawn );
				if ( records.empty( ) || !records.front( ) || !records.front( )->valid )
				{
					return out;
				}

				out[ 0 ] = records.front( );

				if ( records.size( ) > 2 )
				{
					const auto mid = records.size( ) / 2;

					if ( records[ mid ] && records[ mid ]->valid )
					{
						out[ 1 ] = records[ mid ];
					}

					if ( records.back( ) && records.back( )->valid )
					{
						out[ 2 ] = records.back( );
					}
				}
				else if ( records.size( ) > 1 )
				{
					if ( records.back( ) && records.back( )->valid )
					{
						out[ 1 ] = records.back( );
					}
				}

				return out;
			};

		for ( const auto& p : systems::g_entities.get_by_type( systems::entities::type::player ) )
		{
			if ( !p.ptr || p.ptr == local.controller )
			{
				continue;
			}

			if ( !memory::read<bool>( p.ptr + SCHEMA( "CCSPlayerController", "m_bPawnIsAlive"_hash ) ) )
			{
				continue;
			}

			const auto pawn_handle = memory::read<std::uint32_t>( p.ptr + SCHEMA( "CBasePlayerController", "m_hPawn"_hash ) );
			const auto pawn = systems::g_entities.lookup( pawn_handle );

			if ( !pawn || pawn == local.pawn )
			{
				continue;
			}

			const auto team = memory::read<std::int32_t>( pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
			if ( !local.is_this_other_team( team ) )
			{
				continue;
			}

			const auto health = memory::read<std::int32_t>( pawn + SCHEMA( "C_BaseEntity", "m_iHealth"_hash ) );
			if ( health <= 0 )
			{
				continue;
			}

			if ( memory::read<bool>( pawn + SCHEMA( "C_CSPlayerPawn", "m_bGunGameImmunity"_hash ) ) )
			{
				continue;
			}

			const auto recs = gather_records( pawn );
			if ( !recs[ 0 ] )
			{
				continue;
			}

			const auto game_scene_node = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
			const auto hitbox_set = systems::g_hitboxes.query( game_scene_node );

			for ( const auto rec : recs )
			{
				if ( !rec )
				{
					continue;
				}

				const auto skeleton = g_shared.lc( ).get_skeleton( *rec );

				if ( seed_mode )
				{
					for ( const auto& hb : hitbox_set )
					{
						if ( hb.bone < 0 || hb.bone >= 28 )
						{
							continue;
						}

						const auto& bone = skeleton[ hb.bone ];
						if ( bone.position.length_sqr( ) < 1.0f )
						{
							continue;
						}

						const auto capsule_start = bone.rotation.rotate_vector( hb.mins ) + bone.position;
						const auto capsule_end = bone.rotation.rotate_vector( hb.maxs ) + bone.position;
						const auto radius = hb.radius > 0.0f ? hb.radius * 0.9f : 1.8f;

						auto fraction{ 1.0f };
						if ( !g_shared.ray_vs_capsule( shoot_position, bullet_dir * ctx.range, capsule_start, capsule_end, radius, fraction ) )
						{
							continue;
						}

						const auto hitgroup = systems::g_hitboxes.hitgroup_from_hitbox( hb.index );

						if ( config.trigger_head_only.value && hitgroup != 1 )
						{
							continue;
						}

						const auto hit_point = shoot_position + bullet_dir * ctx.range * fraction;
						const auto pen_ctx = g_shared.pen( ).prepare_target( pawn, rec );

						shared::penetration::result candidate_pen{};
						if ( !g_shared.pen( ).run( shoot_position, hit_point, pen_ctx, local.pawn, local.team, candidate_pen ) )
						{
							continue;
						}

						if ( candidate_pen.penetrated && candidate_pen.damage < static_cast< float >( config.min_damage.value ) )
						{
							continue;
						}

						if ( !found || candidate_pen.damage > pen.damage )
						{
							hit_pawn = pawn;
							hit_record = rec;
							pen = candidate_pen;
							found = true;
						}

						break;
					}
				}
				else
				{
					const systems::hitboxes::entry* closest_hb{ nullptr };
					auto closest_fov{ FLT_MAX };

					for ( const auto& entry : hitbox_set )
					{
						if ( entry.bone < 0 || entry.bone >= 28 )
						{
							continue;
						}

						const auto& bone = skeleton[ entry.bone ];
						if ( bone.position.length_sqr( ) < 1.0f )
						{
							continue;
						}

						const auto center = bone.rotation.rotate_vector( ( entry.mins + entry.maxs ) * 0.5f ) + bone.position;
						const auto aim = math::helpers::calculate_angle( shoot_position, center );
						const auto fov = math::helpers::angle_distance( corrected_angles, aim );

						if ( fov > 2.0f )
						{
							continue;
						}

						if ( fov < closest_fov )
						{
							closest_fov = fov;
							closest_hb = &entry;
						}
					}

					if ( !closest_hb )
					{
						continue;
					}

					const auto& bone = skeleton[ closest_hb->bone ];
					const auto target_point = bone.rotation.rotate_vector( ( closest_hb->mins + closest_hb->maxs ) * 0.5f ) + bone.position;
					const auto pen_ctx = g_shared.pen( ).prepare_target( pawn, rec );

					shared::penetration::result candidate_pen{};
					if ( !g_shared.pen( ).run( shoot_position, target_point, pen_ctx, local.pawn, local.team, candidate_pen ) )
					{
						continue;
					}

					const auto visible = !candidate_pen.penetrated;
					if ( !visible )
					{
						if ( !config.autowall.value )
						{
							continue;
						}

						if ( candidate_pen.damage < static_cast< float >( config.min_damage.value ) )
						{
							continue;
						}
					}

					if ( !found || candidate_pen.damage > pen.damage )
					{
						hit_pawn = pawn;
						hit_record = rec;
						pen = candidate_pen;
						found = true;
					}
				}
			}
		}

		if ( !found )
		{
			this->m_trigger_pending_pawn = 0;
			return;
		}

		if ( config.trigger_head_only.value && pen.hitgroup != 1 )
		{
			this->m_trigger_pending_pawn = 0;
			return;
		}

		if ( pen.penetrated && pen.damage < static_cast< float >( config.min_damage.value ) )
		{
			this->m_trigger_pending_pawn = 0;
			return;
		}

		if ( !seed_mode )
		{
			const auto skeleton = g_shared.lc( ).get_skeleton( *hit_record );
			const auto game_scene_node = memory::read<std::uintptr_t>( hit_pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
			const auto hitbox_set = systems::g_hitboxes.query( game_scene_node );

			const systems::hitboxes::entry* best_hb{ nullptr };

			for ( const auto& entry : hitbox_set )
			{
				if ( entry.index == pen.hitbox )
				{
					best_hb = &entry;
					break;
				}
			}

			if ( best_hb && best_hb->bone >= 0 && best_hb->bone < 28 )
			{
				hit_record->apply( );
				const auto hc = g_shared.calculate_hitchance( shoot_position, corrected_angles, *best_hb, skeleton[ best_hb->bone ], ctx.inaccuracy, ctx.spread );
				hit_record->restore( );

				const auto min_hc = static_cast< float >( config.trigger_hitchance.value ) / 100.0f;
				if ( hc < min_hc && !g_shared.is_max_accuracy( ctx.inaccuracy ) )
				{
					this->m_trigger_pending_pawn = 0;
					return;
				}
			}

			const auto delay_ms = static_cast< float >( config.trigger_delay.value );

			if ( this->m_trigger_pending_pawn != hit_pawn )
			{
				this->m_trigger_pending_pawn = hit_pawn;
				this->m_trigger_delay_start = ctx.current_time;
			}

			const auto elapsed_ms = ( ctx.current_time - this->m_trigger_delay_start ) * 1000.0f;
			if ( elapsed_ms < delay_ms )
			{
				return;
			}
		}

		g_shared.last_shoot_tick( ) = memory::read<std::int32_t>( local.controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) );

		const auto record_time = cstypes::tick_fraction::from_value( hit_record->simulation_time / cstypes::tick_interval );
		const auto input_history_size = cmd->csgo_user_cmd.input_history_size( );
		const auto history_angles = seed_mode ? corrected_angles : math::vector3{ view_angles.x - aim_punch.x, view_angles.y - aim_punch.y, 0.0f };

		for ( auto i = 0; i < input_history_size; ++i )
		{
			const auto entry = cmd->csgo_user_cmd.mutable_input_history( i );
			if ( !entry )
			{
				continue;
			}

			if ( const auto angles = entry->mutable_view_angles( ) )
			{
				angles->set_x( history_angles.x );
				angles->set_y( history_angles.y );
			}

			entry->set_render_tick_count( record_time.tick + 1 );
			entry->set_render_tick_fraction( 0.0f );

			if ( entry->has_sv_interp0( ) )
			{
				const auto interp = entry->mutable_sv_interp0( );
				interp->set_src_tick( -1 );
				interp->set_dst_tick( -1 );
				interp->set_frac( 0.0f );
			}

			if ( entry->has_sv_interp1( ) )
			{
				const auto interp = entry->mutable_sv_interp1( );
				interp->set_src_tick( -1 );
				interp->set_dst_tick( -1 );
				interp->set_frac( 0.0f );
			}

			if ( entry->has_cl_interp( ) )
			{
				const auto interp = entry->mutable_cl_interp( );
				interp->set_frac( 0.0f );
			}
		}

		cmd->buttons.value |= cstypes::command_buttons::in_attack;
		cmd->buttons.value_changed |= cstypes::command_buttons::in_attack;

		if ( input_history_size > 0 )
		{
			cmd->csgo_user_cmd.set_attack1_start_history_index( input_history_size - 1 );
		}

		this->m_trigger_release_time = ctx.current_time + random::hold_duration( );
	}

	void legit::apply_rcs( math::vector3& aim_angle, const math::vector3& aim_punch, int rand_min, int rand_max ) const
	{
		if ( aim_punch.length_sqr( ) < 0.0001f )
		{
			return;
		}

		const auto factor = this->compute_rcs_factor( rand_min, rand_max );

		aim_angle.x -= aim_punch.x * factor;
		aim_angle.y -= aim_punch.y * factor;
		math::helpers::normalize_angles( aim_angle );
	}

	void legit::update_standalone_rcs( const math::vector3& view_angles, const math::vector3& aim_punch, int amount, int rand_min, int rand_max, bool apply, const systems::local::snapshot& local )
	{
		const auto shots_fired = memory::read<int>( local.pawn + SCHEMA( "C_CSPlayerPawn", "m_iShotsFired"_hash ) );
		if ( shots_fired > 1 )
		{
			const auto factor = this->compute_rcs_factor( rand_min, rand_max );
			const auto scale = static_cast< float >( amount ) / 100.0f;

			const auto punch_scaled = math::vector3
			{
				aim_punch.x * scale * factor,
				aim_punch.y * scale * factor,
				0.0f
			};

			if ( apply )
			{
				auto new_angles = view_angles;
				new_angles.x += this->m_old_punch.x - punch_scaled.x;
				new_angles.y += this->m_old_punch.y - punch_scaled.y;
				math::helpers::normalize_angles( new_angles );

				systems::g_input.set_view_angles( new_angles );
			}

			this->m_old_punch = punch_scaled;
		}
		else
		{
			this->m_old_punch = {};
		}
	}

	float legit::compute_rcs_factor( int rand_min, int rand_max ) const
	{
		const auto seed = static_cast< std::uint32_t >( g_shared.ctx( ).current_time * 1000.0f );
		const auto t = static_cast< float >( seed % 1000 ) / 1000.0f;
		const auto min_scale = static_cast< float >( rand_min ) / 100.0f;
		const auto max_scale = static_cast< float >( rand_max ) / 100.0f;
		return min_scale + ( max_scale - min_scale ) * t;
	}

	void legit::draw_fov( xdraw::draw_list& draw_list, const math::vector3& view_angles, const math::vector3& aim_punch, float fov_degrees, const config::col& color, bool rcs_active ) const
	{
		const auto [screen_w, screen_h] = xdraw::viewport_size( );
		const auto sw = static_cast< float >( screen_w );
		const auto sh = static_cast< float >( screen_h );

		const auto camera_fov_rad = math::helpers::deg_to_rad( systems::g_view.fov( ) );
		const auto aimbot_fov_rad = math::helpers::deg_to_rad( fov_degrees );
		const auto radius = std::tanf( aimbot_fov_rad ) / std::tanf( camera_fov_rad * 0.5f ) * ( sw * 0.5f );

		auto offset_x{ 0.0f };
		auto offset_y{ 0.0f };

		if ( rcs_active )
		{
			const auto punch_magnitude = aim_punch.length_sqr( );
			const auto current_time = g_shared.ctx( ).current_time;

			if ( punch_magnitude > 0.5f )
			{
				this->m_last_significant_punch_time = current_time;
			}

			const auto time_since = current_time - this->m_last_significant_punch_time;
			if ( time_since < 0.3f && punch_magnitude > 0.01f )
			{
				auto corrected = view_angles;
				corrected.x -= aim_punch.x;
				corrected.y -= aim_punch.y;
				math::helpers::normalize_angles( corrected );

				math::vector3 center_dir{}, corrected_dir{};
				math::helpers::angle_vectors_left( view_angles, &center_dir );
				math::helpers::angle_vectors_left( corrected, &corrected_dir );

				const auto render_origin = systems::g_frame_data.origin( );
				const auto cs = systems::g_view.project( render_origin + center_dir * 1000.0f );
				const auto ns = systems::g_view.project( render_origin + corrected_dir * 1000.0f );

				if ( systems::g_view.projection_valid( cs ) && systems::g_view.projection_valid( ns ) )
				{
					offset_x = ( cs.x - ns.x ) * 0.5f;
					offset_y = ( cs.y - ns.y ) * 0.5f;
				}
			}
		}

		const auto cx = sw * 0.5f + offset_x;
		const auto cy = sh * 0.5f + offset_y;
		const auto& c = color.value;

		draw_list.circle( cx, cy, radius, xdraw::color{ c.r, c.g, c.b, c.a }, 1.5f, 64 );
	}

	int legit::hitgroup_to_cfg( int hitgroup )
	{
		static constexpr int table[ ]{ -1, 0, 1, 2, 3, 3, 4, 4 };

		if ( hitgroup < 0 || hitgroup >= static_cast< int >( std::size( table ) ) )
		{
			return -1;
		}

		return table[ hitgroup ];
	}

} // namespace features::combat
