#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>

#include <optional>
#include <utility>
#include <protection/game_addresses.hpp>

namespace features::misc {

	namespace {

		// Hull sim mask used before, merged with solids mask from visibility traces so thin breakables (e.g. glass) collide.
		constexpr std::uint32_t grenade_collision_mask_v = static_cast<std::uint32_t>( 0x2000c3001u | 0x001c3003u );

		// Penetration fudge after a pawn hit so the next trace does not re-penetrate the same hull.
		constexpr float k_pass_player_nudge{ 3.5f };

		[[nodiscard]] bool grenade_passes_through_hit_entity( std::uintptr_t entity )
		{
			if ( !entity )
			{
				return false;
			}

			const auto* name = systems::g_entities.get_schema_name( entity );
			if ( !name )
			{
				return false;
			}

			// CS grenade simulation does not collide with player pawns; traces that stop on them fake bounces.
			return fnv1a::runtime_hash( name ) == "C_CSPlayerPawn"_hash;
		}

		[[nodiscard]] systems::tracing::filter make_grenade_hull_filter( std::uintptr_t thrower_pawn, int sim_tick )
		{
			const auto skip_thrower = thrower_pawn && sim_tick < projectile_trajectory::k_thrower_collision_skip_ticks;
			return systems::g_tracing.make_filter( skip_thrower ? thrower_pawn : 0, grenade_collision_mask_v, 4 );
		}

		// Segment trace: advance through player hulls to match in-game grenades, stop on world/geo.
		[[nodiscard]] std::pair<math::vector3, std::optional<systems::tracing::result>> grenade_trace_hull_segment(
			const math::vector3& from,
			const math::vector3& to,
			const math::vector3& hull_mins,
			const math::vector3& hull_maxs,
			std::uintptr_t thrower_pawn,
			int sim_tick )
		{
			auto cursor = from;

			for ( auto pass = 0; pass < 16; ++pass )
			{
				const auto remainder = to - cursor;
				const auto remainder_len_sq = remainder.length_sqr( );
				if ( remainder_len_sq < 1.0e-10f )
				{
					return { to, std::nullopt };
				}

				const auto filter = make_grenade_hull_filter( thrower_pawn, sim_tick );
				const auto trace = systems::g_tracing.trace_hull( cursor, to, hull_mins, hull_maxs, filter );

				if ( trace.fraction >= 1.0f )
				{
					return { to, std::nullopt };
				}

				if ( grenade_passes_through_hit_entity( trace.hit_entity ) )
				{
					const auto rem_len = std::sqrt( remainder_len_sq );
					const auto nd = remainder * ( 1.0f / rem_len );
					cursor = trace.end_pos + nd * k_pass_player_nudge;
					continue;
				}

				return { trace.end_pos, trace };
			}

			return { cursor, std::nullopt };
		}

	} // namespace

	void projectile_trajectory::on_render( xdraw::draw_list& draw_list )
	{
		const auto local = systems::g_local.get( );
		if ( !settings::g_misc.m_projectile_trajectory.enabled.value || systems::g_local.is_in_cinematic( ) || !local.is_alive || !local.controller )
		{
			return;
		}

		const auto now = std::chrono::steady_clock::now( );
		const auto local_pawn_handle = memory::read<std::uint32_t>( local.controller + SCHEMA( "CBasePlayerController", "m_hPawn"_hash ) );

		if ( !local_pawn_handle )
		{
			return;
		}

		for ( auto& gren : this->m_in_flight )
		{
			if ( !gren.traj.valid )
			{
				continue;
			}

			if ( gren.thrower_handle != local_pawn_handle )
			{
				continue;
			}

			if ( !gren.detonated )
			{
				const auto elapsed = std::chrono::duration<float>( now - gren.throw_time ).count( );
				if ( elapsed >= gren.traj.duration )
				{
					gren.detonated = true;
					gren.detonate_time = now;
				}
			}

			auto alpha{ 1.0f };

			if ( gren.detonated )
			{
				const auto elapsed = std::chrono::duration<float>( now - gren.detonate_time ).count( );
				alpha = std::clamp( 1.0f - elapsed / k_fade_duration, 0.0f, 1.0f );
			}

			if ( alpha > 0.0f )
			{
				this->render_trajectory( draw_list, gren.traj, alpha, true, local );
			}
		}

		this->render_preview( draw_list, local );
	}

	void projectile_trajectory::on_create_move( systems::input::usercmd* cmd )
	{
		this->m_should_preview = false;
		this->m_needs_air_stop = false;

		if ( !settings::g_misc.m_projectile_trajectory.enabled.value || systems::g_local.is_in_cinematic( ) )
		{
			this->m_delay_release = false;
			this->m_delay_ticks = 0;
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.pawn || !local.is_alive )
		{
			this->m_delay_release = false;
			this->m_delay_ticks = 0;
			return;
		}

		this->m_sv_gravity = CONVAR ("sv_gravity")->get<float>( );
		this->m_molotov_max_slope_z = std::cos(CONVAR ("weapon_molotov_maxdetonateslope")->get<float>( ) * std::numbers::pi_v<float> / 180.0f );

		this->update_in_flight( local );

		const auto& ctx = combat::g_shared.ctx( );
		if ( ctx.weapon_type != cstypes::weapon_type::grenade )
		{
			this->m_delay_release = false;
			this->m_delay_ticks = 0;
			return;
		}

		this->update_weapon_properties( ctx.weapon, ctx.weapon_vdata );

		const auto pin_pulled = memory::read<bool>( ctx.weapon + SCHEMA( "C_BaseCSGrenade", "m_bPinPulled"_hash ) );
		const auto throw_time = memory::read<float>( ctx.weapon + SCHEMA( "C_BaseCSGrenade", "m_fThrowTime"_hash ) );
		const auto attacking = ( cmd->buttons.value & cstypes::command_buttons::in_attack ) != 0;
		const auto attacking2 = ( cmd->buttons.value & cstypes::command_buttons::in_second_attack ) != 0;
		const auto holding_attack = attacking || attacking2;

		if ( throw_time > 0.0f )
		{
			this->m_delay_release = false;
			this->m_delay_ticks = 0;
			this->m_throw_stopping = true;

			if ( settings::g_misc.m_projectile_trajectory.straight_throw.value )
			{
				this->m_needs_air_stop = !this->m_was_forward_only;
				this->correct_throw_angles( cmd, local, ctx.weapon );
			}

			return;
		}
		else
		{
			if ( this->m_throw_stopping )
			{
				this->m_last_throw_time = std::chrono::steady_clock::now( );
			}

			this->m_throw_stopping = false;
		}

		if ( holding_attack && pin_pulled )
		{
			this->m_delay_release = false;
			this->m_delay_ticks = 0;

			const auto has_forward = ( cmd->buttons.value & cstypes::command_buttons::in_forward ) != 0;
			const auto has_back = ( cmd->buttons.value & cstypes::command_buttons::in_back ) != 0;
			const auto has_left = ( cmd->buttons.value & cstypes::command_buttons::in_moveleft ) != 0;
			const auto has_right = ( cmd->buttons.value & cstypes::command_buttons::in_moveright ) != 0;

			this->m_was_forward_only = has_forward && !has_back && !has_left && !has_right;
		}
		else if ( pin_pulled && !holding_attack && settings::g_misc.m_projectile_trajectory.straight_throw.value )
		{
			if ( !this->m_delay_release )
			{
				this->m_delay_release = true;
				this->m_delay_ticks = 0;
				this->m_delayed_strength = std::clamp( memory::read<float>( ctx.weapon + SCHEMA( "C_BaseCSGrenade", "m_flThrowStrength"_hash ) ), 0.0f, 1.0f );
				this->m_delayed_attack2 = !attacking && attacking2;
			}

			auto predicted_velocity = systems::g_prediction.pre( ).networked_velocity;
			systems::g_prediction.simulate( cmd, local, [ & ]( ) { predicted_velocity = memory::read<math::vector3>( local.pawn + SCHEMA( "C_BaseEntity", "m_vecVelocity"_hash ) ); } );

			const auto vel_contribution = predicted_velocity * k_velocity_inherit;
			const auto throw_vel = std::clamp( this->m_throw_velocity * 0.9f, 15.0f, 750.0f );
			const auto throw_speed = ( this->m_delayed_strength * 0.7f + 0.3f ) * throw_vel;

			math::vector3 desired_forward{};
			this->compute_desired_direction( desired_forward );

			const auto vel_along = desired_forward * vel_contribution.dot( desired_forward );
			const auto vel_perp = vel_contribution - vel_along;
			const auto perp_len = std::sqrt( vel_perp.length_sqr( ) );
			const auto can_compensate = perp_len < throw_speed;

			if ( can_compensate || this->m_was_forward_only || this->m_delay_ticks >= k_max_delay_ticks )
			{
				this->m_delay_release = false;
				this->m_delay_ticks = 0;
				this->m_needs_air_stop = !this->m_was_forward_only;
			}
			else
			{
				if ( this->m_delayed_attack2 )
				{
					cmd->buttons.value |= cstypes::command_buttons::in_second_attack;
					cmd->buttons.value_changed |= cstypes::command_buttons::in_second_attack;
				}
				else
				{
					cmd->buttons.value |= cstypes::command_buttons::in_attack;
					cmd->buttons.value_changed |= cstypes::command_buttons::in_attack;
				}

				this->m_needs_air_stop = true;
				++this->m_delay_ticks;
				this->m_should_preview = true;
				return;
			}
		}

		this->m_was_holding = pin_pulled;

		if ( !pin_pulled )
		{
			const auto since_throw = std::chrono::duration<float>( std::chrono::steady_clock::now( ) - this->m_last_throw_time ).count( );
			if ( since_throw < k_throw_cooldown )
			{
				return;
			}
		}

		this->m_should_preview = true;
	}

	void projectile_trajectory::setup_throw( std::uintptr_t local_pawn, std::uintptr_t weapon, math::vector3& origin, math::vector3& velocity )
	{
		auto strength{ 1.0f };

		const auto pin_pulled = memory::read<bool>( weapon + SCHEMA( "C_BaseCSGrenade", "m_bPinPulled"_hash ) );
		const auto throw_time = memory::read<float>( weapon + SCHEMA( "C_BaseCSGrenade", "m_fThrowTime"_hash ) );

		if ( pin_pulled || throw_time > 0.0f )
		{
			strength = std::clamp( memory::read<float>( weapon + SCHEMA( "C_BaseCSGrenade", "m_flThrowStrength"_hash ) ), 0.0f, 1.0f );

			if ( std::abs( strength - 0.5f ) <= 0.1f )
			{
				strength = 0.5f;
			}
		}

		auto angles = systems::g_input.get_view_angles( );
		math::helpers::normalize_angle( angles.x );
		angles.x -= ( 90.0f - std::abs( angles.x ) ) * 10.0f / 90.0f;

		auto eye_pos = systems::g_frame_data.origin( ) + memory::read<math::vector3>( local_pawn + SCHEMA( "C_BaseModelEntity", "m_vecViewOffset"_hash ) );
		eye_pos.z += strength * 12.0f - 12.0f;

		math::vector3 forward{};
		{
			const auto pitch = angles.x * ( std::numbers::pi_v<float> / 180.0f );
			const auto yaw = angles.y * ( std::numbers::pi_v<float> / 180.0f );

			forward =
			{
				std::cos( pitch ) * std::cos( yaw ),
				std::cos( pitch ) * std::sin( yaw ),
				-std::sin( pitch )
			};
		}

		const auto hull_mins = math::vector3{ -k_hull_size, -k_hull_size, -k_hull_size };
		const auto hull_maxs = math::vector3{ k_hull_size, k_hull_size, k_hull_size };
		const auto trace = systems::g_tracing.trace_hull( eye_pos, eye_pos + forward * k_forward_offset, hull_mins, hull_maxs, local_pawn, grenade_collision_mask_v );
		origin = trace.end_pos - forward * k_pull_back;

		const auto throw_velocity = std::clamp( this->m_throw_velocity * 0.9f, 15.0f, 750.0f );
		const auto throw_speed = ( strength * 0.7f + 0.3f ) * throw_velocity;
		velocity = forward * throw_speed + systems::g_prediction.pre( ).networked_velocity * k_velocity_inherit;
	}

	void projectile_trajectory::simulate( const math::vector3& start, const math::vector3& velocity, std::uintptr_t thrower_pawn, trajectory& out )
	{
		out.points.clear( );
		out.points.reserve( k_max_ticks / k_ticks_per_point );
		out.bounces.clear( );
		out.valid = false;
		out.end_tick = -1;

		auto pos = start;
		auto vel = velocity;
		auto bounce_count{ 0 };
		auto tick_timer{ 0 };

		for ( auto tick = 0; tick < k_max_ticks; ++tick )
		{
			if ( tick_timer == 0 )
			{
				out.points.push_back( { pos } );
			}

			auto hit{ false };
			auto impact_detonate{ false };
			this->step_simulation( pos, vel, thrower_pawn, tick, hit, impact_detonate );

			if ( hit )
			{
				++bounce_count;
				out.bounces.push_back( pos );
			}

			const auto velocity_stopped = std::abs( vel.x ) < 20.0f && std::abs( vel.y ) < 20.0f && vel.length_sqr( ) < k_stop_speed_sq;

			if ( impact_detonate || this->should_detonate( vel, tick ) || bounce_count > k_max_bounces || velocity_stopped )
			{
				out.end_tick = tick;
				out.end_pos = pos;
				out.duration = static_cast< float >( tick ) * cstypes::tick_interval;
				break;
			}

			if ( hit || ++tick_timer >= k_ticks_per_point )
			{
				tick_timer = 0;
			}
		}

		if ( !out.points.empty( ) && out.end_tick >= 0 )
		{
			if ( out.points.back( ).distance_sqr( out.end_pos ) > 1.0f )
			{
				out.points.push_back( { out.end_pos } );
			}
		}

		out.valid = out.end_tick >= 0;
	}

	void projectile_trajectory::step_simulation( math::vector3& pos, math::vector3& vel, std::uintptr_t thrower_pawn, int sim_tick, bool& hit, bool& detonated )
	{
		detonated = false;

		const auto gravity = this->m_sv_gravity * k_gravity_scale;
		const auto new_vel_z = vel.z - gravity * cstypes::tick_interval;

		const math::vector3 move
		{
			vel.x * cstypes::tick_interval,
			vel.y * cstypes::tick_interval,
			( vel.z + new_vel_z ) * 0.5f * cstypes::tick_interval
		};

		vel.z = new_vel_z;

		const auto hull_mins = math::vector3{ -k_hull_size, -k_hull_size, -k_hull_size };
		const auto hull_maxs = math::vector3{ k_hull_size, k_hull_size, k_hull_size };
		const auto move_dest = pos + move;

		const auto [travel_end, world_hit_trace] = grenade_trace_hull_segment(
			pos, move_dest, hull_mins, hull_maxs, thrower_pawn, sim_tick );

		pos = travel_end;

		if ( world_hit_trace.has_value( ) )
		{
			hit = true;
			this->resolve_collision( *world_hit_trace, pos, vel, thrower_pawn, sim_tick, detonated );
		}
		else
		{
			hit = false;
		}
	}

	void projectile_trajectory::resolve_collision( const systems::tracing::result& trace, math::vector3& pos, math::vector3& vel, std::uintptr_t thrower_pawn, int sim_tick, bool& detonated ) const
	{
		detonated = false;

		if ( this->m_weapon_hash == "weapon_molotov"_hash || this->m_weapon_hash == "weapon_incgrenade"_hash )
		{
			if ( trace.normal.z >= this->m_molotov_max_slope_z || vel.length_sqr( ) < k_stop_speed_sq )
			{
				detonated = true;
				vel = {};
				return;
			}
		}

		auto new_vel = clip_velocity( vel, trace.normal, 2.0f );

		new_vel.x *= k_elasticity;
		new_vel.y *= k_elasticity;
		new_vel.z *= k_elasticity;

		if ( trace.hit_entity )
		{
			if ( !grenade_passes_through_hit_entity( trace.hit_entity ) )
			{
				const auto health = memory::read<int>( trace.hit_entity + SCHEMA( "C_BaseEntity", "m_iHealth"_hash ) );
				if ( health > 0 && health <= 100 )
				{
					if ( trace.fraction > k_player_hit_fraction_threshold )
					{
						const auto speed_sq = new_vel.length_sqr( );

						if ( speed_sq > k_steep_bounce_speed_sq )
						{
							const auto dot = new_vel.normalized( ).dot( trace.normal );

							if ( dot > k_player_dampen_dot_threshold )
							{
								const auto dampen = ( 1.0f - dot ) + 0.5f;
								new_vel.x *= dampen;
								new_vel.y *= dampen;
								new_vel.z *= dampen;
							}
						}
					}
				}
			}
		}

		if ( trace.normal.z > k_steep_bounce_normal_z )
		{
			const auto speed_sq = new_vel.length_sqr( );

			if ( speed_sq > k_steep_bounce_speed_sq )
			{
				const auto dot = new_vel.normalized( ).dot( trace.normal );
				if ( dot > 0.5f )
				{
					const auto dampen = 1.5f - dot;
					new_vel.x *= dampen;
					new_vel.y *= dampen;
					new_vel.z *= dampen;
				}
			}
		}

		if ( new_vel.length_sqr( ) < k_stop_speed_sq )
		{
			vel = {};
			return;
		}

		vel = new_vel;

		const auto remaining = 1.0f - trace.fraction;
		if ( remaining > 0.0f )
		{
			const auto hull_mins = math::vector3{ -k_hull_size, -k_hull_size, -k_hull_size };
			const auto hull_maxs = math::vector3{ k_hull_size, k_hull_size, k_hull_size };
			const auto slide_goal = pos + new_vel * ( remaining * cstypes::tick_interval );

			const auto [slide_end, slide_hit] = grenade_trace_hull_segment(
				pos, slide_goal, hull_mins, hull_maxs, thrower_pawn, sim_tick );

			( void ) slide_hit;

			pos = slide_end;
		}
	}

	void projectile_trajectory::set_simulation_params( std::uintptr_t weapon_hash )
	{
		switch ( weapon_hash )
		{
		case "weapon_molotov"_hash:
		case "weapon_incgrenade"_hash:
			this->m_detonate_time = 2.0f;
			this->m_velocity_threshold = 0.0f;
			break;
		case "weapon_decoy"_hash:
			this->m_detonate_time = 2.0f;
			this->m_velocity_threshold = 0.2f;
			break;
		default:
			this->m_detonate_time = 1.5f;
			this->m_velocity_threshold = 0.1f;
			break;
		}
	}

	bool projectile_trajectory::should_detonate( const math::vector3& vel, int tick ) const
	{
		switch ( this->m_weapon_hash )
		{
		case "weapon_smokegrenade"_hash:
		case "weapon_decoy"_hash:
		{
			const auto speed_2d = std::sqrt( vel.x * vel.x + vel.y * vel.y );
			const auto check_ticks = static_cast< int >( 0.2f / cstypes::tick_interval );
			return speed_2d < this->m_velocity_threshold && ( tick % check_ticks ) == 0;
		}
		case "weapon_molotov"_hash:
		case "weapon_incgrenade"_hash:
			return static_cast< float >( tick ) * cstypes::tick_interval > this->m_detonate_time;
		case "weapon_flashbang"_hash:
		case "weapon_hegrenade"_hash:
			return static_cast< float >( tick - 8 ) * cstypes::tick_interval > this->m_detonate_time;
		default:
			return false;
		}
	}

	math::vector3 projectile_trajectory::clip_velocity( const math::vector3& velocity, const math::vector3& normal, float overbounce )
	{
		const auto backoff = velocity.dot( normal ) * overbounce;

		math::vector3 out
		{
			velocity.x - normal.x * backoff,
			velocity.y - normal.y * backoff,
			velocity.z - normal.z * backoff
		};

		if ( std::abs( out.x ) < 0.1f ) out.x = 0.0f;
		if ( std::abs( out.y ) < 0.1f ) out.y = 0.0f;
		if ( std::abs( out.z ) < 0.1f ) out.z = 0.0f;

		return out;
	}

	void projectile_trajectory::update_weapon_properties( std::uintptr_t weapon, std::uintptr_t weapon_vdata )
	{
		if ( !weapon_vdata || weapon_vdata == this->m_weapon_vdata )
		{
			return;
		}

		this->m_weapon_vdata = weapon_vdata;
		this->m_throw_velocity = std::clamp( memory::read<float>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flThrowVelocity"_hash ) ), 1.0f, 10000.0f );

		const auto name_ptr = memory::read<std::uintptr_t>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_szName"_hash ) );
		if ( !name_ptr )
		{
			this->m_weapon_hash = 0;
			this->m_detonate_time = 1.5f;
			this->m_velocity_threshold = 0.1f;
			return;
		}

		const auto name = memory::read_string( name_ptr, 64 );
		this->m_weapon_hash = fnv1a::runtime_hash( name.c_str( ) );

		switch ( this->m_weapon_hash )
		{
		case "weapon_molotov"_hash:
		case "weapon_incgrenade"_hash:
			this->m_detonate_time = 2.0f;
			this->m_velocity_threshold = 0.0f;
			break;
		case "weapon_decoy"_hash:
			this->m_detonate_time = 2.0f;
			this->m_velocity_threshold = 0.2f;
			break;
		default:
			this->m_detonate_time = 1.5f;
			this->m_velocity_threshold = 0.1f;
			break;
		}
	}

	void projectile_trajectory::update_in_flight( const systems::local::snapshot& local )
	{
		this->m_sv_gravity = CONVAR ("sv_gravity")->get<float>( );
		this->m_molotov_max_slope_z = std::cos(CONVAR ("weapon_molotov_maxdetonateslope")->get<float>( ) * std::numbers::pi_v<float> / 180.0f );

		const auto local_pawn_handle = memory::read<std::uint32_t>( local.controller + SCHEMA( "CBasePlayerController", "m_hPawn"_hash ) );
		const auto local_team = memory::read<std::int32_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
		const auto now = std::chrono::steady_clock::now( );
		const auto projectiles = systems::g_entities.get_by_type( systems::entities::type::projectile );

		std::unordered_set<std::uintptr_t> alive{};

		for ( const auto& entry : projectiles )
		{
			if ( !entry.ptr )
			{
				continue;
			}

			const auto wh = this->get_other_name( entry.schema_hash );
			if ( !wh )
			{
				continue;
			}

			const auto thrower_handle = memory::read<std::uint32_t>( entry.ptr + SCHEMA( "C_BaseGrenade", "m_hThrower"_hash ) );
			auto is_enemy{ true };

			if ( thrower_handle == local_pawn_handle )
			{
				is_enemy = false;
			}
			else if ( thrower_handle && thrower_handle != 0xffffffff )
			{
				const auto thrower = systems::g_entities.lookup( thrower_handle );
				if ( thrower )
				{
					const auto thrower_team = memory::read<std::int32_t>( thrower + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
					is_enemy = thrower_team != local_team;
				}
			}

			alive.insert( entry.ptr );

			in_flight_grenade* existing{ nullptr };

			for ( auto& gren : this->m_in_flight )
			{
				if ( gren.entity == entry.ptr )
				{
					existing = &gren;
					break;
				}
			}

			if ( existing )
			{
				existing->last_seen = now;

				if ( !existing->corrected )
				{
					const auto initial_pos = memory::read<math::vector3>( entry.ptr + SCHEMA( "C_BaseCSGrenadeProjectile", "m_vInitialPosition"_hash ) );
					const auto initial_vel = memory::read<math::vector3>( entry.ptr + SCHEMA( "C_BaseCSGrenadeProjectile", "m_vInitialVelocity"_hash ) );

					if ( initial_vel.length_sqr( ) >= 1.0f )
					{
						const auto saved_hash = this->m_weapon_hash;
						const auto saved_detonate = this->m_detonate_time;
						const auto saved_threshold = this->m_velocity_threshold;

						this->m_weapon_hash = existing->weapon_hash;
						this->set_simulation_params( existing->weapon_hash );

						const auto ignore = existing->thrower_handle ? systems::g_entities.lookup( existing->thrower_handle ) : local.pawn;
						this->simulate( initial_pos, initial_vel, ignore ? ignore : local.pawn, existing->traj );

						this->m_weapon_hash = saved_hash;
						this->m_detonate_time = saved_detonate;
						this->m_velocity_threshold = saved_threshold;

						existing->corrected = true;
					}
				}

				if ( !existing->detonated )
				{
					auto effect_started{ false };

					switch ( wh )
					{
					case "weapon_hegrenade"_hash:
					case "weapon_flashbang"_hash:
						effect_started = memory::read<std::int32_t>( entry.ptr + SCHEMA( "C_BaseCSGrenadeProjectile", "m_nExplodeEffectTickBegin"_hash ) ) > 0;
						break;
					case "weapon_smokegrenade"_hash:
						effect_started = memory::read<std::int32_t>( entry.ptr + SCHEMA( "C_SmokeGrenadeProjectile", "m_nSmokeEffectTickBegin"_hash ) ) > 0;
						break;
					case "weapon_decoy"_hash:
						effect_started = memory::read<std::int32_t>( entry.ptr + SCHEMA( "C_DecoyProjectile", "m_nDecoyShotTick"_hash ) ) > 0;
						break;
					default:
						break;
					}

					if ( effect_started )
					{
						existing->detonated = true;
						existing->detonate_time = now;
					}
				}

				continue;
			}

			{
				auto already_detonated{ false };

				switch ( wh )
				{
				case "weapon_hegrenade"_hash:
				case "weapon_flashbang"_hash:
					already_detonated = memory::read<std::int32_t>( entry.ptr + SCHEMA( "C_BaseCSGrenadeProjectile", "m_nExplodeEffectTickBegin"_hash ) ) > 0;
					break;
				case "weapon_smokegrenade"_hash:
					already_detonated = memory::read<std::int32_t>( entry.ptr + SCHEMA( "C_SmokeGrenadeProjectile", "m_nSmokeEffectTickBegin"_hash ) ) > 0;
					break;
				case "weapon_decoy"_hash:
					already_detonated = memory::read<std::int32_t>( entry.ptr + SCHEMA( "C_DecoyProjectile", "m_nDecoyShotTick"_hash ) ) > 0;
					break;
				default:
					break;
				}

				if ( already_detonated )
				{
					continue;
				}
			}

			const auto initial_pos = memory::read<math::vector3>( entry.ptr + SCHEMA( "C_BaseCSGrenadeProjectile", "m_vInitialPosition"_hash ) );
			const auto initial_vel = memory::read<math::vector3>( entry.ptr + SCHEMA( "C_BaseCSGrenadeProjectile", "m_vInitialVelocity"_hash ) );

			if ( initial_vel.length_sqr( ) < 1.0f )
			{
				continue;
			}

			const auto saved_hash = this->m_weapon_hash;
			const auto saved_detonate = this->m_detonate_time;
			const auto saved_threshold = this->m_velocity_threshold;

			this->m_weapon_hash = wh;
			this->set_simulation_params( wh );

			in_flight_grenade gren{};
			gren.entity = entry.ptr;
			gren.weapon_hash = wh;
			gren.thrower_handle = thrower_handle;
			gren.is_enemy = is_enemy;
			gren.throw_time = now;
			gren.last_seen = now;
			gren.corrected = true;

			const auto ignore = thrower_handle ? systems::g_entities.lookup( thrower_handle ) : local.pawn;
			this->simulate( initial_pos, initial_vel, ignore ? ignore : local.pawn, gren.traj );

			this->m_weapon_hash = saved_hash;
			this->m_detonate_time = saved_detonate;
			this->m_velocity_threshold = saved_threshold;

			this->m_in_flight.push_back( std::move( gren ) );
		}

		for ( auto& gren : this->m_in_flight )
		{
			if ( gren.detonated || alive.contains( gren.entity ) )
			{
				continue;
			}

			if ( std::chrono::duration<float>( now - gren.last_seen ).count( ) >= k_missing_grace )
			{
				gren.detonated = true;
				gren.detonate_time = now;
			}
		}

		std::erase_if( this->m_in_flight, [ &now, &alive ]( const in_flight_grenade& g )
			{
				if ( !g.detonated )
				{
					return false;
				}

				return std::chrono::duration<float>( now - g.detonate_time ).count( ) > k_fade_duration && !alive.contains( g.entity );
			} );
	}

	std::uintptr_t projectile_trajectory::get_other_name( std::uint32_t schema_hash )
	{
		switch ( schema_hash )
		{
		case "C_HEGrenadeProjectile"_hash:    return "weapon_hegrenade"_hash;
		case "C_FlashbangProjectile"_hash:    return "weapon_flashbang"_hash;
		case "C_SmokeGrenadeProjectile"_hash: return "weapon_smokegrenade"_hash;
		case "C_MolotovProjectile"_hash:      return "weapon_molotov"_hash;
		case "C_DecoyProjectile"_hash:        return "weapon_decoy"_hash;
		default:                              return 0;
		}
	}

	void projectile_trajectory::correct_throw_angles( systems::input::usercmd* cmd, const systems::local::snapshot& local, std::uintptr_t weapon )
	{
		const auto throw_time = memory::read<float>( weapon + SCHEMA( "C_BaseCSGrenade", "m_fThrowTime"_hash ) );
		if ( throw_time <= 0.0f )
		{
			return;
		}

		auto strength = std::clamp( memory::read<float>( weapon + SCHEMA( "C_BaseCSGrenade", "m_flThrowStrength"_hash ) ), 0.0f, 1.0f );
		if ( std::abs( strength - 0.5f ) <= 0.1f )
		{
			strength = 0.5f;
		}

		const auto throw_vel = std::clamp( this->m_throw_velocity * 0.9f, 15.0f, 750.0f );
		const auto throw_speed = ( strength * 0.7f + 0.3f ) * throw_vel;

		math::vector3 desired_forward{};
		this->compute_desired_direction( desired_forward );

		const auto base = cmd->csgo_user_cmd.mutable_base( );
		if ( !base )
		{
			return;
		}

		auto predicted_velocity = systems::g_prediction.pre( ).networked_velocity;
		systems::g_prediction.simulate( cmd, local, [ & ]( ) { predicted_velocity = memory::read<math::vector3>( local.pawn + SCHEMA( "C_BaseEntity", "m_vecVelocity"_hash ) ); } );

		if ( predicted_velocity.length_sqr( ) < 1.0f )
		{
			return;
		}

		const auto vel_contribution = predicted_velocity * k_velocity_inherit;
		const auto vel_along = desired_forward * vel_contribution.dot( desired_forward );
		const auto vel_perp = vel_contribution - vel_along;
		const auto perp_len_sq = vel_perp.length_sqr( );

		if ( perp_len_sq >= throw_speed * throw_speed )
		{
			return;
		}

		const auto forward_component = std::sqrt( throw_speed * throw_speed - perp_len_sq );
		const auto total_along_desired = vel_contribution.dot( desired_forward ) + forward_component;

		if ( total_along_desired <= 0.0f )
		{
			return;
		}

		auto corrected = ( desired_forward * total_along_desired - vel_contribution ) * ( 1.0f / throw_speed );
		auto len = corrected.length( );

		if ( len < 0.001f )
		{
			return;
		}

		corrected = corrected * ( 1.0f / len );

		auto corrected_pitch = -std::asin( std::clamp( corrected.z, -1.0f, 1.0f ) ) * ( 180.0f / std::numbers::pi_v<float> );
		auto corrected_yaw = std::atan2( corrected.y, corrected.x ) * ( 180.0f / std::numbers::pi_v<float> );
		auto input_pitch = corrected_pitch;

		for ( auto i = 0; i < 16; ++i )
		{
			const auto bias = ( 90.0f - std::abs( input_pitch ) ) * 10.0f / 90.0f;
			input_pitch = corrected_pitch + bias;
		}

		input_pitch = std::clamp( input_pitch, -89.0f, 89.0f );

		base->mutable_viewangles( )->set_x( input_pitch );
		base->mutable_viewangles( )->set_y( corrected_yaw );
	}

	void projectile_trajectory::compute_desired_direction( math::vector3& desired_forward ) const
	{
		auto angles = systems::g_input.get_view_angles( );

		if ( angles.x > 90.0f )
		{
			angles.x -= 360.0f;
		}
		else if ( angles.x < -90.0f )
		{
			angles.x += 360.0f;
		}

		angles.x -= ( 90.0f - std::abs( angles.x ) ) * 10.0f / 90.0f;

		const auto pitch = angles.x * ( std::numbers::pi_v<float> / 180.0f );
		const auto yaw = angles.y * ( std::numbers::pi_v<float> / 180.0f );

		desired_forward =
		{
			std::cos( pitch ) * std::cos( yaw ),
			std::cos( pitch ) * std::sin( yaw ),
			-std::sin( pitch )
		};
	}

	void projectile_trajectory::compute_damage_at_endpoint( const trajectory& traj, std::vector<damage_info>& damages, const systems::local::snapshot& local ) const
	{
		damages.clear( );

		if ( !traj.valid || traj.end_tick < 0 )
		{
			return;
		}

		if ( this->m_weapon_hash != "weapon_hegrenade"_hash )
		{
			return;
		}

		constexpr auto base_damage{ 99.0f };
		constexpr auto damage_radius{ 350.0f };
		constexpr auto sigma{ 116.6f };

		const auto trace_start = traj.end_pos + math::vector3{ 0.0f, 0.0f, 1.0f };
		const auto players = systems::g_entities.get_by_type( systems::entities::type::player );

		for ( const auto& player : players )
		{
			if ( !player.ptr || player.ptr == local.controller )
			{
				continue;
			}

			if ( !memory::read<bool>( player.ptr + SCHEMA( "CCSPlayerController", "m_bPawnIsAlive"_hash ) ) )
			{
				continue;
			}

			const auto pawn_handle = memory::read<std::uint32_t>( player.ptr + SCHEMA( "CBasePlayerController", "m_hPawn"_hash ) );
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

			const auto game_scene_node = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
			if ( !game_scene_node )
			{
				continue;
			}

			if ( memory::read<bool>( game_scene_node + SCHEMA( "CGameSceneNode", "m_bDormant"_hash ) ) )
			{
				continue;
			}

			const auto player_origin = memory::read<math::vector3>( game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
			const auto center = player_origin + math::vector3{ 0.0f, 0.0f, 36.0f };
			const auto delta = center - traj.end_pos;
			const auto distance = std::sqrt( delta.length_sqr( ) );

			if ( distance > damage_radius )
			{
				continue;
			}

			const auto trace = systems::g_tracing.trace( trace_start, center, local.pawn );
			const auto visible = trace.fraction >= 0.97f || trace.hit_entity == pawn;

			if ( !visible )
			{
				continue;
			}

			const auto falloff = std::exp( -( distance * distance ) / ( 2.0f * sigma * sigma ) );
			auto adjusted_damage = base_damage * falloff;
			const auto armor = memory::read<std::int32_t>( pawn + SCHEMA( "C_CSPlayerPawn", "m_ArmorValue"_hash ) );

			if ( armor > 0 )
			{
				constexpr auto armor_ratio{ 1.2f * 0.5f };
				const auto damage_to_health = adjusted_damage * armor_ratio;
				const auto damage_to_armor = ( adjusted_damage - damage_to_health );

				if ( damage_to_armor <= static_cast< float >( armor ) )
				{
					adjusted_damage = damage_to_health;
				}
				else
				{
					adjusted_damage = adjusted_damage - static_cast< float >( armor );
				}
			}

			const auto final_damage = static_cast< int >( adjusted_damage );
			if ( final_damage <= 0 )
			{
				continue;
			}

			damages.push_back( { final_damage, final_damage >= health, center } );
		}
	}

	void projectile_trajectory::render_preview( xdraw::draw_list& draw_list, const systems::local::snapshot& local )
	{
		if ( !this->m_should_preview || !systems::g_frame_data.valid( ) )
		{
			return;
		}

		const auto weapon_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pWeaponServices"_hash ) );
		if ( !weapon_services )
		{
			return;
		}

		const auto weapon_handle = memory::read<std::uint32_t>( weapon_services + SCHEMA( "CPlayer_WeaponServices", "m_hActiveWeapon"_hash ) );
		if ( !weapon_handle || weapon_handle == 0xffffffff )
		{
			return;
		}

		const auto weapon = systems::g_entities.lookup( weapon_handle );
		if ( !weapon )
		{
			return;
		}

		math::vector3 origin{}, velocity{};
		this->setup_throw( local.pawn, weapon, origin, velocity );

		if ( settings::g_misc.m_projectile_trajectory.straight_throw.value )
		{
			const auto player_velocity = systems::g_prediction.pre( ).networked_velocity;
			if ( player_velocity.length_sqr( ) >= 1.0f )
			{
				velocity = velocity - player_velocity * k_velocity_inherit;

				const auto dir = velocity.normalized( );
				const auto vel_along = dir * ( player_velocity * k_velocity_inherit ).dot( dir );
				velocity = velocity + vel_along;
			}
		}

		trajectory frame_traj{};
		this->simulate( origin, velocity, local.pawn, frame_traj );

		if ( frame_traj.valid )
		{
			this->render_trajectory( draw_list, frame_traj, 1.0f, false, local );
		}
	}

	void projectile_trajectory::render_trajectory( xdraw::draw_list& draw_list, const trajectory& traj, float alpha, bool is_thrown, const systems::local::snapshot& local ) const
	{
		if ( !traj.valid || traj.points.size( ) < 2 )
		{
			return;
		}

		const auto& cfg = settings::g_misc.m_projectile_trajectory;
		const auto& points = traj.points;
		const auto count = points.size( );

		std::vector<damage_info> damages{};
		this->compute_damage_at_endpoint( traj, damages, local );

		const auto has_damage = !damages.empty( );
		const auto color = is_thrown ? ( has_damage ? cfg.will_deal_damage_thrown_color : cfg.thrown_color ) : ( has_damage ? cfg.will_deal_damage_held_color : cfg.held_color );

		std::vector<float> seg_points{};
		std::vector<xdraw::color> seg_colors{};

		seg_points.reserve( count * 2 );
		seg_colors.reserve( count );

		for ( auto i = 0ull; i < count; ++i )
		{
			const auto screen = systems::g_view.project( points[ i ] );
			if ( !systems::g_view.projection_valid( screen ) )
			{
				seg_points.push_back( std::numeric_limits<float>::quiet_NaN( ) );
				seg_points.push_back( std::numeric_limits<float>::quiet_NaN( ) );
				seg_colors.push_back( { 0, 0, 0, 0 } );
				continue;
			}

			const auto t = static_cast< float >( i ) / static_cast< float >( count - 1 );
			const auto fade = alpha * ( 1.0f - t * 0.6f );
			const auto a = static_cast< std::uint8_t >( static_cast< float >( color.value.a ) * fade );

			seg_points.push_back( screen.x );
			seg_points.push_back( screen.y );
			seg_colors.push_back( { color.value.r, color.value.g, color.value.b, a } );
		}

		const auto flush_to = [ & ]( xdraw::draw_list& target, const std::vector<xdraw::color>& colors, float thickness )
			{
				std::vector<float> run_pts{};
				std::vector<xdraw::color> run_cols{};

				for ( auto i = 0ull; i < colors.size( ); ++i )
				{
					const auto px = seg_points[ i * 2 ];
					const auto py = seg_points[ i * 2 + 1 ];

					if ( std::isnan( px ) )
					{
						if ( run_pts.size( ) >= 4 )
						{
							target.polyline_gradient( run_pts, run_cols, false, thickness );
						}

						run_pts.clear( );
						run_cols.clear( );
						continue;
					}

					run_pts.push_back( px );
					run_pts.push_back( py );
					run_cols.push_back( colors[ i ] );
				}

				if ( run_pts.size( ) >= 4 )
				{
					target.polyline_gradient( run_pts, run_cols, false, thickness );
				}
			};

		if ( cfg.glow && alpha > 0.0f )
		{
			auto& glow = xdraw::get_glow( xdraw::layer::middle );

			std::vector<xdraw::color> glow_colors{};
			glow_colors.reserve( seg_colors.size( ) );

			const auto strength = std::clamp( cfg.glow_strength.value, 0.0f, 1.0f );

			for ( const auto& c : seg_colors )
			{
				const auto ga = static_cast< std::uint8_t >( static_cast< float >( c.a ) * strength );
				glow_colors.push_back( { c.r, c.g, c.b, ga } );
			}

			flush_to( glow, glow_colors, 2.5f );
		}

		flush_to( draw_list, seg_colors, 1.5f );

		if ( traj.end_tick >= 0 )
		{
			const auto screen = systems::g_view.project( traj.end_pos );
			if ( systems::g_view.projection_valid( screen ) )
			{
				const auto det_a = static_cast< std::uint8_t >( static_cast< float >( color.value.a ) * alpha );
				const auto outline_a = static_cast< std::uint8_t >( 200.0f * alpha );

				draw_list.circle_filled( screen.x, screen.y, 3.5f, { 0, 0, 0, outline_a }, 16 );
				draw_list.circle_filled( screen.x, screen.y, 2.5f, { color.value.r, color.value.g, color.value.b, det_a }, 16 );
			}
		}
	}

} // namespace features::misc
