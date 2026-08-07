#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>

#include "../misc.hpp"
#include <protection/game_addresses.hpp>

namespace features::misc {
	namespace {
		constexpr std::ptrdiff_t k_fov_offset{ 0x498 };
		constexpr std::ptrdiff_t k_aspect_ratio_offset{ 0x4d4 };
		constexpr std::ptrdiff_t k_view_flags_offset{ 0x551 };
		constexpr std::uint8_t k_explicit_aspect_ratio_flag{ 1u << 1 };

		[[nodiscard]] float scale_horizontal_fov( float fov, float aspect_ratio )
		{
			constexpr auto degrees_to_half_radians{ std::numbers::pi_v<float> / 360.0f };
			constexpr auto half_radians_to_degrees{ 360.0f / std::numbers::pi_v<float> };
			constexpr auto four_by_three_inverse{ 0.75f };

			return std::atan( std::tan( fov * degrees_to_half_radians ) * aspect_ratio * four_by_three_inverse ) * half_radians_to_degrees;
		}
	}

	void camera::on_override_view( std::uintptr_t view_setup )
	{
		const auto local = systems::g_local.get( );
		if ( local.is_alive && !systems::g_local.is_in_cinematic( ) && local.team >= 2 && local.pawn )
		{
			this->do_thirdperson( view_setup, local.pawn );
			this->do_fov_change( view_setup, local.pawn );
		}

		// Aspect conversion must run after the base FOV has been selected.
		this->do_aspect_ratio_change( view_setup );
	}

	void camera::update_fov_sensitivity( std::uintptr_t player_pawn ) const
	{
		if ( !settings::g_misc.m_camera.change_fov.value )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		if ( player_pawn != local.pawn )
		{
			return;
		}

		const auto& cfg = settings::g_misc.m_camera;
		const auto is_scoped = memory::read<bool>( player_pawn + SCHEMA( "C_CSPlayerPawn", "m_bIsScoped"_hash ) );
		const auto target_fov = ( is_scoped && cfg.scoped_fov_override.value ) ? cfg.scoped_fov.value : cfg.fov.value;

		if ( is_scoped == this->m_cached_scoped && target_fov == this->m_cached_target_fov && this->m_cached_fov_sensitivity >= 0.0f )
		{
			const auto current_adjust = memory::read<float>( player_pawn + SCHEMA( "C_BasePlayerPawn", "m_flFOVSensitivityAdjust"_hash ) );
			if ( std::fabsf( current_adjust - this->m_cached_fov_sensitivity ) < 0.0001f )
			{
				return;
			}
		}

		this->m_cached_scoped = is_scoped;
		this->m_cached_target_fov = target_fov;

		const auto ratio = CONVAR ("zoom_sensitivity_ratio")->get<float>( );
		const auto desired = ratio * ( target_fov / 90.0f );

		this->m_cached_fov_sensitivity = desired;
		memory::write<float>( player_pawn + SCHEMA( "C_BasePlayerPawn", "m_flFOVSensitivityAdjust"_hash ), desired );
	}

	void camera::do_thirdperson( std::uintptr_t view_setup, std::uintptr_t local_pawn ) const
	{
		const auto& cfg = settings::g_misc.m_camera;
		if ( !cfg.thirdperson.value )
		{
			return;
		}

		const auto game_scene_node = memory::read<std::uintptr_t>( local_pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		const auto origin = memory::read<math::vector3>( game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
		const auto view_offset = memory::read<math::vector3>( local_pawn + SCHEMA( "C_BaseModelEntity", "m_vecViewOffset"_hash ) );
		const auto eye_position = origin + view_offset;
		const auto view_angles = systems::g_input.get_view_angles( );

		math::vector3 forward{};
		{
			math::helpers::angle_vectors_left( view_angles, &forward );
		}

		auto camera_position = eye_position - forward * cfg.thirdperson_distance;
		const auto hull = math::vector3{ -cfg.thirdperson_hull_size, -cfg.thirdperson_hull_size, -cfg.thirdperson_hull_size };
		const auto result = systems::g_tracing.trace_hull( eye_position, camera_position, hull, hull, local_pawn );

		if ( result.fraction < 1.0f )
		{
			const auto world = systems::g_entities.get_by_index( 0 );
			if ( result.hit_entity == world )
			{
				camera_position = eye_position + ( camera_position - eye_position ) * result.fraction;
			}
		}

		memory::write<math::vector3>( view_setup + 0x4a0, camera_position );
	}

	void camera::do_fov_change( std::uintptr_t view_setup, std::uintptr_t local_pawn ) const
	{
		const auto& cfg = settings::g_misc.m_camera;
		if ( !cfg.change_fov.value )
		{
			return;
		}

		const auto is_scoped = memory::read<bool>( local_pawn + SCHEMA( "C_CSPlayerPawn", "m_bIsScoped"_hash ) );
		const auto target_fov = ( is_scoped && cfg.scoped_fov_override.value ) ? cfg.scoped_fov.value : cfg.fov.value;

		memory::write<float>( view_setup + k_fov_offset, target_fov );

		this->update_fov_sensitivity( local_pawn );
	}

	void camera::do_aspect_ratio_change( std::uintptr_t view_setup )
	{
		const auto& cfg = settings::g_misc.m_camera;

		if ( cfg.change_aspect_ratio.value )
		{
			const auto base_fov = memory::read<float>( view_setup + k_fov_offset );
			const auto flags = memory::read<std::uint8_t>( view_setup + k_view_flags_offset );

			// Explicit aspect bypasses the game's native 4:3-based FOV conversion.
			memory::write<float>( view_setup + k_fov_offset, scale_horizontal_fov( base_fov, cfg.aspect_ratio ) );
			memory::write<float>( view_setup + k_aspect_ratio_offset, cfg.aspect_ratio );
			memory::write<std::uint8_t>( view_setup + k_view_flags_offset, flags | k_explicit_aspect_ratio_flag );
		}
		else
		{
			const auto flags = memory::read<std::uint8_t>( view_setup + k_view_flags_offset );
			memory::write<std::uint8_t>( view_setup + k_view_flags_offset,
				flags & static_cast<std::uint8_t>( ~k_explicit_aspect_ratio_flag ) );
		}
	}

} // namespace features::misc
