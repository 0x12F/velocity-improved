#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>

#include "../systems.hpp"

namespace systems {

    void frame_data::update( )
    {
        const auto local = systems::g_local.get( );
        if ( !local.is_alive || !local.pawn )
        {
            this->reset( );
            return;
        }

        const auto game_scene_node = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
        if ( !game_scene_node )
        {
            this->reset( );
            return;
        }

        this->m_origin = memory::read<math::vector3>( game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
        this->m_valid = true;
    }

	void frame_data::reset( )
	{
		this->m_origin = {};
		this->m_valid = false;
	}

} // namespace systems