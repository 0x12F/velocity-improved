#pragma once

namespace features::world {

    class weather
    {
    public:
        void on_frame_stage_notify( );
        void release( );

    private:
        static constexpr std::uint32_t invalid_effect_index{ static_cast<std::uint32_t>( -1 ) };

        void create_particle( );
        void update_particles( );
        void release_particles( );

        std::uint32_t m_effect_index{ invalid_effect_index };
        int m_last_particle_type{ -1 };
        float m_last_round_start_time{};
        bool m_particle_loaded{};
    };

    class scene
    {
    public:
        struct skybox_entry
        {
            std::string display_name;
            std::string resource_path;
        };

        void discover_skyboxes( );
        void reset_skybox_state( );

        void on_frame_stage_notify( );
        void on_draw_skybox_array_pre( std::uintptr_t mesh_array, int mesh_count );
        void on_draw_skybox_array_post( );
        void on_light_scene_object_pre( std::uintptr_t object ) const;
        void on_light_scene_object_post( std::uintptr_t object ) const;
        void on_draw_scene_object_array( std::uintptr_t object_array ) const;
        void on_draw_scene_object( std::uintptr_t batch, int batch_count ) const;
        [[nodiscard]] bool on_setup_fog( __m128i* output, int* mode ) const;
        void on_set_shader_param( __m128i*& value, std::uint32_t hash ) const;

       [[nodiscard]] const std::vector<skybox_entry>& get_skyboxes( ) const { return this->m_skyboxes; }

    private:
        void load_skybox_material( const char* path );

        std::vector<skybox_entry> m_skyboxes{};
        struct cached_skybox_material
        {
            std::uintptr_t texture_binding{};
            std::uintptr_t material{};
        };

        std::unordered_map<std::string, cached_skybox_material> m_skybox_materials{};
        std::uintptr_t m_custom_sky_material{};
        int m_loaded_skybox_index{ -1 };

        std::uintptr_t m_active_material_binding{};
        std::uintptr_t m_active_original_material{};
        std::uintptr_t m_active_skybox_descriptor{};
        std::array<float, 3> m_active_original_sky_color{};
        bool m_active_sky_tinted{};
    };

    class smoke
    {
    public:
        void on_render_smoke_pre( ) { this->m_active = true; }
        void on_render_smoke_post( ) { this->m_active = false; }

        void on_map( std::uintptr_t token, std::size_t size, std::uintptr_t buf_ptr );
        void on_unmap( std::uintptr_t token );

    private:
        static inline bool m_active{};
        static inline std::uintptr_t m_buf{};
        static inline std::uintptr_t m_token{};
    };

} // namespace features::world
