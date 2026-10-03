#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <protection/game_addresses.hpp>
#include "../systems.hpp"

namespace systems {

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

} // namespace systems
