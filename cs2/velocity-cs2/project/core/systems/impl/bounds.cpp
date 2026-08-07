#include <pch/pch.hpp>
#include <cmath>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>

#include "../systems.hpp"

namespace systems {

	bounds::data bounds::get( std::uintptr_t entity )
	{
		const auto collision = memory::read<std::uintptr_t>( entity + SCHEMA( "C_BaseEntity", "m_pCollision"_hash ) );
		const auto game_scene_node = memory::read<std::uintptr_t>( entity + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );

		if ( !collision || !game_scene_node )
		{
			return {};
		}

		// m_vecOrigin is a quantized network-origin type in current CS2 builds,
		// not a plain vector3. Use the evaluated world-space origin instead.
		const auto origin = memory::read<math::vector3>( game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
		const auto mins = memory::read<math::vector3>( collision + SCHEMA( "CCollisionProperty", "m_vecMins"_hash ) ) + origin;
		const auto maxs = memory::read<math::vector3>( collision + SCHEMA( "CCollisionProperty", "m_vecMaxs"_hash ) ) + origin;

		const auto [viewport_w, viewport_h] = xdraw::viewport_size( );
		const auto sw = static_cast< float >( viewport_w );
		const auto sh = static_cast< float >( viewport_h );

		data result{};
		result.min = { FLT_MAX, FLT_MAX };
		result.max = { -FLT_MAX, -FLT_MAX };

		constexpr auto edge_margin = 384.0f;
		auto had_valid_projection = false;

		for ( std::size_t i = 0; i < 8; ++i )
		{
			const math::vector3 corner
			{
				( i & 1 ) ? maxs.x : mins.x,
				( i & 2 ) ? maxs.y : mins.y,
				( i & 4 ) ? maxs.z : mins.z
			};

			const auto proj = g_view.project_full( corner );
			if ( proj.w <= 0.001f || !std::isfinite( proj.screen.x ) || !std::isfinite( proj.screen.y ) )
			{
				continue;
			}

			const auto sx = std::clamp( proj.screen.x, -edge_margin, sw + edge_margin );
			const auto sy = std::clamp( proj.screen.y, -edge_margin, sh + edge_margin );

			result.min.x = std::min( result.min.x, sx );
			result.min.y = std::min( result.min.y, sy );
			result.max.x = std::max( result.max.x, sx );
			result.max.y = std::max( result.max.y, sy );
			had_valid_projection = true;
		}

		if ( !had_valid_projection || result.min.x >= result.max.x || result.min.y >= result.max.y )
		{
			return {};
		}

		result.valid = true;
		return result;
	}

} // namespace systems
