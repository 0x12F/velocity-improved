#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <core/systems/systems.hpp>

namespace systems
{
    bool model_preview::initialize( )
    {
        m_initialized = true;
        m_current_texture = nullptr;
        logging::console::print( "[model_preview] initialized" );
        return true;
    }

    bool model_preview::on_generate_primitives(
        std::uintptr_t owner_entity,
        std::uint32_t owner_hash,
        std::uintptr_t scene_object,
        std::uintptr_t primitive_buffer,
        void( __fastcall* original_fn )( std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t ),
        std::uintptr_t a1,
        std::uintptr_t scene_view )
    {
        if ( !m_initialized || !owner_entity || !scene_object )
            return false;

        if ( owner_hash != "C_CSGO_PreviewPlayer"_hash )
            return false;

        const std::uintptr_t mesh_instance = memory::read<std::uintptr_t>(
            owner_entity + SCHEMA( "C_BaseEntity", "m_pMeshInstance"_hash )
        );

        if ( mesh_instance )
        {
            const std::uintptr_t mesh_cache = memory::read<std::uintptr_t>( mesh_instance + 0x8 );
            if ( mesh_cache && mesh_cache < 0x7FFFFFFFFFFF )
            {
                const std::uintptr_t model_imp = memory::read<std::uintptr_t>( mesh_cache );
                if ( model_imp && model_imp < 0x7FFFFFFFFFFF )
                {
                    const char* model_path = memory::read<const char*>( model_imp + 0x8 );
                    if ( model_path )
                    {
                        if ( fnv1a::runtime_hash( model_path ) != fnv1a::runtime_hash( "characters/models/ctm_st6/ctm_st6_variante.vmdl" ) )
                            return false;
                    }
                }
            }
        }

        /*if ( auto* data = reinterpret_cast< c_generate_primitives_data* >( scene_object ) )
        {
            if ( auto* scene_layer = data->m_scene_layer )
            {
                if ( scene_layer->m_texture_handle &&
                    scene_layer->m_texture_handle->m_texture )
                {
                    m_current_texture = scene_layer->m_texture_handle->m_texture;

                    static bool first_log = true;
                    if ( first_log )
                    {
                        logging::console::print( "[model_preview] captured preview texture!" );
                        first_log = false;
                    }
                }
            }
        }*/

        return false;
    }
}