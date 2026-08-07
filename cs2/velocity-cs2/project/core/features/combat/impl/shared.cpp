#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>

namespace features::combat {

	namespace detail {

		struct bullet_trace_record
		{
			float enter_fraction;
			float exit_fraction;
			float damage_applied;
			int team_at_contact;
			std::uint16_t enter_contact_ix;
			std::uint16_t exit_contact_ix;
			std::uint8_t can_penetrate;
			std::uint8_t pad[ 3 ];
		};

	} // namespace detail

	void shared::penetration::prepare( std::uintptr_t weapon_vdata, std::uintptr_t weapon )
	{
		if ( !weapon_vdata || !weapon )
		{
			return;
		}

		this->m_weapon_data = weapon_data
		{
			.damage = static_cast< float >( memory::read<int>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_nDamage"_hash ) ) ),
			.penetration = memory::read<float>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flPenetration"_hash ) ),
			.range_modifier = memory::read<float>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flRangeModifier"_hash ) ),
			.range = memory::read<float>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flRange"_hash ) ),
			.armor_ratio = memory::read<float>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flArmorRatio"_hash ) ),
			.headshot_multiplier = memory::read<float>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flHeadshotMultiplier"_hash ) )
		};
	}

	shared::penetration::run_context shared::penetration::prepare_target( std::uintptr_t target_pawn, lagcomp::record* record ) const
	{
		run_context ctx{};
		ctx.target_pawn = target_pawn;
		ctx.record = record;
		if ( record && record->game_scene_node )
		{
			ctx.hitboxes = systems::g_hitboxes.query( record->game_scene_node );
		}

		ctx.target_armor = memory::read<int>( target_pawn + SCHEMA( "C_CSPlayerPawn", "m_ArmorValue"_hash ) );
		ctx.target_team = memory::read<int>( target_pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );

		if ( ctx.target_armor > 0 )
		{
			const auto services = memory::read<std::uintptr_t>( target_pawn + SCHEMA( "C_BasePlayerPawn", "m_pItemServices"_hash ) );
			if ( services )
			{
				ctx.has_helmet = memory::read<bool>( services + SCHEMA( "CCSPlayer_ItemServices", "m_bHasHelmet"_hash ) );
			}
		}

		ctx.scales =
		{
			.ct_head = CONVAR ("mp_damage_scale_ct_head")->get<float>( ),
			.t_head = CONVAR ("mp_damage_scale_t_head")->get<float>( ),
			.ct_body = CONVAR ("mp_damage_scale_ct_body")->get<float>( ),
			.t_body = CONVAR ("mp_damage_scale_t_body")->get<float>( )
		};

		ctx.armor_ratio = this->m_weapon_data.armor_ratio;
		ctx.headshot_multiplier = this->m_weapon_data.headshot_multiplier;

		return ctx;
	}

	bool shared::penetration::run( const math::vector3& start, const math::vector3& end, const run_context& ctx, std::uintptr_t local_pawn, int local_team, result& out ) const
	{
		if ( this->m_weapon_data.damage <= 0.0f )
		{
			return false;
		}

		const auto direction = ( end - start ).normalized( );
		const auto trace_delta = direction * this->m_weapon_data.range;

		auto filter = systems::g_tracing.make_filter( local_pawn, 0x1c300b, 3, 15 );
		// Rage scanning calls this hundreds of times in a frame. Reuse the large
		// trace buffer per worker instead of allocating and freeing 7 KB per point.
		thread_local systems::tracing::trace_data trace_storage{};
		trace_storage = {};
		auto* trace = &trace_storage;
		trace->array_pointer = &trace->elements;
		trace->hit_array_pointer = &trace->hit_elements;

		g_shared.m_current_autowall_record = ctx.record;
		g_shared.m_autowalling = true;

		// The hitbox-transform hook supplies this thread's record directly.
		// Do not swap the live entity pose: Present may read it concurrently.
		systems::g_tracing.setup_trace( trace, start, trace_delta, filter, 4, true );

		g_shared.m_autowalling = false;
		g_shared.m_current_autowall_record = nullptr;

		const auto num_hits = trace->num_hits;
		const auto hit_array = reinterpret_cast< std::uintptr_t >( trace->hit_array_pointer );

		if ( num_hits <= 0 )
		{
			out = {};
			return false;
		}

		const auto surface_array = reinterpret_cast< std::uintptr_t >( trace->array_pointer );

		memory::call<void> (PATTERN (patterns::trace_bullet), trace, this->m_weapon_data.damage, this->m_weapon_data.penetration, this->m_weapon_data.range_modifier, 4, local_team, static_cast<std::uintptr_t>(0));

		auto actual_hitbox{ -1 };
		auto closest_hitbox_fraction{ 1.0f };
		if ( ctx.record )
		{
			for ( const auto& hitbox : ctx.hitboxes )
			{
				if ( hitbox.bone < 0 || hitbox.bone >= ctx.record->bone_count )
				{
					continue;
				}

				const auto& bone = ctx.record->bones[ hitbox.bone ];
				auto fraction{ 1.0f };
				auto intersects{ false };

				if ( hitbox.radius > 0.001f )
				{
					const auto capsule_start = bone.rotation.rotate_vector( hitbox.mins ) + bone.position;
					const auto capsule_end = bone.rotation.rotate_vector( hitbox.maxs ) + bone.position;
					intersects = g_shared.ray_vs_capsule( start, trace_delta, capsule_start, capsule_end, hitbox.radius, fraction );
				}
				else
				{
					auto inverse = bone.rotation;
					inverse.x = -inverse.x;
					inverse.y = -inverse.y;
					inverse.z = -inverse.z;

					const auto local_origin = inverse.rotate_vector( start - bone.position );
					const auto local_delta = inverse.rotate_vector( trace_delta );
					auto entry{ 0.0f };
					auto exit{ 1.0f };

					const auto intersect_axis = [ & ]( float origin, float delta, float minimum, float maximum )
						{
							if ( std::fabsf( delta ) < 1.0e-8f )
							{
								return origin >= minimum && origin <= maximum;
							}

							auto first = ( minimum - origin ) / delta;
							auto second = ( maximum - origin ) / delta;
							if ( first > second ) std::swap( first, second );
							entry = std::max( entry, first );
							exit = std::min( exit, second );
							return entry <= exit;
						};

					intersects = intersect_axis( local_origin.x, local_delta.x, hitbox.mins.x, hitbox.maxs.x ) &&
						intersect_axis( local_origin.y, local_delta.y, hitbox.mins.y, hitbox.maxs.y ) &&
						intersect_axis( local_origin.z, local_delta.z, hitbox.mins.z, hitbox.maxs.z );
					fraction = entry;
				}

				if ( intersects && fraction < closest_hitbox_fraction )
				{
					closest_hitbox_fraction = fraction;
					actual_hitbox = hitbox.index;
				}
			}
		}

		auto penetrated{ false };

		for ( auto i = 0; i < num_hits; ++i )
		{
			auto hit = reinterpret_cast< detail::bullet_trace_record* >( hit_array + i * sizeof( detail::bullet_trace_record ) );
			const auto damage = *reinterpret_cast< float* >( reinterpret_cast< std::uintptr_t >( hit ) + 8 );

			if ( damage <= 0.0f )
			{
				break;
			}

			if ( ( hit->can_penetrate & 1 ) != 0 )
			{
				penetrated = true;

				if ( *reinterpret_cast< float* >( reinterpret_cast< std::uintptr_t >( hit ) + 4 ) == 1.0f )
				{
					break;
				}

				continue;
			}

			const auto trace_holder = surface_array + sizeof( systems::tracing::trace_array_element ) * ( hit->enter_contact_ix & 0x7fff );
			const auto hit_handle = memory::read<std::uint32_t>( trace_holder + 0x2c );
			const auto hit_entity = systems::g_entities.lookup( hit_handle );

			if ( !hit_entity || hit_entity != ctx.target_pawn )
			{
				continue;
			}

			if ( actual_hitbox < 0 )
			{
				continue;
			}

			out.hitbox = actual_hitbox;
			out.hitgroup = systems::g_hitboxes.hitgroup_from_hitbox( actual_hitbox );
			out.penetrated = penetrated;
			out.damage = damage;

			this->scale_damage( out.hitgroup, ctx.target_armor, ctx.has_helmet, ctx.target_team, ctx.armor_ratio, ctx.headshot_multiplier, ctx.scales, out.damage );

			return true;
		}

		out = {};
		return false;
	}

	bool shared::penetration::can( const math::vector3& start, const math::vector3& direction, float& out_damage, const systems::local::snapshot& local ) const
	{
		out_damage = 0.0f;

		if ( this->m_weapon_data.damage <= 0.0f || this->m_weapon_data.penetration <= 0.0f )
		{
			return false;
		}

		const auto local_team = memory::read<int>( local.pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
		const auto trace_delta = direction * this->m_weapon_data.range;

		auto filter = systems::g_tracing.make_filter( local.pawn, 0x1c300b, 3, 15 );
		thread_local systems::tracing::trace_data trace_storage{};
		trace_storage = {};
		auto* trace = &trace_storage;
		trace->array_pointer = &trace->elements;
		trace->hit_array_pointer = &trace->hit_elements;

		systems::g_tracing.setup_trace( trace, start, trace_delta, filter, 4, true );

		const auto num_hits = trace->num_hits;

		if ( num_hits <= 0 )
		{
			return false;
		}

		const auto hit_array = reinterpret_cast< std::uintptr_t >( trace->hit_array_pointer );

		memory::call<void> (PATTERN (patterns::trace_bullet), trace, this->m_weapon_data.damage, this->m_weapon_data.penetration, this->m_weapon_data.range_modifier, 4, local_team, static_cast<std::uintptr_t>(0));

		for ( auto i = 0; i < num_hits; ++i )
		{
			auto hit = reinterpret_cast< detail::bullet_trace_record* >( hit_array + i * sizeof( detail::bullet_trace_record ) );
			const auto damage = hit->damage_applied;

			if ( damage <= 0.0f )
			{
				break;
			}

			if ( ( hit->can_penetrate & 1 ) != 0 )
			{
				// Match run(): bit 0 marks a penetration record and an exit
				// fraction of 1 means the bullet did not make it through.
				if ( hit->exit_fraction == 1.0f )
				{
					break;
				}

				out_damage = damage;
				return true;
			}
		}

		return false;
	}

	float shared::penetration::get_max_damage( int hitgroup, int target_armor, bool has_helmet, int target_team ) const
	{
		if ( this->m_weapon_data.damage <= 0.0f )
		{
			return 0.0f;
		}

		const damage_scales scales
		{
			.ct_head = CONVAR ("mp_damage_scale_ct_head")->get<float> (),
			.t_head = CONVAR ("mp_damage_scale_t_head")->get<float> (),
			.ct_body = CONVAR ("mp_damage_scale_ct_body")->get<float> (),
			.t_body = CONVAR ("mp_damage_scale_t_body")->get<float> ()
		};

		auto damage = this->m_weapon_data.damage;
		this->scale_damage( hitgroup, target_armor, has_helmet, target_team, this->m_weapon_data.armor_ratio, this->m_weapon_data.headshot_multiplier, scales, damage );
		return damage;
	}

	void shared::penetration::scale_damage( int hitgroup, int armor, bool has_helmet, int team, float armor_ratio, float headshot_multiplier, const damage_scales& scales, float& damage ) const
	{
		const auto is_ct = ( team == 3 );
		const auto head_scale = is_ct ? scales.ct_head : scales.t_head;
		const auto body_scale = is_ct ? scales.ct_body : scales.t_body;

		switch ( hitgroup )
		{
		case 1:
			damage *= headshot_multiplier * head_scale;
			break;
		case 2:
		case 4:
		case 5:
		case 8:
			damage *= body_scale;
			break;
		case 3:
			damage *= 1.25f * body_scale;
			break;
		case 6:
		case 7:
			damage *= 0.75f * body_scale;
			break;
		default:
			break;
		}

		const auto is_head = ( hitgroup == 1 );
		const auto is_armored = ( hitgroup >= 1 && hitgroup <= 5 ) || ( hitgroup == 8 );

		if ( armor <= 0 || !is_armored || ( is_head && !has_helmet ) )
		{
			damage = std::floor( damage );
			return;
		}

		constexpr auto armor_bonus{ 0.5f };
		const auto armor_ratio_scaled = armor_ratio * 0.5f;

		auto damage_to_health = damage * armor_ratio_scaled;
		auto damage_to_armor = ( damage - damage_to_health ) * armor_bonus;

		if ( damage_to_armor > static_cast< float >( armor ) )
		{
			damage_to_health = damage - ( static_cast< float >( armor ) / armor_bonus );
		}

		damage = std::floor( damage_to_health );
	}

	bool shared::lagcomp::record::setup( std::uintptr_t pawn )
	{
		this->pawn = pawn;
		this->game_scene_node = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );

		if ( !this->game_scene_node )
		{
			return false;
		}

		this->bone_cache = memory::read<std::uintptr_t>( this->game_scene_node + SCHEMA( "CSkeletonInstance", "m_modelState"_hash ) + 0x80 );
		if ( !this->bone_cache )
		{
			return false;
		}

		this->bone_count = memory::read<int>( this->game_scene_node + SCHEMA( "CSkeletonInstance", "m_modelState"_hash ) + 0x8c );
		if ( this->bone_count <= 0 )
		{
			return false;
		}
		this->bone_count = std::min( this->bone_count, 128 );

		const auto abs_origin = memory::read<math::vector3>( this->game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
		const auto abs_rotation = memory::read<math::vector3>( this->game_scene_node + SCHEMA( "CGameSceneNode", "m_angAbsRotation"_hash ) );
		if ( !std::isfinite( abs_origin.x ) || !std::isfinite( abs_origin.y ) || !std::isfinite( abs_origin.z ) )
		{
			return false;
		}

		// Network origin is encoded. Records and bones must stay in the same
		// evaluated world-space coordinate system.
		this->origin = abs_origin;
		this->rotation = abs_rotation;

		this->simulation_time = memory::read<float>( pawn + SCHEMA( "C_BaseEntity", "m_flSimulationTime"_hash ) );

		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
		if ( !global_vars )
		{
			return false;
		}

		const auto backup_current_time = memory::read<float>( global_vars + 0x30 );
		const auto backup_tick_count = memory::read<int>( global_vars + 0x44 );
		memory::write<float>( global_vars + 0x30, this->simulation_time );
		memory::write<int>( global_vars + 0x44, cstypes::time_to_ticks( this->simulation_time ) );

		memory::call<void>(PATTERN (patterns::game_scene_node_set_mesh_group), this->game_scene_node, 0xfffff );
		memory::call<void>(PATTERN (patterns::game_scene_node_set_skeleton), this->game_scene_node, 0x100 );

		memory::write<int>( global_vars + 0x44, backup_tick_count );
		memory::write<float>( global_vars + 0x30, backup_current_time );

		this->bone_cache = memory::read<std::uintptr_t>( this->game_scene_node + SCHEMA( "CSkeletonInstance", "m_modelState"_hash ) + 0x80 );
		if ( !this->bone_cache )
		{
			return false;
		}

		std::memcpy( this->bones, reinterpret_cast< void* >( this->bone_cache ), sizeof( systems::bones::data ) * this->bone_count );

		this->tick = cstypes::time_to_ticks( this->simulation_time );
		this->valid = true;

		return true;
	}

	bool shared::lagcomp::record::is_valid( ) const
	{
		if ( !this->valid )
		{
			return false;
		}

		const auto local_pawn = systems::g_local.get( ).pawn;
		const auto net_channel = memory::call<std::uintptr_t>(PATTERN (patterns::get_net_channel), 0, 0 );
		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );

		if ( !local_pawn || !net_channel || !global_vars )
		{
			return false;
		}

		const auto max_unlag = [ ]
			{
				const auto server_limit = CONVAR ("sv_maxunlag")->get<float>( );
				const auto player_limit = CONVAR ("sv_maxunlag_player")->get<float>( );
				return player_limit > 0.0f ? std::min( server_limit, player_limit ) : server_limit;
			}( );

		const auto current_time = memory::read<float>( global_vars + 0x30 );
		const auto latency = memory::call_vfunc<float>( net_channel, 10, 0 );

		if ( !std::isfinite( max_unlag ) || !std::isfinite( current_time ) ||
			!std::isfinite( latency ) )
		{
			return false;
		}

		// This value is the effective ping for the selected flow in this build;
		// combining both flows double-counts latency and can erase the window.
		const auto budget = max_unlag - std::max( latency, 0.0f );

		return budget > 0.0f && this->simulation_time >= current_time - budget;
	}

	void shared::lagcomp::record::apply( )
	{
		if ( !this->valid || this->is_applied || !this->game_scene_node )
		{
			return;
		}

		this->bone_cache = memory::read<std::uintptr_t>( this->game_scene_node + SCHEMA( "CSkeletonInstance", "m_modelState"_hash ) + 0x80 );
		if ( !this->bone_cache )
		{
			return;
		}

		const auto size = sizeof( systems::bones::data ) * this->bone_count;
		std::memcpy( this->bones_backup, reinterpret_cast< void* >( this->bone_cache ), size );
		std::memcpy( reinterpret_cast< void* >( this->bone_cache ), this->bones, size );

		this->is_applied = true;
	}

	void shared::lagcomp::record::restore( )
	{
		if ( !this->valid || !this->is_applied || !this->bone_cache )
		{
			return;
		}

		const auto size = sizeof( systems::bones::data ) * this->bone_count;
		std::memcpy( reinterpret_cast< void* >( this->bone_cache ), this->bones_backup, size );

		this->is_applied = false;
	}

	void shared::lagcomp::run( )
	{
		std::unique_lock records_lock( this->m_records_mtx );

		const auto local = systems::g_local.get( );
		if ( !local.is_alive )
		{
			this->m_records.clear( );
			return;
		}

		std::unordered_set<std::uintptr_t> active{};

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

			const auto team = memory::read<int>( pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
			if ( !local.is_this_other_team( team ) )
			{
				continue;
			}

			active.insert( pawn );
		}

		std::erase_if( this->m_records, [ & ]( const auto& pair ) { return !active.contains( pair.first ); } );

		struct pending_record
		{
			std::uintptr_t pawn{};
			int simulation_tick{};
		};

		std::vector<pending_record> pending;
		pending.reserve( active.size( ) );

		for ( const auto& pawn : active )
		{
			const auto health = memory::read<int>( pawn + SCHEMA( "C_BaseEntity", "m_iHealth"_hash ) );
			if ( health <= 0 )
			{
				this->m_records.erase( pawn );
				continue;
			}

			auto& records = this->m_records[ pawn ];
			const auto simulation_time = memory::read<float>( pawn + SCHEMA( "C_BaseEntity", "m_flSimulationTime"_hash ) );
			const auto simulation_tick = cstypes::time_to_ticks( simulation_time );

			if ( records.empty( ) || simulation_tick > records.front( ).tick )
			{
				pending.push_back( { pawn, simulation_tick } );
			}

			while ( !records.empty( ) && !records.back( ).is_valid( ) )
			{
				records.pop_back( );
			}
		}

		if ( pending.empty( ) )
		{
			return;
		}

		for ( auto& p : pending )
		{
			record rec{};

			if ( rec.setup( p.pawn ) )
			{
				this->m_records[ p.pawn ].emplace_front( std::move( rec ) );
			}
		}

		for ( auto& [pawn, records] : this->m_records )
		{
			for ( auto& rec : records )
			{
				rec.was_valid = rec.is_valid( );
			}
		}
	}

	shared::lagcomp::record* shared::lagcomp::get_oldest_valid( std::uintptr_t pawn )
	{
		std::shared_lock records_lock( this->m_records_mtx );

		auto it = this->m_records.find( pawn );
		if ( it == this->m_records.end( ) || it->second.empty( ) )
		{
			return nullptr;
		}

		for ( auto rit = it->second.rbegin( ); rit != it->second.rend( ); ++rit )
		{
			if ( rit->is_valid( ) )
			{
				return &( *rit );
			}
		}

		return nullptr;
	}

	shared::lagcomp::record* shared::lagcomp::get_oldest_was_valid( std::uintptr_t pawn )
	{
		std::shared_lock records_lock( this->m_records_mtx );

		auto it = this->m_records.find( pawn );
		if ( it == this->m_records.end( ) || it->second.empty( ) )
		{
			return nullptr;
		}

		for ( auto rit = it->second.rbegin( ); rit != it->second.rend( ); ++rit )
		{
			if ( rit->was_valid )
			{
				return &( *rit );
			}
		}

		return nullptr;
	}

	std::optional<shared::lagcomp::visual_record> shared::lagcomp::get_oldest_was_valid_visual( std::uintptr_t pawn ) const
	{
		std::shared_lock records_lock( this->m_records_mtx );

		const auto it = this->m_records.find( pawn );
		if ( it == this->m_records.end( ) )
		{
			return std::nullopt;
		}

		for ( auto rit = it->second.rbegin( ); rit != it->second.rend( ); ++rit )
		{
			if ( !rit->was_valid )
			{
				continue;
			}

			visual_record out{};
			out.origin = rit->origin;
			for ( auto i = 0; i < 27; ++i )
			{
				out.bones[ i ] = rit->bones[ i ];
			}

			return out;
		}

		return std::nullopt;
	}

	std::vector<shared::lagcomp::record*> shared::lagcomp::get_valid_records( std::uintptr_t pawn )
	{
		std::shared_lock records_lock( this->m_records_mtx );

		std::vector<record*> result;

		auto it = this->m_records.find( pawn );
		if ( it == this->m_records.end( ) )
		{
			return result;
		}

		result.reserve( it->second.size( ) );

		for ( auto& rec : it->second )
		{
			if ( rec.is_valid( ) )
			{
				result.push_back( &rec );
			}
		}

		if ( result.empty( ) )
		{
			return result;
		}

		const auto max_ticks = std::clamp( settings::g_combat.m_lagcomp.max_backtrack_ticks.value, 1, static_cast< int >( rage::k_max_lagcomp_records ) );
		const auto newest_tick = result.front( )->tick;

		result.erase(
			std::remove_if( result.begin( ), result.end( ), [ newest_tick, max_ticks ]( const record* rec )
				{
					return ( newest_tick - rec->tick ) > max_ticks;
				} ),
			result.end( )
		);

		return result;
	}

	std::array<systems::bones::data, 27> shared::lagcomp::get_skeleton( const record& record ) const
	{
		std::array<systems::bones::data, 27> skeleton;

		if ( record.valid )
		{
			for ( auto i = 0; i < 27; ++i )
			{
				skeleton[ i ] = record.bones[ i ];
			}
		}

		return skeleton;
	}

	void shared::shoot_history::snapshot( std::uintptr_t local_pawn, std::uintptr_t weapon_services )
	{
		this->m_count = 0;

		if ( !weapon_services )
		{
			return;
		}

		{
			const auto net_client = addresses::globals::network_client_service;
			if ( !net_client )
			{
				return;
			}

			const auto tick_state = memory::call_vfunc<std::uintptr_t>( net_client, 23 );
			if ( !tick_state )
			{
				return;
			}

			this->m_server_tick = memory::read<int>( tick_state + 892 );
		}

		{
			const auto idx_raw = memory::read<int>( addresses::globals::frame_input_ring_idx );
			const auto idx = static_cast< unsigned >( idx_raw ) % 10u;
			const auto slot = addresses::globals::frame_input_ring_base + 40ull * idx;

			this->m_client_tick = memory::read<int>( slot + 0x0c );
			this->m_client_tick_frac = memory::read<float>( slot + 0x10 );
		}

		{
			const auto lerp_seconds = memory::call<float>(PATTERN (patterns::get_interp_amount), local_pawn );
			const auto lerp_ticks_f = lerp_seconds * 64.0f;
			const auto rounded = std::round( lerp_ticks_f );

			if ( std::fabs( lerp_ticks_f - rounded ) < 1e-4f )
			{
				this->m_lerp_ticks_int = static_cast< int >( rounded );
				this->m_lerp_ticks_frac = 0.0f;
			}
			else
			{
				this->m_lerp_ticks_int = static_cast< int >( std::floor( lerp_ticks_f ) );
				this->m_lerp_ticks_frac = lerp_ticks_f - static_cast< float >( this->m_lerp_ticks_int );
			}
		}

		const auto tail = memory::read<int>( weapon_services + 872 );
		const auto count = memory::read<int>( weapon_services + 876 );

		if ( count <= 0 || count > 32 || tail < 0 || tail >= 32 )
		{
			return;
		}

		for ( auto i = 0; i < count; ++i )
		{
			const auto idx = ( tail + i ) % 32;
			const auto off = weapon_services + 232 + 20ull * idx;

			auto& e = this->m_entries[ i ];
			e.tick = memory::read<int>( off + 0x00 );
			e.fraction = memory::read<float>( off + 0x04 );
			e.position.x = memory::read<float>( off + 0x08 );
			e.position.y = memory::read<float>( off + 0x0C );
			e.position.z = memory::read<float>( off + 0x10 );
		}

		this->m_count = count;
	}

	shared::shoot_history::eye_candidates shared::shoot_history::get_candidates( ) const
	{
		eye_candidates out{};

		if ( this->m_count < 1 )
		{
			return out;
		}

		constexpr auto ring_slot{ 0.03125f };

		const auto newest_valid_tick = this->m_client_tick - this->m_lerp_ticks_int;
		const auto oldest_valid_tick = this->m_client_tick - this->m_lerp_ticks_int - 1;

		auto first_valid{ -1 };
		auto last_valid{ -1 };

		for ( auto i = 0; i < this->m_count; ++i )
		{
			const auto t = this->m_entries[ i ].tick;
			if ( t > newest_valid_tick || t < oldest_valid_tick )
			{
				continue;
			}

			if ( first_valid == -1 )
			{
				first_valid = i;
			}

			last_valid = i;
		}

		if ( last_valid == -1 )
		{
			return out;
		}

		const auto& newest = this->m_entries[ last_valid ];
		out.entries[ 0 ].position = newest.position;
		out.entries[ 0 ].player_tick = newest.tick;
		out.entries[ 0 ].player_frac = newest.fraction + ring_slot;
		out.entries[ 0 ].lerp_ticks_int = this->m_lerp_ticks_int;
		out.entries[ 0 ].lerp_ticks_frac = this->m_lerp_ticks_frac;
		out.count = 1;

		if ( first_valid != last_valid )
		{
			const auto& oldest = this->m_entries[ first_valid ];
			const auto  delta = oldest.position - newest.position;

			if ( delta.x * delta.x + delta.y * delta.y + delta.z * delta.z >= 4.0f )
			{
				out.entries[ 1 ].position = oldest.position;
				out.entries[ 1 ].player_tick = oldest.tick;
				out.entries[ 1 ].player_frac = oldest.fraction + ring_slot;
				out.entries[ 1 ].lerp_ticks_int = this->m_lerp_ticks_int;
				out.entries[ 1 ].lerp_ticks_frac = this->m_lerp_ticks_frac;
				out.count = 2;
			}
		}

		return out;
	}

	void shared::update( )
	{
		this->m_ctx = {};

		const auto local = systems::g_local.get( );
		if ( !local.pawn )
		{
			return;
		}

		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
		const auto movement_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );

		if ( !global_vars || !movement_services )
		{
			return;
		}

		this->m_ctx.current_tick = memory::read<int>( global_vars + 0x44 );
		this->m_ctx.current_time = memory::read<float>( global_vars + 0x30 );
		this->m_ctx.is_scoped = memory::read<bool>( local.pawn + SCHEMA( "C_CSPlayerPawn", "m_bIsScoped"_hash ) );
		this->m_ctx.ticks_since_land = this->m_ctx.current_tick - memory::read<int>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_ModernJump"_hash ) + SCHEMA( "CCSPlayerModernJump", "m_nLastLandedTick"_hash ) );
		this->m_ctx.weapon_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pWeaponServices"_hash ) );

		if ( !this->m_ctx.weapon_services )
		{
			return;
		}

		const auto weapon_handle = memory::read<std::uint32_t>( this->m_ctx.weapon_services + SCHEMA( "CPlayer_WeaponServices", "m_hActiveWeapon"_hash ) );
		if ( !weapon_handle )
		{
			return;
		}

		this->m_ctx.weapon = systems::g_entities.lookup( weapon_handle );
		if ( !this->m_ctx.weapon )
		{
			return;
		}

		this->m_ctx.weapon_vdata = memory::read<std::uintptr_t>( this->m_ctx.weapon + SCHEMA( "C_BaseEntity", "m_nSubclassID"_hash ) + 0x8 );
		if ( !this->m_ctx.weapon_vdata )
		{
			return;
		}

		this->m_ctx.range = memory::read<float>( this->m_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flRange"_hash ) );
		this->m_ctx.weapon_type = memory::read<std::uint32_t>( this->m_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_WeaponType"_hash ) );
		this->m_ctx.item_def_idx = memory::read<std::uint16_t>( this->m_ctx.weapon + SCHEMA( "C_EconEntity", "m_AttributeManager"_hash ) + SCHEMA( "C_AttributeContainer", "m_Item"_hash ) + SCHEMA( "C_EconItemView", "m_iItemDefinitionIndex"_hash ) );
		this->m_ctx.num_bullets = memory::read<int>( this->m_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_nNumBullets"_hash ) );
		this->m_ctx.recoil_index = memory::read<float>( this->m_ctx.weapon + SCHEMA( "C_CSWeaponBase", "m_flRecoilIndex"_hash ) );
		this->m_ctx.weapon_max_speed = memory::read<float>( this->m_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flMaxSpeed"_hash ) );
		this->m_ctx.is_jump_scouting = ( systems::g_prediction.pre( ).flags & cstypes::entity_flags::on_ground ) == 0 && this->m_ctx.item_def_idx == cstypes::item_definition_index::weapon_ssg_08 && this->m_ctx.is_scoped;
		this->m_ctx.valid = true;

		this->m_pen.prepare( this->m_ctx.weapon_vdata, this->m_ctx.weapon );
	}

	void shared::invalidate_if_needed( )
	{
		const auto local = systems::g_local.get( );
		if ( !local.is_alive || !local.pawn || !local.controller )
		{
			this->m_ctx = {};
			this->m_last_shoot_tick = 0;
		}
	}

	std::uint32_t shared::get_spread_seed( const math::vector3& angles, int tick ) const
	{
		return memory::call<std::uint32_t>(PATTERN (patterns::get_tick_view_angles), nullptr, &angles, tick );
	}

	math::vector2 shared::calculate_spread( int seed, float accuracy, float spread, float recoil_index, int item_def_idx, int num_bullets ) const
	{
		math::vector2 out{};

		memory::call<void>(PATTERN (patterns::weapon_calculate_spread), static_cast< std::int16_t >( item_def_idx ), num_bullets, 0, static_cast< std::uint32_t >( seed + 1 ), accuracy, spread, recoil_index, &out.x, &out.y );

		return out;
	}

	math::vector3 shared::get_aim_punch( std::uintptr_t local_pawn ) const
	{
		math::vector3 out{};

		memory::call<void>(PATTERN (patterns::get_aim_punch), memory::read<std::uintptr_t>( local_pawn + SCHEMA( "C_CSPlayerPawn", "m_pAimPunchServices"_hash ) ), &out, 0u );

		return out;
	}

	float shared::calculate_hitchance( const math::vector3& shoot_position, const math::vector3& aim_angle, const systems::hitboxes::entry& hitbox, const systems::bones::data& bone, float inaccuracy, float spread, int samples ) const
	{
		const auto total = spread + inaccuracy;
		if ( total < 0.0001f )
		{
			return 1.0f;
		}

		if ( samples <= 0 )
		{
			return 0.0f;
		}

		const auto capsule_start = bone.rotation.rotate_vector( hitbox.mins ) + bone.position;
		const auto capsule_end = bone.rotation.rotate_vector( hitbox.maxs ) + bone.position;
		const auto is_capsule = hitbox.radius > 0.001f;
		auto inverse_rotation = bone.rotation;
		inverse_rotation.x = -inverse_rotation.x;
		inverse_rotation.y = -inverse_rotation.y;
		inverse_rotation.z = -inverse_rotation.z;
		const auto box_ray_origin = inverse_rotation.rotate_vector( shoot_position - bone.position );

		const auto ray_vs_box = [ & ]( const math::vector3& ray_direction )
		{
			const auto direction = inverse_rotation.rotate_vector( ray_direction );
			auto entry{ 0.0f };
			auto exit{ 1.0f };

			const auto intersect_axis = [ & ]( float origin, float delta, float minimum, float maximum )
			{
				if ( std::fabs( delta ) < 1.0e-8f )
				{
					return origin >= minimum && origin <= maximum;
				}

				auto first = ( minimum - origin ) / delta;
				auto second = ( maximum - origin ) / delta;
				if ( first > second )
				{
					std::swap( first, second );
				}

				entry = std::max( entry, first );
				exit = std::min( exit, second );
				return entry <= exit;
			};

			return intersect_axis( box_ray_origin.x, direction.x, hitbox.mins.x, hitbox.maxs.x ) &&
				intersect_axis( box_ray_origin.y, direction.y, hitbox.mins.y, hitbox.maxs.y ) &&
				intersect_axis( box_ray_origin.z, direction.z, hitbox.mins.z, hitbox.maxs.z );
		};

		math::vector3 forward{}, left{}, up{};
		math::helpers::angle_vectors_left( aim_angle, &forward, &left, &up );

		// Every candidate in a scan uses the same weapon state. The engine spread
		// function is much more expensive than the capsule test, so calculate each
		// deterministic seed once and reuse it for all candidate points.
		struct spread_cache
		{
			float inaccuracy{};
			float spread{};
			float recoil_index{};
			int item_def_idx{};
			int num_bullets{};
			int count{};
			bool initialized{};
			std::array<math::vector2, 256> values{};
		};

		thread_local spread_cache cache{};
		if ( !cache.initialized || cache.inaccuracy != inaccuracy || cache.spread != spread ||
			cache.recoil_index != this->m_ctx.recoil_index || cache.item_def_idx != this->m_ctx.item_def_idx ||
			cache.num_bullets != this->m_ctx.num_bullets )
		{
			cache.inaccuracy = inaccuracy;
			cache.spread = spread;
			cache.recoil_index = this->m_ctx.recoil_index;
			cache.item_def_idx = this->m_ctx.item_def_idx;
			cache.num_bullets = this->m_ctx.num_bullets;
			cache.count = 0;
			cache.initialized = true;
		}

		const auto cached_samples = std::min( samples, static_cast< int >( cache.values.size( ) ) );
		for ( auto i = cache.count; i < cached_samples; ++i )
		{
			cache.values[ i ] = this->calculate_spread( i, inaccuracy, spread, this->m_ctx.recoil_index, this->m_ctx.item_def_idx, this->m_ctx.num_bullets );
		}
		cache.count = std::max( cache.count, cached_samples );

		auto hits{ 0 };

		for ( auto i = 0; i < samples; ++i )
		{
			const auto calculated_spread = i < cached_samples
				? cache.values[ i ]
				: this->calculate_spread( i, inaccuracy, spread, this->m_ctx.recoil_index, this->m_ctx.item_def_idx, this->m_ctx.num_bullets );
			const auto direction = forward + ( left * calculated_spread.x ) + ( up * calculated_spread.y );
			const auto ray_end = direction.normalized( ) * 8192.0f;

			auto hit{ false };
			if ( is_capsule )
			{
				auto fraction{ 1.0f };
				hit = this->ray_vs_capsule( shoot_position, ray_end, capsule_start, capsule_end, hitbox.radius, fraction );
			}
			else
			{
				hit = ray_vs_box( ray_end );
			}

			if ( hit )
			{
				++hits;
			}
		}

		return static_cast< float >( hits ) / static_cast< float >( samples );
	}

	math::vector3 shared::find_spread_correction( const math::vector3& aim_angle, int tick ) const
	{
		for ( auto i = 0; i < 720; i++ )
		{
			const auto test_angles = math::vector3{ static_cast< float >( i ) / 2.0f, aim_angle.y, 0.0f };
			const auto seed = this->get_spread_seed( test_angles, tick );
			const auto spread = this->calculate_spread( seed, this->m_ctx.inaccuracy, this->m_ctx.spread, this->m_ctx.recoil_index, this->m_ctx.item_def_idx, this->m_ctx.num_bullets );

			auto adj_angle = aim_angle;
			adj_angle.x += math::helpers::rad_to_deg( std::atan( std::sqrt( spread.x * spread.x + spread.y * spread.y ) ) );
			adj_angle.z = -math::helpers::rad_to_deg( std::atan2( spread.x, spread.y ) );

			if ( this->get_spread_seed( adj_angle, tick ) == seed )
			{
				return adj_angle;
			}
		}

		return {};
	}

	math::vector3 shared::get_eye_position( std::uintptr_t local_pawn ) const
	{
		const auto game_scene_node = memory::read<std::uintptr_t>( local_pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		const auto origin = memory::read<math::vector3>( game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
		const auto view_offset = memory::read<math::vector3>( local_pawn + SCHEMA( "C_BaseModelEntity", "m_vecViewOffset"_hash ) );
		return origin + view_offset;
	}

	math::vector3 shared::get_shoot_position( ) const
	{
		math::vector3 out{};
		memory::call_vfunc<void>( this->m_ctx.weapon_services, 29, reinterpret_cast< std::uintptr_t >( &out ) );
		return out;
	}

	math::vector3 shared::get_interpolated_shoot_position( std::uintptr_t local_pawn, bool newest ) const
	{
		const auto ws = this->m_ctx.weapon_services;
		const auto head = memory::read<int>( ws + 872 );
		const auto count = memory::read<int>( ws + 876 );

		if ( count < 1 )
		{
			return this->get_shoot_position( );
		}

		if ( newest )
		{
			const auto newest_idx = ( head + count - 1 ) % 32;
			const auto newest_off = ws + 232 + 20ull * newest_idx;
			return memory::read<math::vector3>( newest_off + 8 );
		}

		if ( count < 2 )
		{
			return this->get_shoot_position( );
		}

		const auto interp = memory::call<float>(PATTERN (patterns::get_interp_amount), local_pawn );
		const auto newest_idx = ( head + count - 1 ) % 32;
		const auto newest_off = ws + 232 + 20ull * newest_idx;
		const auto newest_tick = memory::read<int>( newest_off );
		const auto newest_frac = memory::read<float>( newest_off + 4 );

		const auto target = cstypes::tick_fraction{ newest_tick, newest_frac }.subtract_value( interp * 64.0f );

		for ( auto i = 0; i < count - 1; ++i )
		{
			const auto idx_a = ( head + static_cast< std::size_t >( i ) ) % 32;
			const auto idx_b = ( head + static_cast< std::size_t >( i ) + 1 ) % 32;

			const auto a_off = ws + 232 + 20ull * idx_a;
			const auto b_off = ws + 232 + 20ull * idx_b;

			const auto a_tick = memory::read<int>( a_off );
			const auto a_frac = memory::read<float>( a_off + 4 );
			const auto b_tick = memory::read<int>( b_off );
			const auto b_frac = memory::read<float>( b_off + 4 );

			const auto a_before = a_tick < target.tick || ( a_tick == target.tick && a_frac <= target.frac );
			if ( !a_before )
			{
				break;
			}

			const auto b_after = b_tick > target.tick || ( b_tick == target.tick && b_frac >= target.frac );
			if ( !b_after )
			{
				continue;
			}

			const auto a_pos = memory::read<math::vector3>( a_off + 8 );
			const auto b_pos = memory::read<math::vector3>( b_off + 8 );

			const auto span = cstypes::tick_fraction{ b_tick, b_frac }.subtract( { a_tick, a_frac } );
			const auto partial = target.subtract( { a_tick, a_frac } );

			const auto total_f = static_cast< float >( span.tick ) + span.frac;
			const auto partial_f = static_cast< float >( partial.tick ) + partial.frac;

			const auto t = total_f > 0.0f ? partial_f / total_f : 0.0f;

			return a_pos + ( b_pos - a_pos ) * t;
		}

		return this->get_shoot_position( );
	}

	int shared::calculate_stop_ticks( const math::vector3& velocity, float max_speed, std::uintptr_t local_pawn ) const
	{
		auto vel = velocity;
		vel.z = 0.0f;

		auto ticks{ 0 };
		const auto sv_friction = CONVAR ("sv_friction")->get<float>( );
		const auto sv_stopspeed = CONVAR ("sv_stopspeed")->get<float>( );
		const auto sv_accelerate = CONVAR ("sv_accelerate")->get<float>( );
		const auto surface_friction = systems::g_prediction.pre( ).surface_friction;
		const auto accurate_threshold = max_speed * 0.34f;

		const auto is_scoped = this->m_ctx.is_scoped;
		auto max_move_speed{ 250.0f };

		if ( is_scoped && local_pawn )
		{
			const auto movement_services = memory::read<std::uintptr_t>( local_pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );
			if ( movement_services )
			{
				max_move_speed = memory::read<float>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_flMaxspeed"_hash ) );
			}
		}

		while ( vel.length_2d( ) > accurate_threshold && ticks < 15 )
		{
			const auto speed = vel.length_2d( );
			if ( speed <= 0.0f )
			{
				break;
			}

			const auto control = std::fmaxf( speed, sv_stopspeed );
			const auto drop = sv_friction * surface_friction * control * cstypes::tick_interval;
			auto new_speed = std::fmaxf( speed - drop, 0.0f );

			auto accel = sv_accelerate;

			if ( is_scoped )
			{
				const auto weapon_ratio = std::fminf( 1.0f, max_speed / 250.0f );
				const auto scoped_max = std::fmaxf( 250.0f, max_move_speed ) * weapon_ratio * 0.52f;

				if ( new_speed > scoped_max - 5.0f )
				{
					const auto t = 1.0f - std::fmaxf( 0.0f, new_speed - ( scoped_max - 5.0f ) ) / std::fmaxf( 0.01f, 5.0f );
					accel *= std::clamp( t, 0.0f, 1.0f );
				}
			}

			const auto accel_speed = std::fminf( accel * max_speed * surface_friction * cstypes::tick_interval, new_speed );
			new_speed = std::fmaxf( new_speed - accel_speed, 0.0f );

			vel *= ( new_speed / speed );
			ticks++;
		}

		return ticks;
	}

	float shared::get_spread( ) const
	{
		static const auto get_spread = PATTERN( patterns::get_spread );
		return memory::call<float>( get_spread, this->m_ctx.weapon );
	}

	float shared::get_inaccuracy( bool update_accuracy_penalty ) const
	{
		const auto accuracy_state_begin = SCHEMA( "C_CSWeaponBase", "m_flTurningInaccuracyDelta"_hash );
		const auto accuracy_state_end = SCHEMA( "C_CSWeaponBase", "m_flRecoilIndex"_hash );
		if ( !this->m_ctx.weapon || accuracy_state_begin <= 0 || accuracy_state_end < accuracy_state_begin )
		{
			return 0.0f;
		}

		const auto accuracy_state_size = static_cast< std::size_t >( accuracy_state_end - accuracy_state_begin ) + sizeof( float );
		if ( accuracy_state_size > 0x100 )
		{
			return 0.0f;
		}

		std::vector<std::uint8_t> backup( accuracy_state_size );
		std::memcpy( backup.data( ), reinterpret_cast< const void* >( this->m_ctx.weapon + accuracy_state_begin ), accuracy_state_size );

		if ( update_accuracy_penalty )
		{
			memory::call<void>(PATTERN (patterns::weapon_update_accuracy), this->m_ctx.weapon );
		}

		static const auto get_inaccuracy = PATTERN( patterns::get_inaccuracy );
		const auto inaccuracy = memory::call<float>(
			get_inaccuracy, this->m_ctx.weapon,
			static_cast<float*>( nullptr ), static_cast<float*>( nullptr ) );

		std::memcpy( reinterpret_cast< void* >( this->m_ctx.weapon + accuracy_state_begin ), backup.data( ), accuracy_state_size );

		return inaccuracy;
	}

	float shared::get_inaccuracy_at_velocity( std::uintptr_t local_pawn, const math::vector3& velocity ) const
	{
		const auto accuracy_state_begin = SCHEMA( "C_CSWeaponBase", "m_flTurningInaccuracyDelta"_hash );
		const auto accuracy_state_end = SCHEMA( "C_CSWeaponBase", "m_flRecoilIndex"_hash );
		if ( !this->m_ctx.weapon || !local_pawn || accuracy_state_begin <= 0 || accuracy_state_end < accuracy_state_begin )
		{
			return 0.0f;
		}

		const auto accuracy_state_size = static_cast< std::size_t >( accuracy_state_end - accuracy_state_begin ) + sizeof( float );
		if ( accuracy_state_size > 0x100 )
		{
			return 0.0f;
		}

		std::vector<std::uint8_t> backup( accuracy_state_size );
		std::memcpy( backup.data( ), reinterpret_cast< const void* >( this->m_ctx.weapon + accuracy_state_begin ), accuracy_state_size );

		const auto old_velocity = memory::read<math::vector3>( local_pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_hash ) );
		const auto old_eflags = memory::read<std::uint32_t>( local_pawn + SCHEMA( "C_BaseEntity", "m_iEFlags"_hash ) );

		memory::write( local_pawn + SCHEMA( "C_BaseEntity", "m_iEFlags"_hash ), old_eflags & ~0x1000u );
		memory::write( local_pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_hash ), velocity );

		memory::call<void>(PATTERN (patterns::weapon_update_accuracy), this->m_ctx.weapon );

		static const auto get_inaccuracy = PATTERN( patterns::get_inaccuracy );
		const auto inaccuracy = memory::call<float>(
			get_inaccuracy, this->m_ctx.weapon,
			static_cast<float*>( nullptr ), static_cast<float*>( nullptr ) );

		memory::write( local_pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_hash ), old_velocity );
		memory::write( local_pawn + SCHEMA( "C_BaseEntity", "m_iEFlags"_hash ), old_eflags );

		std::memcpy( reinterpret_cast< void* >( this->m_ctx.weapon + accuracy_state_begin ), backup.data( ), accuracy_state_size );

		return inaccuracy;
	}

	float shared::get_air_inaccuracy( float vertical_speed, float jump_initial, float jump_apex ) const
	{
		constexpr auto sqrt_threshold{ 17.37795666f };
		const auto val = ( ( std::sqrtf( std::fabsf( vertical_speed ) ) - sqrt_threshold * 0.25f ) * ( jump_initial - jump_apex ) ) / ( sqrt_threshold * 0.75f ) + jump_apex;
		return std::clamp( val, 0.0f, jump_initial * 2.0f );
	}

	bool shared::can_shoot( systems::input::usercmd* cmd, std::uintptr_t local_controller, bool check_next_attack ) const
	{
		if ( this->m_ctx.weapon_type != cstypes::weapon_type::knife )
		{
			if ( memory::read<bool>( this->m_ctx.weapon + SCHEMA( "C_CSWeaponBase", "m_bInReload"_hash ) ) )
			{
				return false;
			}

			if ( memory::read<int>( this->m_ctx.weapon + SCHEMA( "C_BasePlayerWeapon", "m_iClip1"_hash ) ) <= 0 )
			{
				return false;
			}
		}

		if ( !check_next_attack )
		{
			return true;
		}

		const auto tick_base = memory::read<int>( local_controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) );
		const auto base_cmd = cmd->csgo_user_cmd.base( );
		const auto client_tick = base_cmd ? base_cmd->client_tick( ) : tick_base;
		const auto next_primary = memory::read<int>( this->m_ctx.weapon + SCHEMA( "C_BasePlayerWeapon", "m_nNextPrimaryAttackTick"_hash ) );

		if ( this->m_ctx.weapon_type == cstypes::weapon_type::knife )
		{
			const auto next_secondary = memory::read<int>( this->m_ctx.weapon + SCHEMA( "C_BasePlayerWeapon", "m_nNextSecondaryAttackTick"_hash ) );
			return tick_base >= this->m_last_shoot_tick + 2 && ( client_tick >= next_primary || client_tick >= next_secondary );
		}

		return tick_base >= this->m_last_shoot_tick + 2 && client_tick >= next_primary;
	}

	bool shared::is_max_accuracy( float inaccuracy ) const
	{
		const auto& prestate = systems::g_prediction.pre( );
		const auto on_ground = ( prestate.flags & 1 ) != 0;
		const auto is_ducking = ( prestate.flags & 4 ) != 0;
		const auto speed = prestate.networked_velocity.length_2d( );

		if ( on_ground )
		{
			if ( this->m_ctx.weapon_type == cstypes::weapon_type::sniper )
			{
				if ( !this->m_ctx.is_scoped )
				{
					return false;
				}

				if ( is_ducking )
				{
					const auto rounded = std::floorf( inaccuracy * 300.0f ) / 300.0f;
					return rounded < inaccuracy;
				}

				if ( speed <= 0.1f )
				{
					const auto rounded = std::floorf( inaccuracy * 170.0f ) / 170.0f;
					return rounded < inaccuracy;
				}

				return false;
			}

			return speed <= this->m_ctx.weapon_max_speed * 0.34f;
		}

		const auto inaccuracy_jump_apex = memory::read<float>( this->m_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flInaccuracyJumpApex"_hash ) );
		const auto accuracy_penalty = memory::read<float>( this->m_ctx.weapon + SCHEMA( "C_CSWeaponBase", "m_fAccuracyPenalty"_hash ) );
		const auto min_air_inaccuracy = accuracy_penalty + inaccuracy_jump_apex;

		constexpr auto tolerance{ 0.001f };
		return inaccuracy <= min_air_inaccuracy + tolerance;
	}

	math::vector3 shared::simulate_aim_punch( int recoil_index ) const
	{
		if ( recoil_index <= 0 || !this->m_ctx.valid )
		{
			return {};
		}

		const auto weapon_mode = memory::read<int>( this->m_ctx.weapon + SCHEMA( "C_CSWeaponBase", "m_weaponMode"_hash ) );
		const auto cycle_time = memory::read<float>( this->m_ctx.weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_flCycleTime"_hash ) );

		constexpr auto decay_rate{ 4.5f };
		constexpr auto decay2_exp{ 8.0f };
		constexpr auto decay2_lin{ 18.0f };
		constexpr auto recoil_scale{ 2.0f };

		math::vector3 punch{};
		math::vector3 punch_vel{};

		auto hybrid_decay = [ ]( math::vector3& v, float exp, float lin, float dt )
			{
				v *= std::expf( -exp * dt );

				const auto mag = v.length( );
				if ( mag > lin * dt )
				{
					v *= ( 1.0f - ( lin * dt ) / mag );
				}
				else
				{
					v = {};
				}
			};

		for ( auto i = 0; i < recoil_index; ++i )
		{
			float angle{}, magnitude{};
			memory::call<void>(PATTERN (patterns::weapon_get_recoil_offset), addresses::globals::weapon_recoil_data, this->m_ctx.weapon, weapon_mode, i, &angle, &magnitude );

			math::vector3 offset{};
			offset.x = std::cosf( math::helpers::deg_to_rad( angle ) ) * magnitude;
			offset.y = std::sinf( math::helpers::deg_to_rad( angle ) ) * magnitude;

			punch_vel -= offset;

			for ( auto time = 0.0f; time <= cycle_time; time += cstypes::tick_interval )
			{
				hybrid_decay( punch, decay2_exp, decay2_lin, cstypes::tick_interval );

				punch += punch_vel * cstypes::tick_interval * 0.5f;
				punch_vel *= std::expf( -decay_rate * cstypes::tick_interval );

				if ( punch_vel.length( ) < 0.03125f )
				{
					punch_vel = {};
				}

				punch += punch_vel * cstypes::tick_interval * 0.5f;
			}
		}

		return punch * recoil_scale;
	}

	bool shared::ray_vs_capsule( const math::vector3& ray_origin, const math::vector3& ray_dir, const math::vector3& capsule_a, const math::vector3& capsule_b, float radius, float& out_fraction ) const
	{
		const auto ab = capsule_b - capsule_a;
		const auto ab_sq = ab.dot( ab );
		const auto oc = ray_origin - capsule_a;
		const auto dir_sq = ray_dir.dot( ray_dir );

		if ( dir_sq < 1e-8f )
		{
			return false;
		}

		auto best_t{ 1.0f };
		auto hit{ false };

		if ( ab_sq > 1e-8f )
		{
			const float m = ab.dot( ray_dir ) / ab_sq;
			const float n = ab.dot( oc ) / ab_sq;

			const auto d_perp = ray_dir - ab * m;
			const auto oc_perp = oc - ab * n;

			const auto a = d_perp.dot( d_perp );
			const auto half_b = d_perp.dot( oc_perp );
			const auto c = oc_perp.dot( oc_perp ) - radius * radius;

			if ( a > 1e-8f )
			{
				const auto disc = half_b * half_b - a * c;
				if ( disc >= 0.0f )
				{
					const auto sqrt_disc = std::sqrt( disc );

					for ( int r = 0; r < 2; r++ )
					{
						const auto t = ( -half_b + ( r == 0 ? -sqrt_disc : sqrt_disc ) ) / a;
						if ( t < 0.0f || t >= best_t )
						{
							continue;
						}

						const auto s = m * t + n;
						if ( s >= 0.0f && s <= 1.0f )
						{
							best_t = t;
							hit = true;
							break;
						}
					}
				}
			}
		}

		const math::vector3 caps[ ]{ capsule_a, capsule_b };

		for ( int i = 0; i < 2; i++ )
		{
			const auto co = ray_origin - caps[ i ];
			const auto half_b = co.dot( ray_dir );
			const auto c = co.dot( co ) - radius * radius;
			const auto disc = half_b * half_b - dir_sq * c;

			if ( disc < 0.0f )
			{
				continue;
			}

			const auto sqrt_disc = std::sqrt( disc );

			for ( int r = 0; r < 2; r++ )
			{
				const auto t = ( -half_b + ( r == 0 ? -sqrt_disc : sqrt_disc ) ) / dir_sq;
				if ( t < 0.0f || t >= best_t )
				{
					continue;
				}

				if ( ab_sq > 1e-8f )
				{
					const auto hit_point = ray_origin + ray_dir * t - caps[ i ];
					const auto sign = i == 0 ? -1.0f : 1.0f;

					if ( sign * ab.dot( hit_point ) < 0.0f )
					{
						continue;
					}
				}

				best_t = t;
				hit = true;
				break;
			}
		}

		if ( hit )
		{
			out_fraction = best_t;
		}

		return hit;
	}

} // namespace features::combat
