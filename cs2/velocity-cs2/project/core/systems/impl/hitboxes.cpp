#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>

#include "../systems.hpp"

namespace systems {

    hitboxes::set hitboxes::query( std::uintptr_t game_scene_node, bool seh )
    {
        set result{};

        // CSkeletonInstance moved its model handle in the current client.
        auto model_handle = seh ? memory::safe_read<std::uintptr_t>( game_scene_node + 0x1E0 ).value_or( 0 ) : memory::read<std::uintptr_t>( game_scene_node + 0x1E0 );
        if ( !model_handle )
        {
            model_handle = seh ? memory::safe_read<std::uintptr_t>( game_scene_node + 0x210 ).value_or( 0 ) : memory::read<std::uintptr_t>( game_scene_node + 0x210 );
        }

        if ( !model_handle )
        {
            return result;
        }

        const auto cmodel = memory::read<std::uintptr_t>( model_handle );
        if ( !cmodel )
        {
            return result;
        }

        const auto render_mesh_array = memory::read<std::uintptr_t>( cmodel + 0x78 );
        if ( !render_mesh_array )
        {
            return result;
        }

        const auto render_meshes = memory::read<std::uintptr_t>( render_mesh_array );
        if ( !render_meshes )
        {
            return result;
        }

        std::uintptr_t hitbox_set{};

        // Hitbox sets now live in the render mesh's set container. Player
        // models use the first set; retain the old direct pointer as fallback.
        const auto set_count = memory::read<int>( render_meshes + 0x174 );
        if ( set_count > 0 && set_count <= 32 )
        {
            const auto storage_flags = memory::read<std::uint32_t>( render_meshes + 0x164 );
            hitbox_set = ( storage_flags & 0x7fffffff )
                ? memory::read<std::uintptr_t>( render_meshes + 0x168 )
                : render_meshes + 0x168;
        }

        if ( !hitbox_set )
        {
            hitbox_set = memory::read<std::uintptr_t>( render_meshes + 0x150 );
        }

        if ( !hitbox_set )
        {
            return result;
        }

        const auto count = memory::read<int>( hitbox_set + 0x28 );
        if ( count <= 0 || count > 20 )
        {
            return result;
        }

        const auto array_ptr = memory::read<std::uintptr_t>( hitbox_set + 0x30 );
        if ( !array_ptr )
        {
            return result;
        }

        const auto remap_count = memory::read<int>( cmodel + 0x220 );
        const auto remap_table = memory::read<std::uintptr_t>( cmodel + 0x228 );
        const auto mesh_a = memory::read<std::uintptr_t>( cmodel + 0x240 );
        const auto mesh_b = memory::read<std::uintptr_t>( cmodel + 0x2F0 );

        constexpr auto k_hitbox_stride{ 0x70 };

        for ( auto i = 0; i < count; ++i )
        {
            const auto base = array_ptr + static_cast< std::size_t >( i ) * k_hitbox_stride;

            auto bone = -1;

            if ( remap_table && mesh_a && mesh_b && remap_count > 0 )
            {
                const auto hb_idx = memory::read<std::uint16_t>( base + 0x48 );
                const auto ofs_a = memory::read<std::uint16_t>( mesh_a );
                const auto ofs_b = memory::read<std::uint16_t>( mesh_b );
                const auto slot = static_cast< std::size_t >( hb_idx + ofs_a + ofs_b );

                if ( slot < static_cast< std::size_t >( remap_count ) )
                {
                    bone = memory::read<std::int16_t>( remap_table + 2 * slot );
                }
            }

            if ( bone < 0 )
            {
                continue;
            }

            const auto radius = memory::read<float>( base + 0x30 );
            if ( radius < 0.0f || radius > 100.0f )
            {
                continue;
            }

            auto& hb = result.entries[ result.count++ ];
            hb.index = i;
            hb.bone = bone;
            hb.mins = memory::read<math::vector3>( base + 0x18 );
            hb.maxs = memory::read<math::vector3>( base + 0x24 );
            hb.radius = radius;
            hb.shape_type = memory::read<std::uint8_t>( base + 0x3C );
            hb.translation_only = memory::read<std::uint8_t>( base + 0x3D ) != 0;
        }

        return result;
    }

	int hitboxes::hitgroup_from_hitbox( int hitbox )
	{
		switch ( hitbox )
		{
		case 0:  return 1;
		case 1:  return 8;
		case 2:  return 3;
		case 3:  return 3;
		case 4:  return 2;
		case 5:  return 2;
		case 6:  return 2;
		case 7:  return 7;
		case 8:  return 6;
		case 9:  return 7;
		case 10: return 6;
		case 11: return 7;
		case 12: return 6;
		case 13: return 5;
		case 14: return 4;
		case 15: return 5;
		case 16: return 5;
		case 17: return 4;
		case 18: return 4;
		default: return 0;
		}
	}

	const char* hitboxes::hitgroup_to_name( int hitgroup )
	{
		constexpr const char* k_names[ ]{ "body", "head", "chest", "stomach", "left arm", "right arm", "left leg", "right leg", "neck" };
		return hitgroup < 9 ? k_names[ hitgroup ] : "body";
	}

} // namespace systems
