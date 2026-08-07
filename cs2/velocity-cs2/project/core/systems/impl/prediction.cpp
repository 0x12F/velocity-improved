#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/diag.hpp>
#include <utilities/logging/logging.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>
#include "../systems.hpp"

namespace systems {

	namespace detail {

		class state_guard
		{
		public:
			state_guard( ) = default;
			~state_guard( ) { restore( ); }

			state_guard( const state_guard& ) = delete;
			state_guard& operator=( const state_guard& ) = delete;

			template <typename T>
			void save( std::uintptr_t address )
			{
				auto value = memory::read<T>( address );
				this->m_entries.emplace_back( entry{ address, std::vector<std::uint8_t>( sizeof( T ) ) } );
				std::memcpy( this->m_entries.back( ).data.data( ), &value, sizeof( T ) );
			}

			void save_raw( std::uintptr_t address, std::size_t size )
			{
				this->m_entries.emplace_back( entry{ address, std::vector<std::uint8_t>( size ) } );
				std::memcpy( this->m_entries.back( ).data.data( ), reinterpret_cast< void* >( address ), size );
			}

			void restore( )
			{
				for ( auto it = this->m_entries.rbegin( ); it != this->m_entries.rend( ); ++it )
				{
					std::memcpy( reinterpret_cast< void* >( it->address ), it->data.data( ), it->data.size( ) );
				}

				this->m_entries.clear( );
			}

		private:
			struct entry
			{
				std::uintptr_t address{};
				std::vector<std::uint8_t> data{};
			};

			std::vector<entry> m_entries;
		};

	} // namespace detail

	void prediction::capture_prestate( std::uintptr_t local_pawn, std::uintptr_t movement_services )
	{
		this->m_prestate.flags = memory::read<std::uint32_t>( local_pawn + SCHEMA( "C_BaseEntity", "m_fFlags"_hash ) );
		this->m_prestate.networked_velocity = memory::read<math::vector3>( local_pawn + SCHEMA( "C_BaseEntity", "m_vecVelocity"_hash ) );
		this->m_prestate.velocity = memory::read<math::vector3>( local_pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_hash ) );
		this->m_prestate.stamina = memory::read<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_flStamina"_hash ) );
		this->m_prestate.surface_friction = memory::read<float>( movement_services + SCHEMA( "CPlayer_MovementServices_Humanoid", "m_flSurfaceFriction"_hash ) );

		this->m_prestate.last_movement_impulses.x = memory::read<float>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_flCmdForwardMove"_hash ) );
		this->m_prestate.last_movement_impulses.y = memory::read<float>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_flCmdLeftMove"_hash ) );
		this->m_prestate.last_movement_impulses.z = memory::read<float>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_flCmdUpMove"_hash ) );

		const auto game_scene_node = memory::read<std::uintptr_t>( local_pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		if ( game_scene_node )
		{
			this->m_prestate.origin = memory::read<math::vector3>( game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
			// m_vecOrigin is an encoded network-origin object, not a vector3.
			// Use the evaluated position anywhere a plain world-space vector is needed.
			this->m_prestate.networked_origin = this->m_prestate.origin;
		}
	}

	bool prediction::simulate( input::usercmd* cmd, const systems::local::snapshot& local, const std::function<void( )>& fn )
	{
		std::lock_guard simulation_lock( this->m_simulation_mtx );

		static const auto prediction_set_state = PATTERN( patterns::prediction_set_state );
		static const auto prediction_set_pawn = PATTERN( patterns::prediction_set_pawn );
		static const auto prediction_setup_move = PATTERN( patterns::prediction_setup_move );
		static const auto prediction_process_movement = PATTERN( patterns::prediction_process_movement );
		static const auto prediction_finish_move = PATTERN( patterns::prediction_finish_move );
		static const auto prediction_reset_pawn = PATTERN( patterns::prediction_reset_pawn );

		if ( !prediction_set_state || !prediction_set_pawn || !prediction_setup_move ||
			!prediction_process_movement || !prediction_finish_move || !prediction_reset_pawn )
		{
			static bool logged = false;
			if ( !logged )
			{
				logging::console::print( xs( "[prediction] disabled: required helper unavailable" ) );
				logged = true;
			}

			return false;
		}

		const auto get_active_weapon = [ ]( std::uintptr_t pawn ) -> std::pair<std::uintptr_t, std::uintptr_t>
			{
				const auto weapon_services = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BasePlayerPawn", "m_pWeaponServices"_hash ) );
				if ( !weapon_services )
				{
					return {};
				}

				const auto weapon_handle = memory::read<std::uint32_t>( weapon_services + SCHEMA( "CPlayer_WeaponServices", "m_hActiveWeapon"_hash ) );
				if ( !weapon_handle )
				{
					return { weapon_services, 0 };
				}

				return { weapon_services, systems::g_entities.lookup( weapon_handle ) };
			};

		const auto game_scene_node = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		const auto movement_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );
		const auto aim_punch_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_CSPlayerPawn", "m_pAimPunchServices"_hash ) );
		const auto [weapon_services, weapon] = get_active_weapon( local.pawn );

		if ( !game_scene_node || !movement_services || !aim_punch_services || !weapon )
		{
			return false;
		}

		const auto global_vars = memory::safe_read<std::uintptr_t>( addresses::globals::global_vars ).value_or( 0 );
		const auto cmd_ptr = reinterpret_cast< std::uintptr_t >( cmd );
		const auto old_slot = memory::safe_read<std::uintptr_t>( addresses::globals::source2client_prediction + 56 ).value_or( 0 );
		const auto pred_state = memory::safe_read<std::uintptr_t>( addresses::globals::prediction_state ).value_or( 0 );

		if ( !global_vars || !pred_state )
		{
			return false;
		}

		const auto encoded_origin_offset = SCHEMA( "CGameSceneNode", "m_vecOrigin"_hash );
		const auto rotation_offset = SCHEMA( "CGameSceneNode", "m_angRotation"_hash );
		const auto encoded_origin_size = rotation_offset - encoded_origin_offset;
		if ( encoded_origin_offset <= 0 || encoded_origin_size <= 0 || encoded_origin_size > 0x80 )
		{
			static bool logged = false;
			if ( !logged )
			{
				logging::console::print( xs( "[prediction] disabled: invalid encoded origin layout" ) );
				logged = true;
			}

			return false;
		}

		detail::state_guard guard;

		guard.save<float>( global_vars + 48 );
		guard.save<float>( global_vars + 52 );
		guard.save<int>( global_vars + 68 );
		guard.save<float>( global_vars + 80 );
		guard.save<std::uint32_t>( global_vars + 88 );

		guard.save<std::uint32_t>( addresses::globals::prediction_seed );
		guard.save<std::uintptr_t>( addresses::globals::simulation_player );
		guard.save<std::uintptr_t>( addresses::globals::prediction_player );

		if ( old_slot )
		{
			guard.save<std::uint8_t>( old_slot + 140 );
		}

		guard.save<int>( local.controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) );

		guard.save<std::uint32_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_fFlags"_hash ) );
		guard.save<math::vector3>( local.pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_hash ) );
		guard.save<math::vector3>( local.pawn + SCHEMA( "C_BaseEntity", "m_vecVelocity"_hash ) );
		guard.save<math::vector3>( local.pawn + SCHEMA( "C_BaseEntity", "m_vecBaseVelocity"_hash ) );
		guard.save<float>( local.pawn + SCHEMA( "C_BaseEntity", "m_flFriction"_hash ) );
		guard.save<float>( local.pawn + SCHEMA( "C_BaseEntity", "m_flGravityScale"_hash ) );
		guard.save<std::uint32_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_hGroundEntity"_hash ) );
		guard.save<float>( local.pawn + SCHEMA( "C_BaseEntity", "m_flSimulationTime"_hash ) );
		guard.save<int>( local.pawn + SCHEMA( "C_BaseEntity", "m_nSimulationTick"_hash ) );
		guard.save<float>( local.pawn + SCHEMA( "C_BaseEntity", "m_flWaterLevel"_hash ) );

		guard.save<math::vector3>( game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
		guard.save_raw( game_scene_node + encoded_origin_offset, static_cast< std::size_t >( encoded_origin_size ) );

		guard.save<float>( local.pawn + SCHEMA( "C_CSPlayerPawn", "m_flVelocityModifier"_hash ) );
		guard.save<int>( local.pawn + SCHEMA( "C_CSPlayerPawn", "m_iShotsFired"_hash ) );
		guard.save<bool>( local.pawn + SCHEMA( "C_CSPlayerPawn", "m_bIsWalking"_hash ) );
		guard.save<math::vector3>( local.pawn + SCHEMA( "C_CSPlayerPawn", "m_angEyeAngles"_hash ) );
		guard.save<float>( local.pawn + SCHEMA( "C_CSPlayerPawn", "m_flLastFiredWeaponTime"_hash ) );
		guard.save<float>( local.pawn + SCHEMA( "C_CSPlayerPawn", "m_ignoreLadderJumpTime"_hash ) );
		guard.save<float>( local.pawn + SCHEMA( "C_CSPlayerPawn", "m_grenadeParameterStashTime"_hash ) );
		guard.save<bool>( local.pawn + SCHEMA( "C_CSPlayerPawn", "m_bGrenadeParametersStashed"_hash ) );
		guard.save<math::vector3>( local.pawn + SCHEMA( "C_CSPlayerPawn", "m_angStashedShootAngles"_hash ) );
		guard.save<math::vector3>( local.pawn + SCHEMA( "C_CSPlayerPawn", "m_vecStashedGrenadeThrowPosition"_hash ) );
		guard.save<math::vector3>( local.pawn + SCHEMA( "C_CSPlayerPawn", "m_vecStashedVelocity"_hash ) );
		guard.save_raw( local.pawn + SCHEMA( "C_CSPlayerPawn", "m_angShootAngleHistory"_hash ), sizeof( math::vector3 ) * 2 );
		guard.save_raw( local.pawn + SCHEMA( "C_CSPlayerPawn", "m_vecThrowPositionHistory"_hash ), sizeof( math::vector3 ) * 2 );
		guard.save_raw( local.pawn + SCHEMA( "C_CSPlayerPawn", "m_vecVelocityHistory"_hash ), sizeof( math::vector3 ) * 2 );

		guard.save<math::vector3>( local.pawn + SCHEMA( "C_BaseModelEntity", "m_vecViewOffset"_hash ) );

		guard.save<int>( aim_punch_services + SCHEMA( "CCSPlayer_AimPunchServices", "m_predictableBaseTick"_hash ) );
		guard.save<float>( aim_punch_services + SCHEMA( "CCSPlayer_AimPunchServices", "m_predictableBaseTickInterpAmount"_hash ) );
		guard.save<math::vector3>( aim_punch_services + SCHEMA( "CCSPlayer_AimPunchServices", "m_predictableBaseAngle"_hash ) );
		guard.save<math::vector3>( aim_punch_services + SCHEMA( "CCSPlayer_AimPunchServices", "m_predictableBaseAngleVel"_hash ) );
		guard.save<int>( aim_punch_services + SCHEMA( "CCSPlayer_AimPunchServices", "m_unpredictableBaseTick"_hash ) );
		guard.save<math::vector3>( aim_punch_services + SCHEMA( "CCSPlayer_AimPunchServices", "m_unpredictableBaseAngle"_hash ) );

		guard.save<float>( weapon_services + SCHEMA( "CCSPlayer_WeaponServices", "m_flNextAttack"_hash ) );
		guard.save<std::uint32_t>( weapon_services + SCHEMA( "CCSPlayer_WeaponServices", "m_nOldTotalShootPositionHistoryCount"_hash ) );
		guard.save<std::uint32_t>( weapon_services + SCHEMA( "CCSPlayer_WeaponServices", "m_nOldTotalInputHistoryCount"_hash ) );

		guard.save<float>( weapon + SCHEMA( "C_CSWeaponBase", "m_flNextClientFireBulletTime"_hash ) );
		guard.save<float>( weapon + SCHEMA( "C_CSWeaponBase", "m_flNextClientFireBulletTime_Repredict"_hash ) );
		guard.save_raw( weapon + SCHEMA( "C_CSWeaponBase", "m_weaponMode"_hash ), SCHEMA( "C_CSWeaponBase", "m_flRecoilIndex"_hash ) + sizeof( float ) - SCHEMA( "C_CSWeaponBase", "m_weaponMode"_hash ) );
		guard.save_raw( weapon + SCHEMA( "C_CSWeaponBase", "m_nPostponeFireReadyTicks"_hash ), SCHEMA( "C_CSWeaponBase", "m_bIsHauledBack"_hash ) + sizeof( bool ) - SCHEMA( "C_CSWeaponBase", "m_nPostponeFireReadyTicks"_hash ) );
		guard.save<float>( weapon + SCHEMA( "C_CSWeaponBase", "m_fLastShotTime"_hash ) );
		guard.save<float>( weapon + SCHEMA( "C_CSWeaponBase", "m_flNextAttackRenderTimeOffset"_hash ) );
		guard.save<float>( weapon + SCHEMA( "C_CSWeaponBase", "m_flWatTickOffset"_hash ) );
		guard.save<int>( weapon + SCHEMA( "C_BasePlayerWeapon", "m_nNextPrimaryAttackTick"_hash ) );
		guard.save<float>( weapon + SCHEMA( "C_BasePlayerWeapon", "m_flNextPrimaryAttackTickRatio"_hash ) );

		guard.save<int>( weapon + SCHEMA( "C_BasePlayerWeapon", "m_nNextSecondaryAttackTick"_hash ) );
		guard.save<float>( weapon + SCHEMA( "C_BasePlayerWeapon", "m_flNextSecondaryAttackTickRatio"_hash ) );
		guard.save<int>( weapon + SCHEMA( "C_BasePlayerWeapon", "m_iClip1"_hash ) );
		guard.save<int>( weapon + SCHEMA( "C_BasePlayerWeapon", "m_iClip2"_hash ) );
		guard.save_raw( weapon + SCHEMA( "C_BasePlayerWeapon", "m_pReserveAmmo"_hash ), sizeof( int ) * 2 );
		guard.save<int>( weapon + SCHEMA( "C_CSWeaponBaseGun", "m_zoomLevel"_hash ) );
		guard.save<int>( weapon + SCHEMA( "C_CSWeaponBaseGun", "m_iBurstShotsRemaining"_hash ) );

		guard.save<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_flStamina"_hash ) );
		guard.save<bool>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_bDucked"_hash ) );
		guard.save<bool>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_bDucking"_hash ) );
		guard.save<bool>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_bDesiresDuck"_hash ) );
		guard.save<bool>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_bDuckOverride"_hash ) );
		guard.save<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_flDuckAmount"_hash ) );
		guard.save<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_flDuckSpeed"_hash ) );
		guard.save<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_flDuckRootOffset"_hash ) );
		guard.save<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_flDuckViewOffset"_hash ) );
		guard.save<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_flLastDuckTime"_hash ) );
		guard.save<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_flBombPlantViewOffset"_hash ) );
		guard.save<bool>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_bSpeedCropped"_hash ) );
		guard.save<int>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_nLadderSurfacePropIndex"_hash ) );
		guard.save<math::vector2>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_vecLastPositionAtFullCrouchSpeed"_hash ) );
		guard.save<bool>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_duckUntilOnGround"_hash ) );
		guard.save<bool>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_bHasWalkMovedSinceLastJump"_hash ) );
		guard.save<bool>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_bInStuckTest"_hash ) );
		guard.save<int>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_nOldWaterLevel"_hash ) );
		guard.save<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_flWaterEntryTime"_hash ) );
		guard.save<math::vector3>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_vecForward"_hash ) );
		guard.save<math::vector3>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_vecLeft"_hash ) );
		guard.save<math::vector3>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_vecUp"_hash ) );
		guard.save<int>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_nGameCodeHasMovedPlayerAfterCommand"_hash ) );
		guard.save<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_fStashGrenadeParameterWhen"_hash ) );
		guard.save<std::uintptr_t>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_nButtonDownMaskPrev"_hash ) );
		guard.save<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_flHeightAtJumpStart"_hash ) );
		guard.save<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_flMaxJumpHeightThisJump"_hash ) );
		guard.save<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_flMaxJumpHeightLastJump"_hash ) );
		guard.save<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_flStaminaAtJumpStart"_hash ) );
		guard.save<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_flVelMulAtJumpStart"_hash ) );
		guard.save<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_flAccumulatedJumpError"_hash ) );
		guard.save<math::vector2>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_vecWalkWishVel"_hash ) );
		guard.save<bool>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_bHasEverProcessedCommand"_hash ) );
		guard.save<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_flTicksSinceLastSurfingDetected"_hash ) );
		guard.save<bool>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_bJumpApexPending"_hash ) );
		guard.save<bool>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_bWasSurfing"_hash ) );
		guard.save<int>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_nLastJumpTick"_hash ) );
		guard.save<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_flLastJumpFrac"_hash ) );
		guard.save<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_flLastJumpVelocityZ"_hash ) );
		guard.save<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_gtLastTimeOnStaticWorldGround"_hash ) );
		guard.save<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_gtLastTimeInAir"_hash ) );
		guard.save_raw( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_LegacyJump"_hash ), 24 );
		guard.save_raw( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_ModernJump"_hash ), 56 );
		guard.save<bool>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_bUseFrictionStashedSpeed"_hash ) );
		guard.save<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_flUseFrictionStashedSpeedUntilFrac"_hash ) );
		guard.save<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_flFrictionStashedSpeed"_hash ) );

		guard.save<float>( movement_services + SCHEMA( "CPlayer_MovementServices_Humanoid", "m_flSurfaceFriction"_hash ) );
		guard.save<float>( movement_services + SCHEMA( "CPlayer_MovementServices_Humanoid", "m_flFallVelocity"_hash ) );
		guard.save<math::vector3>( movement_services + SCHEMA( "CPlayer_MovementServices_Humanoid", "m_groundNormal"_hash ) );
		guard.save<float>( movement_services + SCHEMA( "CPlayer_MovementServices_Humanoid", "m_flStepSoundTime"_hash ) );
		guard.save<int>( movement_services + SCHEMA( "CPlayer_MovementServices_Humanoid", "m_nStepside"_hash ) );
		guard.save<std::uint32_t>( movement_services + SCHEMA( "CPlayer_MovementServices_Humanoid", "m_surfaceProps"_hash ) );
		guard.save<std::uintptr_t>( movement_services + 408 );

		static std::atomic_bool first_prediction_traced{};
		const auto trace = !first_prediction_traced.exchange( true, std::memory_order_relaxed );
		diag::exception_scope exception_scope{ "prediction" };
		if ( trace )
		{
			diag::step( "prediction: state saved" );
		}

		{
			if ( old_slot )
			{
				memory::write<std::uint8_t>( old_slot + 140, 0 );
			}

			const auto next_tick = memory::read<int>( local.controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ) ) + 1;
			const auto next_time = static_cast< float >( next_tick ) * 0.015625f;

			memory::write<float>( global_vars + 48, next_time );
			memory::write<float>( global_vars + 52, 0.015625f );
			memory::write<int>( global_vars + 68, next_tick );
			memory::write<float>( global_vars + 80, 0.0f );
			memory::write<std::uint32_t>( global_vars + 88, GetCurrentThreadId( ) );

			memory::write( addresses::globals::simulation_player, local.pawn );
			memory::write( addresses::globals::prediction_player, local.pawn );
			memory::write<int>( local.controller + SCHEMA( "CBasePlayerController", "m_nTickBase"_hash ), next_tick );

			std::uintptr_t pawn_guard[ 1 ]{};
			memory::call<void>( prediction_set_pawn, pawn_guard, local.pawn );
			memory::call<void>( prediction_set_state, pred_state, std::uint8_t( 1 ) );

			memory::call_vfunc<void>( movement_services, 46, cmd_ptr );  // SetupContext
			if ( trace )
			{
				diag::step( "prediction: context ready" );
			}

			const auto move_data = memory::call_vfunc<std::uintptr_t>( movement_services, 38 );
			const bool fb_seedsync = memory::call_vfunc<bool>( movement_services, 48 );

			if ( !move_data )
			{
				memory::call<void>( prediction_set_state, pred_state, std::uint8_t( 0 ) );
				memory::call<void>( prediction_reset_pawn, pawn_guard );
				memory::call_vfunc<void>( movement_services, 47 );  // CleanupContext
				return false;
			}

			memory::call<void>( prediction_setup_move, move_data, cmd_ptr, next_tick, fb_seedsync ? 1 : 0 );
			memory::call_vfunc<void>( movement_services, 39, cmd_ptr, move_data );  // SetupMove
			memory::write<std::uintptr_t>( movement_services + 408, 0 );
			if ( trace )
			{
				diag::step( "prediction: move ready" );
			}

			memory::call<void>( prediction_process_movement, movement_services, cmd_ptr, move_data, 1 );
			if ( trace )
			{
				diag::step( "prediction: movement processed" );
			}
			memory::call_vfunc<void>( movement_services, 43, cmd_ptr, move_data );  // PostThink_Weapon
			memory::call<void>( prediction_finish_move, movement_services, cmd_ptr, move_data, 1 );
			if ( trace )
			{
				diag::step( "prediction: move finished" );
			}

			memory::write( addresses::globals::simulation_player, 0ull );

			memory::call<void>( prediction_set_state, pred_state, std::uint8_t( 0 ) );
			memory::call<void>( prediction_reset_pawn, pawn_guard );
			memory::call_vfunc<void>( movement_services, 47 );  // CleanupContext
			if ( trace )
			{
				diag::step( "prediction: context cleaned" );
				diag::step( "prediction: callback begin" );
			}

			fn( );
			if ( trace )
			{
				diag::step( "prediction: callback end" );
			}
		}

		if ( trace )
		{
			diag::step( "prediction: restore begin" );
		}
		guard.restore( );
		if ( trace )
		{
			diag::step( "prediction: restore end" );
		}

		return true;
	}

} // namespace systems
