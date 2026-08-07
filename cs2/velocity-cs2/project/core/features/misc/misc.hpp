#pragma once

#include <limits>

#include <core/systems/systems.hpp>

namespace features::misc {

	class projectile_trajectory
	{
	public:
		struct trajectory
		{
			std::vector<math::vector3> points{};
			std::vector<math::vector3> bounces{};
			math::vector3 end_pos{};
			float duration{};
			int end_tick{ -1 };
			bool valid{};
		};

		struct in_flight_grenade
		{
			std::uintptr_t entity{};
			std::uintptr_t weapon_hash{};
			std::uint32_t thrower_handle{};
			bool is_enemy{};
			trajectory traj{};
			std::chrono::steady_clock::time_point throw_time{};
			std::chrono::steady_clock::time_point last_seen{};
			std::chrono::steady_clock::time_point detonate_time{};
			bool corrected{};
			bool detonated{};
		};

		void on_render( xdraw::draw_list& draw_list );
		void on_create_move( systems::input::usercmd* cmd );

		[[nodiscard]] bool should_stop( ) const { return this->m_needs_air_stop; }
		[[nodiscard]] const std::vector<in_flight_grenade>& in_flight( ) const { return this->m_in_flight; }

		/// Skip collision with thrower only for opening sim ticks so the nade clears the hand/body.
		static constexpr int k_thrower_collision_skip_ticks{ 18 };

	private:
		static constexpr auto k_gravity_scale{ 0.4f };
		static constexpr auto k_elasticity{ 0.45f };
		static constexpr auto k_hull_size{ 2.0f };
		static constexpr auto k_forward_offset{ 22.0f };
		static constexpr auto k_pull_back{ 6.0f };
		static constexpr auto k_velocity_inherit{ 1.25f };
		static constexpr auto k_stop_speed_sq{ 400.0f };
		static constexpr auto k_steep_bounce_speed_sq{ 96000.0f };
		static constexpr auto k_steep_bounce_normal_z{ 0.7f };
		static constexpr auto k_max_ticks{ 4096 };
		static constexpr auto k_ticks_per_point{ 2 };
		static constexpr auto k_max_bounces{ 20 };
		static constexpr auto k_max_delay_ticks{ 32 };
		static constexpr auto k_throw_cooldown{ 0.75f };
		static constexpr auto k_missing_grace{ 0.1f };
		static constexpr auto k_fade_duration{ 0.75f };
		static constexpr auto k_player_hit_fraction_threshold{ 0.7f };
		static constexpr auto k_player_dampen_dot_threshold{ 0.5f };

		struct damage_info
		{
			int damage{};
			bool is_lethal{};
			math::vector3 position{};
		};

		void setup_throw( std::uintptr_t local_pawn, std::uintptr_t weapon, math::vector3& origin, math::vector3& velocity );
		void simulate( const math::vector3& start, const math::vector3& velocity, std::uintptr_t thrower_pawn, trajectory& out );
		void step_simulation( math::vector3& pos, math::vector3& vel, std::uintptr_t thrower_pawn, int sim_tick, bool& hit, bool& detonated );
		void resolve_collision( const systems::tracing::result& trace, math::vector3& pos, math::vector3& vel, std::uintptr_t thrower_pawn, int sim_tick, bool& detonated ) const;
		void set_simulation_params( std::uintptr_t weapon_hash );
		[[nodiscard]] bool should_detonate( const math::vector3& vel, int tick ) const;
		[[nodiscard]] static math::vector3 clip_velocity( const math::vector3& velocity, const math::vector3& normal, float overbounce );

		void update_weapon_properties( std::uintptr_t weapon, std::uintptr_t weapon_vdata );
		void update_in_flight( const systems::local::snapshot& local );
		[[nodiscard]] std::uintptr_t get_other_name( std::uint32_t schema_hash );

		void correct_throw_angles( systems::input::usercmd* cmd, const systems::local::snapshot& local, std::uintptr_t weapon );
		void compute_desired_direction( math::vector3& desired_forward ) const;

		void compute_damage_at_endpoint( const trajectory& traj, std::vector<damage_info>& damages, const systems::local::snapshot& local ) const;

		void render_preview( xdraw::draw_list& draw_list, const systems::local::snapshot& local );
		void render_trajectory( xdraw::draw_list& draw_list, const trajectory& traj, float alpha, bool is_thrown, const systems::local::snapshot& local ) const;

		std::uintptr_t m_weapon_vdata{};
		std::uintptr_t m_weapon_hash{};
		float m_throw_velocity{};
		float m_detonate_time{ 1.5f };
		float m_velocity_threshold{ 0.1f };

		float m_sv_gravity{};
		float m_molotov_max_slope_z{};

		std::vector<in_flight_grenade> m_in_flight{};

		std::chrono::steady_clock::time_point m_last_throw_time{};
		bool m_was_holding{};
		bool m_should_preview{};

		bool m_was_forward_only{};
		bool m_delay_release{};
		bool m_delayed_attack2{};
		bool m_needs_air_stop{};
		bool m_throw_stopping{};
		float m_delayed_strength{ 1.0f };
		int m_delay_ticks{};
	};

	class impacts
	{
	public:
		void on_render_early( xdraw::draw_list& draw_list );
		void on_frame_stage_notify( );
		void on_level_change( );
		void on_render( xdraw::draw_list& draw_list );
		void on_report_hit( std::uintptr_t msg );
		void on_player_hurt( std::uintptr_t event );
		void on_bullet_impact( std::uintptr_t event );
		void on_base_fire_guns_get_inaccuracy( std::uintptr_t weapon, float inaccuracy );
		void on_get_interpolated_shoot_position( std::uintptr_t weapon_services, float* out );
		void on_boom( std::uintptr_t victim_pawn, int hitgroup, float damage, float hitchance, float inaccuracy, float spread, const math::vector3& aim_angle, const math::vector3& shoot_position, int tick, const std::array<systems::bones::data, 27>& skeleton, bool forced );

		[[nodiscard]] static std::vector<std::string> list_custom_sounds( );
		[[nodiscard]] static std::string custom_sounds_directory_narrow( );
		void play_custom_sound( std::string_view filename, float volume ) const;

	private:
		struct shot_record
		{
			std::uintptr_t victim_pawn{};
			int hitgroup{};
			float damage{};
			float hitchance{};
			float predicted_inaccuracy{};
			float predicted_spread{};
			float server_inaccuracy{};
			math::vector3 aim_angle{};
			math::vector3 shoot_position{};
			math::vector3 impact_position{};
			float best_impact_dist_sq{ FLT_MAX };
			int tick{};
			float time{};
			float impact_time{};
			std::array<systems::bones::data, 27> skeleton{};
			bool resolved{};
			bool server_confirmed{};
			bool impact_confirmed{};
			math::vector3 server_shoot_position{};
			bool server_shoot_position_confirmed{};
			math::vector3 target_velocity{};
			bool forced{};
			std::uint32_t weapon_type{};
		};

		struct hit_data
		{
			std::uintptr_t victim{};
			std::uintptr_t victim_pawn{};
			int damage{};
			int health{};
			int hitgroup{};
			int expected_hitgroup{};
			bool was_aimbot{};
			std::string mismatch_reason{};
			std::uint32_t weapon_type{};
			float expected_damage{};
		};

		struct hitmarker
		{
			math::vector3 position{};
			float time{};
			int damage{};
		};

		struct log
		{
			std::string name{};
			std::string hitgroup{};
			std::string reason{};
			int damage{};
			int health{};
			float time{};
			float duration{};
			animation::spring offset{};
			animation::fade alpha{};
			bool snapped{};
			bool is_miss{};
			std::uint32_t weapon_type{};
		};

		struct pending_hit
		{
			math::vector3 position{};
			float time{};
		};

		struct bullet_impact
		{
			math::vector3 position{};
			float time{};
		};

		[[nodiscard]] const char* classify_shot_deviation( const shot_record& shot ) const;
		[[nodiscard]] hit_data parse_event( std::uintptr_t event );
		[[nodiscard]] std::string get_player_name( std::uintptr_t controller );
		[[nodiscard]] std::string get_player_name_from_pawn( std::uintptr_t pawn );
		[[nodiscard]] float distance_to_nearest_hitbox( const shot_record& shot ) const;
		[[nodiscard]] float ray_distance_to_nearest_hitbox( const shot_record& shot, const math::vector3& direction ) const;

		void add_hit_log( const hit_data& data );
		void add_miss_log( const shot_record& shot, const char* reason );
		void check_misses( );

		void render_hit_markers( xdraw::draw_list& draw_list, float time );
		void render_logs( xdraw::draw_list& draw_list, float time );
		void render_hit_effect( xdraw::draw_list& draw_list, float time );
		void render_bullet_impact_overlays( xdraw::draw_list& draw_list, float time );

		void play_sound( settings::misc::impacts::sound_type type, float volume, std::string_view custom_file = {} );
		void play_hit_effect( std::uintptr_t victim_pawn );
		void play_death_effect( std::uintptr_t victim_pawn );
		void play_bullet_impact_effect( const math::vector3& position );
		void play_bullet_tracer( const math::vector3& position );
		void flush_buffered_impacts( );

		std::vector<hitmarker> m_hitmarkers{};
		std::vector<log> m_logs{};
		std::vector<pending_hit> m_pending_hits{};
		std::vector<shot_record> m_pending_shots{};
		std::vector<bullet_impact> m_bullet_impacts{};
		mutable std::mutex m_mtx{};

		bool m_death_effect_loaded{};
		bool m_bullet_impact_effect_loaded{};
		bool m_bullet_tracers_loaded{};

		float m_hit_effect_time{};

		std::vector<math::vector3> m_buffered_impacts{};
		math::vector3 m_buffered_eye_position{};
		float m_buffered_impact_time{ -1.0f };
	};

	class removals
	{
	public:
		void on_prepare_scene_material( std::uintptr_t material ) const;
		void on_override_view( std::uintptr_t view_setup ) const;
	};

	class camera
	{
	public:
		void on_override_view( std::uintptr_t view_setup );
		void update_fov_sensitivity( std::uintptr_t player_pawn ) const;

	private:
		void do_thirdperson( std::uintptr_t view_setup, std::uintptr_t local_pawn ) const;
		void do_fov_change( std::uintptr_t view_setup, std::uintptr_t local_pawn ) const;
		void do_aspect_ratio_change( std::uintptr_t view_setup );

		mutable float m_cached_fov_sensitivity{ -1.0f };
		mutable bool m_cached_scoped{};
		mutable float m_cached_target_fov{};
	};

	class hud
	{
	public:
		void on_render( xdraw::draw_list& draw_list );

	private:
		void do_crosshair( xdraw::draw_list& draw_list, float cx, float cy ) const;
		void do_scope( xdraw::draw_list& draw_list, float cx, float cy, float screen_h, std::uintptr_t local_pawn );
		void do_hat( xdraw::draw_list& draw_list, std::uintptr_t local_pawn ) const;
		void do_velocity( xdraw::draw_list& draw_list, float cx, float screen_h, std::uintptr_t local_pawn );

		static constexpr std::size_t k_velocity_history{ 120 };

		float m_scope_anim{};
		float m_spread_smooth{};
		std::uint32_t m_last_weapon{};
		float m_cached_spread_pixels{};
		int m_scope_update_frame{};
		std::array<float, k_velocity_history> m_velocity_history{};
		std::size_t m_velocity_history_count{};
		std::size_t m_velocity_history_head{};
		float m_velocity_smoothed{};
		float m_velocity_scale{};
	};

	class dlight
	{
	public:
		void on_present( );
		void on_frame_stage_notify( );
		void on_level_shutdown( );
		void apply_scene_color( std::uintptr_t object ) const;

	private:
		struct config_snapshot
		{
			bool enabled{};
			std::uint32_t packed_color{};
			float radius{ 300.0f };
			float z_offset{ 2.0f };
		};

		void retire_entry( );

		std::mutex m_mutex{};
		config_snapshot m_config{};
		std::atomic<std::uintptr_t> m_scene_object{};
		std::atomic<std::uint32_t> m_packed_color{};
		std::atomic<float> m_scene_color_scale{};
		std::uintptr_t m_manager{};
		std::uintptr_t m_entry{};
		bool m_logged_submission{};
		bool m_logged_scene{};
	};

	class other
	{
	public:
		void on_round_start( );
		void on_frame_stage_notify( );
		void do_kill_feed_preservation( );
		void on_player_death( std::uintptr_t event );

		[[nodiscard]] bool is_alpha_changed( ) const { return this->m_is_alpha_changed; }

		static inline std::string s_display_name{};
		static inline bool s_name_change_pending{};

	private:
		void do_autobuy( ) const;
		void do_player_alpha_changing( );
		void do_reveal_radar( ) const;
		void do_name_changing( );
		void do_viewmodel_adjust( );
		bool m_is_alpha_changed{};
		bool m_name_changer_active{};
		std::uintptr_t m_name_changer_controller{};
		std::string m_original_name{};
		std::string m_last_sent_name{};
		float m_last_spawntime{};
		float m_cached_vm_x{ std::numeric_limits<float>::quiet_NaN( ) };
		float m_cached_vm_y{ std::numeric_limits<float>::quiet_NaN( ) };
		float m_cached_vm_z{ std::numeric_limits<float>::quiet_NaN( ) };
		float m_cached_vm_fov{ std::numeric_limits<float>::quiet_NaN( ) };
	};

	// this is so ghetto but fuck it for now it works


	struct c_panel_2d {

	};

	struct c_top_level_window_source_2 {

	};

	class c_ui_panel {
	public:
		void* m_vtable; //0x0000
		c_panel_2d* m_panel; //0x0008
		char* m_panel_name; //0x0010
		c_ui_panel* m_parent_panel; //0x0018
		c_top_level_window_source_2* m_ui_window; //0x0020
		int32_t m_child_ui_panel_count; //0x0028
		std::uint8_t pad_0000 [0x4]; //0x002C
		c_ui_panel** m_children_ui_panel_array; //0x0030
		std::uint8_t pad_0001 [0x18]; //0x0038
		float m_width; //0x0050
		float m_height; //0x0054
		std::uint8_t pad_0002 [0x270]; //0x0058
	};

	//class c_ui_engine {
	//public:
	//	void run_script (c_ui_panel* panel, const char* script) {
	//		const auto vmt = *reinterpret_cast<std::uintptr_t*>(this);
	//		const auto fn = *reinterpret_cast<std::uintptr_t*>(vmt + 77 * sizeof (std::uintptr_t));

	//		using fn_t = void (__thiscall*)(c_ui_engine*, c_ui_panel*, const char*, const char*, std::uint64_t);
	//		reinterpret_cast<fn_t>(fn)(this, panel, script, nullptr, 0);
	//	}
	//};

	struct panel_data_t {
		std::uint8_t pad_0000 [8];
		uint32_t m_flags;
		uint32_t m_visible;
		c_ui_panel* m_panel;
		uint32_t m_panel_id;
		std::uint8_t pad_0001 [4];
	};

	class c_ui_engine {
	public:
		void* m_vtable; //0x0000
		std::uint8_t pad_0000 [0x220]; //0x002C
		panel_data_t* m_panels_array; //0x0228
		int32_t m_panel_count; //0x0230
		std::uint8_t pad_0001 [0x3CC]; //0x002C
		void* m_isolate; //0x0600
		std::uint8_t pad_0002 [0x2E0]; //0x002C

		void* m_panel_stack [63]; //0x08E8
		int32_t m_panel_stack_depth; //0x0AE0
		std::uint8_t pad_0003 [0x8C]; //0x002C
		void* m_script_cache; //0x0B70
		std::uint8_t pad_0004 [0x10]; //0x002C

		void run_script (c_ui_panel* panel, const char* script);

	};

	class c_panorama_ui_engine {
	private:
		std::uint8_t pad_0001 [40]; //0x002C
		c_ui_engine* m_ui_engine; //0x0028
	public:
		c_ui_engine* get_ui_engine ();
	};

	class scoreboard_weapons {
	public:
		void on_frame_stage_notify ();
		void on_level_change ();

	private:
		struct weapon_entry {
			std::string name {};
			int         type {};

			bool operator==(const weapon_entry& o) const {
				return name == o.name && type == o.type;
			}
		};

		struct player_weapon_state {
			std::vector<weapon_entry> weapons {};
			std::string               active_name {};

			bool operator==(const player_weapon_state& o) const {
				return weapons == o.weapons && active_name == o.active_name;
			}
		};

		void try_initialize ();
		[[nodiscard]] c_ui_panel* find_hud_panel () const;
		[[nodiscard]] bool run_script (const std::string& script);
		void send_player_weapons (
			std::uintptr_t controller,
			std::span<const systems::entities::cached> items);
		[[nodiscard]] bool send_clear (std::uint64_t steamid);
		void clear_all ();

		bool                                                 m_script_injected {};
		bool                                                 m_scoreboard_open {};
		std::unordered_map<std::uint64_t, player_weapon_state> m_cache {};
		int                                                  m_throttle {};
		int                                                  m_init_throttle {};

		// The persistent HUD panel hosts scripts; the scoreboard itself is rebuilt
		// whenever it is opened and is resolved from JavaScript at update time.
		c_ui_engine* m_ui_engine {};
		c_ui_panel* m_script_panel {};
	};

} // namespace features::misc
