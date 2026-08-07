#pragma once
#include "interfaces.hpp"

namespace addresses {

	namespace modules {

		bool initialize( );

		inline std::uintptr_t client{};
		inline std::uintptr_t engine2{};
		inline std::uintptr_t server{};
		inline std::uintptr_t scene_system{};
		inline std::uintptr_t material_system2{};
		inline std::uintptr_t render_system_dx11{};
		inline std::uintptr_t panorama{};
		inline std::uintptr_t game_overlay_renderer{};
		inline std::uintptr_t schema_system{};
		inline std::uintptr_t input_system{};
		inline std::uintptr_t sound_system{};
		inline std::uintptr_t tier0{};
		inline std::uintptr_t particles{};
		inline std::uintptr_t resource_system{};
		inline std::uintptr_t localize{};
		inline std::uintptr_t mesh_system{};
		inline std::uintptr_t file_system_stdio{};
		inline std::uintptr_t vphysics2{};

	} // namespace modules

	namespace globals {

		bool initialize( );

		// interfaces
		inline std::uintptr_t source2client{};
		inline std::uintptr_t panorama{};
		inline std::uintptr_t source2engine_to_client{};
		inline std::uintptr_t scene_system{};
		inline std::uintptr_t material_system{};
		inline std::uintptr_t schema_system{};
		inline std::uintptr_t input_system{};
		inline std::uintptr_t particle_system_mgr{};
		inline interfaces::c_engine_cvar* cvar{};
		inline std::uintptr_t source2client_prediction{};
		inline std::uintptr_t network_client_service{};
		inline std::uintptr_t resource_system{};
		inline std::uintptr_t localize{};
		inline std::uintptr_t mesh_system{};
		inline std::uintptr_t file_system{};

		// normal
		inline std::uintptr_t csgo_input{};
		inline std::uintptr_t entity_list{};
		inline std::uintptr_t local_player_controller{};
		inline std::uintptr_t global_vars{};
		inline std::uintptr_t view_matrix{};
		inline std::uintptr_t game_rules{};
		inline std::uintptr_t light_data_queue{};
		inline std::uintptr_t particle_manager{};
		inline std::uintptr_t game_event_manager{};
		inline std::uintptr_t game_trace_manager{};
		inline std::uintptr_t render_game_system_storage{};
		inline std::uintptr_t mem_alloc{};
		inline std::uintptr_t material_manager{};
		inline std::uintptr_t game_entity_system{};
		inline std::uintptr_t weapon_recoil_data{};
		inline std::uintptr_t hud{};
		inline std::uintptr_t prediction_seed{};
		inline std::uintptr_t simulation_player{};
		inline std::uintptr_t prediction_player{};
		inline std::uintptr_t planted_c4{};
		inline std::uintptr_t item_system{};
		inline std::uintptr_t frame_input_ring_idx{};
		inline std::uintptr_t frame_input_ring_base{};
		inline std::uintptr_t prediction_state{};

	} // namespace globals

	namespace functions {

		bool initialize( );


		// cheat hooks
		inline std::uintptr_t present{};
		inline std::uintptr_t resize_buffers{};

	} // namespace functions

} // namespace addresses
