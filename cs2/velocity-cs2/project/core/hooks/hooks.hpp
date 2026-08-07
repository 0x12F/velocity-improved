#pragma once

#include <utilities/hooking/hooking.hpp>

namespace hooks {

	class cheat
	{
	public:
		cheat( ) = delete;

		static bool initialize( );
		static void shutdown( );

		static HRESULT __fastcall present( IDXGISwapChain* thisptr, UINT sync_interval, UINT flags );
		static HRESULT __fastcall resize_buffers( IDXGISwapChain* thisptr, UINT buffer_count, UINT width, UINT height, DXGI_FORMAT new_format, UINT swap_chain_flags );
		static LRESULT __stdcall wnd_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam );
		static void __stdcall om_set_render_targets( ID3D11DeviceContext* ctx, UINT num_views, ID3D11RenderTargetView* const* rtvs, ID3D11DepthStencilView* dsv );
		static void __fastcall cmd_interpreter( std::uintptr_t render_thread, std::uintptr_t item, std::uint8_t flag );
		static void __fastcall frame_stage_notify( std::uintptr_t thisptr, int stage );
		static void __fastcall create_move( std::uintptr_t thisptr, int slot, bool active );
		static void __fastcall handle_view_angles( std::uintptr_t thisptr, int a2 );
		static void __fastcall add_entity( std::uintptr_t thisptr, std::uintptr_t entity, std::uint32_t handle );
		static void __fastcall remove_entity( std::uintptr_t thisptr, std::uintptr_t entity, std::uint32_t handle );
		static void __fastcall render_view( std::uintptr_t thisptr );
		static void __fastcall draw_skybox_array( std::uintptr_t thisptr, std::uintptr_t a2, std::uintptr_t mesh_array, int mesh_count, int a5, std::uintptr_t a6, std::uintptr_t a7, std::uintptr_t a8 );
		static std::uintptr_t __fastcall light_scene_object( std::uintptr_t thisptr, std::uintptr_t object, std::uintptr_t a3 );
		static void __fastcall draw_scene_object_array( std::uintptr_t thisptr, std::uintptr_t a2, std::uintptr_t object_array );
		static std::uintptr_t __fastcall draw_scene_object( std::uintptr_t a1, std::uintptr_t a2, std::uintptr_t batch, int batch_count, int a5, std::uintptr_t a6, std::uintptr_t a7, std::uintptr_t a8 );
		static bool __fastcall is_glowing( std::uintptr_t glow_property );
		static void __fastcall get_glow_color( std::uintptr_t glow_property, float* color );
		static void __fastcall generate_primitives( std::uintptr_t thisptr, std::uintptr_t scene_object, std::uintptr_t scene_view, std::uintptr_t primitive_buffer );
		static std::uintptr_t __fastcall parse_report_hit( std::uintptr_t thisptr, std::uint8_t deleting );
		static std::uintptr_t __fastcall setup_fog( __m128i* output, int* mode );
		static std::uintptr_t __fastcall set_shader_param( __m128i* map, std::uint32_t hash, __m128i* value );
		static std::uintptr_t __fastcall set_postprocess_vec( __m128i* map, std::uint32_t hash, __m128i* value );
		static void __fastcall override_view( std::uintptr_t thisptr, std::uintptr_t view_setup );
		static void __fastcall update_fov_sensitivity( std::uintptr_t thisptr );
		static void __fastcall render_scope( std::uintptr_t a1, std::uintptr_t a2 );
		static bool __fastcall render_crosshair( std::uintptr_t a1 );
		static float __fastcall prepare_scene_material( std::uintptr_t material, void* a2, float a3 );
		static void __fastcall post_network_data_received( std::uintptr_t thisptr );
		static bool __fastcall draw_overhead( std::uintptr_t pawn, std::uint32_t player_slot );
		static std::uintptr_t __fastcall draw_legs( std::uintptr_t a1, std::uintptr_t a2, std::uintptr_t a3, std::uintptr_t a4, std::uintptr_t a5 );
		static bool __fastcall get_transforms_for_hitbox_list( std::uintptr_t a1, std::uintptr_t a2, int* a3 );
		static void __fastcall sort_primitives( std::uintptr_t thisptr, std::uintptr_t a2, std::uintptr_t a3, std::uint32_t a4 );
		static float __fastcall get_inaccuracy( std::uintptr_t thisptr, float* a2, float* a3 );
		static float* __fastcall get_interpolated_shoot_position( std::uintptr_t thisptr, float* out, int* tick_frac );
		static std::uintptr_t __fastcall level_initialization( std::uintptr_t a1, const char* new_map );
		static std::uintptr_t __fastcall level_shutdown( std::uintptr_t a1 );
		static void __fastcall read_frame_input( std::uintptr_t a1, std::uint32_t a2 );
		static void __fastcall process_input_event( std::uintptr_t thisptr, int slot, float frametime );
		static std::uintptr_t __fastcall render_decals( std::uintptr_t render_context, std::uintptr_t** render_view, bool pass_flag_a, bool pass_flag_b );
		static void __fastcall render_smoke( std::uintptr_t a1, std::uintptr_t a2, int a3, int a4, std::uintptr_t a5, std::uintptr_t a6 );
		static std::uintptr_t __fastcall render_smoke_map( std::uintptr_t thisptr, std::size_t size, std::uintptr_t* out_ptr );
		static void __fastcall render_smoke_unmap( std::uintptr_t thisptr, std::uintptr_t ctx, std::size_t size );
		static void __fastcall draw_flash_effect( std::uintptr_t a1, int a2, std::uintptr_t* a3, std::uintptr_t a4, __m128* a5 );
		static char __fastcall set_info( std::uintptr_t rcx, std::uintptr_t a2 );

	private:
		inline static hooking::jmp m_present{};
		inline static hooking::jmp m_resize_buffers{};
		inline static hooking::jmp m_wnd_proc{};
		inline static hooking::jmp m_om_set_render_targets{};
		inline static hooking::jmp m_cmd_interpreter{};
		inline static hooking::jmp m_frame_stage_notify{};
		inline static hooking::jmp m_create_move{};
		inline static hooking::jmp m_handle_view_angles{};
		inline static hooking::jmp m_add_entity{};
		inline static hooking::jmp m_remove_entity{};
		inline static hooking::jmp m_render_view{};
		inline static hooking::jmp m_draw_skybox_array{};
		inline static hooking::jmp m_light_scene_object{};
		inline static hooking::jmp m_draw_scene_object_array{};
		inline static hooking::jmp m_draw_scene_object{};
		inline static hooking::jmp m_is_glowing{};
		inline static hooking::jmp m_get_glow_color{};
		inline static hooking::jmp m_generate_primitives{};
		inline static hooking::jmp m_parse_report_hit{};
		inline static hooking::jmp m_setup_fog{};
		inline static hooking::jmp m_set_shader_param{};
		inline static hooking::jmp m_set_postprocess_vec{};
		inline static hooking::jmp m_override_view{};
		inline static hooking::jmp m_update_fov_sensitivity{};
		inline static hooking::jmp m_render_scope{};
		inline static hooking::jmp m_render_crosshair{};
		inline static hooking::jmp m_prepare_scene_material{};
		inline static hooking::jmp m_post_network_data_received{};
		inline static hooking::jmp m_draw_overhead{};
		inline static hooking::jmp m_draw_legs{};
		inline static hooking::jmp m_get_transforms_for_hitbox_list{};
		inline static hooking::jmp m_sort_primitives{};
		inline static hooking::jmp m_get_inaccuracy{};
		inline static hooking::jmp m_get_interpolated_shoot_position{};
		inline static hooking::jmp m_level_initialization{};
		inline static hooking::jmp m_level_shutdown{};
		inline static hooking::jmp m_read_frame_input{};
		inline static hooking::jmp m_process_input_event{};
		inline static hooking::jmp m_render_decals{};
		inline static hooking::jmp m_render_smoke{};
		inline static hooking::jmp m_render_smoke_map{};
		inline static hooking::jmp m_render_smoke_unmap{};
		inline static hooking::jmp m_draw_flash_effect{};
		inline static hooking::jmp m_set_info{};
	};

	class utility
	{
	public:
		utility( ) = delete;

		static bool initialize( );
		static void shutdown( );

		static std::uintptr_t __fastcall service_read( std::uintptr_t a1 );
		static std::intptr_t __fastcall log_internal( std::uintptr_t a1, std::uint32_t channel, std::int32_t severity, std::uintptr_t metadata, const char* message, std::intptr_t* args );

	private:
		inline static hooking::jmp m_service_read{};
		inline static hooking::jmp m_log_internal{};
	};

} // namespace hooks
