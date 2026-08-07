#include <pch/pch.hpp>
#include <utilities/diag.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>
#include <cctype>
#include <cstring>
#include "../world.hpp"

namespace features::world {

	namespace {
		constexpr std::array skybox_names {
			"Map default", "Vertigo", "Mirage", "Dust 2", "Nuke", "Anubis",
			"Overpass", "Train", "Aztec", "Italy", "Office", "Cloudy",
			"Rain Night", "Daylight", "Jungle", "Sunset"
		};

		constexpr std::array skybox_paths {
			"",
			"materials/skybox/sky_de_vertigo_exr_c70a3937.vtex",
			"materials/skybox/sky_de_mirage_exr_71e5f2a1.vtex",
			"materials/skybox/sky_de_dust2_exr_908a35ba.vtex",
			"materials/skybox/sky_de_nuke_exr_f04e84b2.vtex",
			"materials/skybox/sky_de_annubis_exr_2c5e0b53.vtex",
			"materials/skybox/sky_de_overpass_01_exr_f8534391.vtex",
			"materials/skybox/sky_de_train03_exr_4fdb8a38.vtex",
			"materials/skybox/sky_hr_aztec_02_exr_f84f8de9.vtex",
			"materials/skybox/cs_italy_s2_skybox_sunset_2_exr_e56cedf6.vtex",
			"materials/skybox/sky_cs_office_45_0_exr_d0152542.vtex",
			"materials/skybox/sky_csgo_cloudy01_cube_pfm_f9a0b177.vtex",
			"materials/skybox/sky_rain_night_01_exr_8d775aee.vtex",
			"materials/skybox/sky_cs15_daylight01_hdr_cube_pfm_a4b050d1.vtex",
			"materials/skybox/jungle_cube_pfm_bc16d813.vtex",
			"materials/skybox/tests/src/lightingtest_sky_sunset_light_exr_f7b19a45.vtex"
		};
		static_assert (skybox_names.size () == skybox_paths.size ());

		namespace shader_hash {
			// Source 2's case-insensitive Murmur2 shader parameter hashes.
			constexpr std::uint32_t wind_direction{ 0x2A416C12 };
			constexpr std::uint32_t wind_strength_frequency{ 0xEB0D997E };
			constexpr std::uint32_t rain_exposure_to_sky{ 0x374C1B3C };
			constexpr std::uint32_t rain_timer{ 0x2DBEE393 };
			constexpr std::uint32_t rain_wetness{ 0x0F592812 };
			constexpr std::uint32_t gradient_fog{ 0x4B01FF63 };
			constexpr std::uint32_t gradient_fog_2{ 0x0AA49C2A };
			constexpr std::uint32_t gradient_fog_3{ 0xFBF6448D };
			constexpr std::uint32_t enable_gradient_fog{ 0x6E0FAD7E };
		}

		[[nodiscard]] std::uint32_t safe_material_hash_impl (std::uintptr_t material) noexcept {
			if (!material) {
				return 0;
			}

			__try {
				const auto name = memory::call_vfunc<const char*> (material, 0);
				return name ? fnv1a::runtime_hash (name) : 0;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return 0;
			}
		}

		[[nodiscard]] std::uint32_t safe_material_hash (std::uintptr_t material) noexcept {
			diag::probe_scope probe;
			return safe_material_hash_impl (material);
		}

		[[nodiscard]] std::filesystem::path find_skybox_directory () {
			std::array<wchar_t, 32768> module_path {};
			const auto length = GetModuleFileNameW (
				nullptr, module_path.data (), static_cast<DWORD> (module_path.size ()));
			if (!length || length >= module_path.size ()) {
				return {};
			}

			std::filesystem::path root {module_path.data (), module_path.data () + length};
			std::error_code error;
			for (auto depth = 0; depth < 6 && root.has_parent_path (); ++depth) {
				root = root.parent_path ();
				const auto candidate = root / L"game" / L"csgo" / L"materials" / L"skybox";
				if (std::filesystem::is_directory (candidate, error)) {
					return candidate;
				}
				error.clear ();
			}

			return {};
		}

		[[nodiscard]] std::string prettify_skybox_name (std::string name) {
			constexpr std::array prefixes {
				"sky_de_", "sky_hr_", "sky_cs15_", "sky_cs_", "sky_csgo_", "sky_", "cs_"
			};
			for (const auto* prefix : prefixes) {
				const auto length = std::strlen (prefix);
				if (name.size () > length && name.compare (0, length, prefix) == 0) {
					name.erase (0, length);
					break;
				}
			}

			auto capitalize = true;
			for (auto& character : name) {
				if (character == '_') {
					character = ' ';
					capitalize = true;
					continue;
				}

				const auto value = static_cast<unsigned char> (character);
				character = static_cast<char> (capitalize ? std::toupper (value) : std::tolower (value));
				capitalize = false;
			}

			return name;
		}

		[[nodiscard]] bool is_safe_sky_texture_path (std::string_view path) {
			if (path.empty () || path.size () > 512 || !path.ends_with (".vtex")) {
				return false;
			}

			return std::ranges::none_of (path, [] (const unsigned char character) {
				return character < 0x20 || character == '"' || character == '\'';
			});
		}

	} // namespace

	void scene::discover_skyboxes () {
		this->m_skyboxes.clear ();
		this->m_skyboxes.reserve (skybox_paths.size () + 16);
		for (auto index = std::size_t {}; index < skybox_paths.size (); ++index) {
			this->m_skyboxes.push_back ({skybox_names [index], skybox_paths [index]});
		}

		const auto directory = find_skybox_directory ();
		if (directory.empty ()) {
			return;
		}

		std::vector<skybox_entry> custom_skyboxes;
		std::error_code error;
		for (const auto& file : std::filesystem::directory_iterator (
			directory, std::filesystem::directory_options::skip_permission_denied, error)) {
			if (error) {
				break;
			}
			if (!file.is_regular_file (error)) {
				continue;
			}

			const auto filename = file.path ().filename ().string ();
			if (filename.size () < 8 || !filename.ends_with (".vtex_c")) {
				continue;
			}

			const auto base = filename.substr (0, filename.size () - 7);
			auto lower = base;
			std::ranges::transform (lower, lower.begin (), [] (const unsigned char character) {
				return static_cast<char> (std::tolower (character));
			});
			if (lower.contains ("_fog") || lower.contains ("_lighting") || lower.contains ("_v1")) {
				continue;
			}

			auto resource_path = std::string {"materials/skybox/"} + base + ".vtex";
			const auto duplicate = [&resource_path] (const skybox_entry& entry) {
				return entry.resource_path == resource_path;
			};
			if (std::ranges::any_of (this->m_skyboxes, duplicate) ||
				std::ranges::any_of (custom_skyboxes, duplicate)) {
				continue;
			}

			custom_skyboxes.push_back ({prettify_skybox_name (base), std::move (resource_path)});
		}

		std::ranges::sort (custom_skyboxes, {}, &skybox_entry::display_name);
		this->m_skyboxes.insert (
			this->m_skyboxes.end (),
			std::make_move_iterator (custom_skyboxes.begin ()),
			std::make_move_iterator (custom_skyboxes.end ()));
	}

	void scene::reset_skybox_state () {
		this->m_custom_sky_material = 0;
		this->m_loaded_skybox_index = -1;
	}

	void scene::on_frame_stage_notify () {
		const auto local = systems::g_local.get ();

		if (!local.pawn || !local.is_alive) {
			this->reset_skybox_state ();
			return;
		}

		const auto& config = settings::g_world.m_scene.skybox;
		if (!config.custom_skybox || this->m_skyboxes.empty ()) {
			return;
		}

		const auto idx = std::clamp (config.selected_skybox.value, 0, static_cast<int>(this->m_skyboxes.size ()) - 1);
		if (idx != this->m_loaded_skybox_index) {
			this->load_skybox_material (this->m_skyboxes [idx].resource_path.c_str ());
			this->m_loaded_skybox_index = idx;
		}
	}

	void scene::on_draw_skybox_array_pre (std::uintptr_t mesh_array, int mesh_count) {
		this->m_active_material_binding = 0;
		this->m_active_original_material = 0;
		this->m_active_skybox_descriptor = 0;
		this->m_active_sky_tinted = false;

		if (!mesh_array || mesh_count <= 0 || !systems::g_local.get ().pawn) {
			return;
		}

		const auto& config = settings::g_world.m_scene.skybox;
		const auto replace_material = config.custom_skybox.value && this->m_custom_sky_material;
		if (!replace_material && !config.custom_color.value) {
			return;
		}

		// Current scenesystem sky records use a 0x70-byte stride; the final
		// record stores its descriptor pointer at -0x58.
		const auto skybox_object = memory::safe_read<std::uintptr_t> (
			mesh_array + (static_cast<std::size_t>(mesh_count) * 0x70) - 0x58);
		if (!skybox_object || !*skybox_object) {
			return;
		}
		this->m_active_skybox_descriptor = *skybox_object;

		if (replace_material) {
			const auto material_binding = memory::safe_read<std::uintptr_t> (*skybox_object + 0xD0);
			const auto original_material = material_binding && *material_binding
				? memory::safe_read<std::uintptr_t> (*material_binding)
				: std::nullopt;
			if (original_material && *original_material &&
				memory::safe_write<std::uintptr_t> (*material_binding, this->m_custom_sky_material)) {
				this->m_active_material_binding = *material_binding;
				this->m_active_original_material = *original_material;
			}
		}

		if (config.custom_color.value) {
			const auto original_color = memory::safe_read<std::array<float, 3>> (*skybox_object + 0xE8);
			const auto color = config.skybox_color.value.to_float ();
			const std::array<float, 3> replacement {color [0], color [1], color [2]};
			if (original_color && memory::safe_write (*skybox_object + 0xE8, replacement)) {
				this->m_active_original_sky_color = *original_color;
				this->m_active_sky_tinted = true;
			}
		}
	}

	void scene::on_draw_skybox_array_post () {
		if (this->m_active_material_binding && this->m_active_original_material) {
			(void) memory::safe_write<std::uintptr_t> (
				this->m_active_material_binding, this->m_active_original_material);
		}

		if (this->m_active_sky_tinted && this->m_active_skybox_descriptor) {
			(void) memory::safe_write (
				this->m_active_skybox_descriptor + 0xE8, this->m_active_original_sky_color);
		}

		this->m_active_material_binding = 0;
		this->m_active_original_material = 0;
		this->m_active_skybox_descriptor = 0;
		this->m_active_sky_tinted = false;
	}

	void scene::on_light_scene_object_pre (std::uintptr_t object) const {
		if (!object || !settings::g_world.m_scene.lighting.value) {
			return;
		}

		const auto color = settings::g_world.m_scene.lighting_color.value.to_float ();

		memory::write<float> (object + 0xe4, color [0] * settings::g_world.m_scene.lighting_intensity);
		memory::write<float> (object + 0xe8, color [1] * settings::g_world.m_scene.lighting_intensity);
		memory::write<float> (object + 0xec, color [2] * settings::g_world.m_scene.lighting_intensity);
	}

	void scene::on_light_scene_object_post (std::uintptr_t object) const {
		if (!object || !settings::g_world.m_scene.lighting.value) {
			return;
		}

		//auto rotation = settings::g_world.m_scene.lighting_rotation;
		//rotation.normalize( );

		//memory::write<math::vector3>( object + 0x184, rotation );
	}

	void scene::on_draw_scene_object_array (std::uintptr_t object_array) const {
		if (!object_array || !settings::g_world.m_scene.world_setting.value) {
			return;
		}

		const auto object_data = memory::safe_read<std::uintptr_t> (object_array + 0x8);
		if (!object_data || !*object_data) {
			return;
		}

		const auto light_data_queue = memory::safe_read<std::uintptr_t> (addresses::globals::light_data_queue);
		if (!light_data_queue || !*light_data_queue) {
			return;
		}

		const auto light_data_base = memory::safe_read<std::uintptr_t> (*light_data_queue + 0x18);
		if (!light_data_base || !*light_data_base) {
			return;
		}

		const auto count = memory::safe_read<int> (*object_data + 0x4);
		const auto index = memory::safe_read<int> (*object_data + 0x30);
		if (!count || !index || *count <= 0 || *count > (1 << 20) ||
			*index < 0 || *index > (1 << 22)) {
			return;
		}

		const auto& configured = settings::g_world.m_scene.world_color.value;
		for (auto i = 0; i < *count; ++i) {
			const auto color_addr = *light_data_base +
				((static_cast<std::size_t> (*index) + i) << 5);
			const auto current = memory::safe_read<xdraw::color> (color_addr);
			if (!current) {
				continue;
			}

			(void) memory::safe_write<xdraw::color> (
				color_addr, {configured.r, configured.g, configured.b, current->a});
		}
	}

	void scene::on_draw_scene_object (std::uintptr_t batch, int batch_count) const {
		if (!batch || batch_count <= 0 || batch_count > (1 << 20)) {
			return;
		}

		const auto& config = settings::g_world.m_scene.skybox;
		if (!config.custom_color.value && !settings::g_world.m_scene.world_setting.value) {
			return;
		}

		constexpr std::array cloud_materials
		{
			"materials/effects/clouds_001.vmat"_hash,
			"materials/de_vertigo/vertigo_clouds_001.vmat"_hash,
			"materials/models/props/de_nuke/hr_nuke/nuke_skydome_001/nuke_clouds_003.vmat"_hash,
			"materials/models/props/de_nuke/hr_nuke/nuke_skydome_001/nuke_clouds_002.vmat"_hash,
			"materials/models/props/de_nuke/hr_nuke/nuke_skydome_001/nuke_clouds_001.vmat"_hash
		};

		constexpr std::array sun_materials
		{
			"materials/sun/overlay.vmat"_hash,
			"materials/effects/glows/sun_glow_001.vmat"_hash,
			"materials/effects/glows/sun_disc_glow_001.vmat"_hash,
			"materials/effects/glows/sun_disc_glow_003.vmat"_hash,
			"materials/effects/glows/sun_disc_glow_004.vmat"_hash,
			"materials/de_train/hr_train_s2/effects/sun_disc_glow_01_clouded.vmat"_hash
		};

		for (auto i = 0; i < batch_count; ++i) {
			// Current scenesystem.dll mesh primitives are 0x70 bytes.
			const auto mesh = batch + (static_cast<std::size_t> (i) * 0x70);
			const auto material = memory::safe_read<std::uintptr_t> (mesh + 0x20);

			if (!material || !*material) {
				continue;
			}

			const auto material_hash = safe_material_hash (*material);
			if (!material_hash) {
				continue;
			}

			const auto is_cloud = std::ranges::contains (cloud_materials, material_hash);
			const auto is_sun = std::ranges::contains (sun_materials, material_hash);

			if ((is_cloud || is_sun) && config.custom_color.value) {
				const auto& color = is_cloud ? config.cloud_color.value : config.sun_color.value;
				(void) memory::safe_write<std::uint32_t> (mesh + 0x50, color);
			} else if (!is_cloud && !is_sun && settings::g_world.m_scene.world_setting.value) {
				(void) memory::safe_write<std::uint32_t> (
					mesh + 0x50, settings::g_world.m_scene.world_color.value);
			}
		}
	}

	bool scene::on_setup_fog (__m128i* output, int* mode) const {
		const auto& fog = settings::g_world.m_weather;
		if (!fog.fog_enabled.value || !output || !mode) {
			return false;
		}

		const auto set_param_f = PATTERN (patterns::set_shader_param);
		const auto set_param_i = PATTERN (patterns::set_shader_param_i);
		if (!set_param_f || !set_param_i) {
			return false;
		}

		const auto falloff = 0.5f + fog.fog_anisotropy.value * 8.0f;
		const auto& color = fog.fog_color.value;
		const __m128 params = _mm_set_ps (0.0f, 0.0f, fog.fog_draw_distance.value, 0.0f);
		const __m128 params_2 = _mm_set_ps (0.0f, 0.0f, falloff, fog.fog_density.value);
		const __m128 params_3 = _mm_set_ps (
			0.0f,
			static_cast<float> (color.b) / 255.0f,
			static_cast<float> (color.g) / 255.0f,
			static_cast<float> (color.r) / 255.0f);

		memory::call<std::uintptr_t> (set_param_f, output, shader_hash::gradient_fog, &params);
		memory::call<std::uintptr_t> (set_param_f, output, shader_hash::gradient_fog_2, &params_2);
		memory::call<std::uintptr_t> (set_param_f, output, shader_hash::gradient_fog_3, &params_3);
		memory::call<std::uintptr_t> (set_param_i, output + 17, shader_hash::enable_gradient_fog, 1);
		*mode = 0;
		return true;
	}

	void scene::on_set_shader_param (__m128i*& value, std::uint32_t hash) const {
		static __m128 dof_val;
		static __m128 wind_direction_val;
		static __m128 wind_strength_frequency_val;
		static __m128 bloom_scale_val;
		static __m128 bloom_threshold_val;
		static __m128 bloom_width_val;
		static __m128 bloom_strength_val;
		static __m128 bloom_skybox_val;
		static __m128 gamma_val;
		static __m128 wetness_sky_val;
		static __m128 wetness_density_val;
		static __m128 wetness_timer_val;

		if (settings::g_world.m_scene.dof.value && hash == 0x2ACAB07C) {
			dof_val = _mm_set_ps (
				settings::g_world.m_scene.dof_far_blurry,
				settings::g_world.m_scene.dof_far_crisp,
				settings::g_world.m_scene.dof_near_crisp,
				settings::g_world.m_scene.dof_near_blurry);
			value = reinterpret_cast<__m128i*> (&dof_val);
		}

		const auto& weather = settings::g_world.m_weather;
		if (weather.wind.value && hash == shader_hash::wind_direction) {
			const auto direction = weather.wind_direction.value *
				(std::numbers::pi_v<float> / 180.0f);
			wind_direction_val = _mm_set_ps (
				0.0f, 0.0f, std::sin (direction), std::cos (direction));
			value = reinterpret_cast<__m128i*> (&wind_direction_val);
		} else if (weather.wind.value && hash == shader_hash::wind_strength_frequency) {
			// Foliage shaders pack the low/high sway strength and frequency into one vector.
			wind_strength_frequency_val = _mm_set_ps (
				weather.wind_turbulence.value,
				weather.wind_strength.value,
				weather.wind_turbulence.value,
				weather.wind_strength.value);
			value = reinterpret_cast<__m128i*> (&wind_strength_frequency_val);
		}

		if (settings::g_world.m_scene.bloom.value) {
			const auto t = settings::g_world.m_scene.bloom_value;
			if (hash == 0x565EAF76) {
				bloom_scale_val = _mm_set_ps1 (0.3f + t * 1.2f);
				value = (__m128i*) & bloom_scale_val;
			} else if (hash == 0xBA98A9B0) {
				bloom_threshold_val = _mm_set_ps1 (1.5f - t * 1.2f);
				value = (__m128i*) & bloom_threshold_val;
			} else if (hash == 0x2AE72B37) {
				bloom_width_val = _mm_set_ps1 (0.5f + t * 1.5f);
				value = (__m128i*) & bloom_width_val;
			} else if (hash == 0xB692902E) {
				bloom_strength_val = _mm_set_ps1 (0.2f + t * 0.6f);
				value = (__m128i*) & bloom_strength_val;
			} else if (hash == 0x1313A424) {
				bloom_skybox_val = _mm_set_ps1 (0.1f + t * 0.4f);
				value = (__m128i*) & bloom_skybox_val;
			}
		}

		if (settings::g_world.m_scene.gamma.value && hash == 0x24470A87) {
			gamma_val = _mm_set_ps1 (settings::g_world.m_scene.gamma_value);
			value = (__m128i*) & gamma_val;
		}

		const auto& wetness = settings::g_world.m_weather;
		if (wetness.wetness.value && hash == shader_hash::rain_exposure_to_sky) {
			wetness_sky_val = _mm_set_ps1 (1.0f);
			value = (__m128i*) & wetness_sky_val;
		} else if (wetness.wetness.value && hash == shader_hash::rain_wetness) {
			wetness_density_val = _mm_set_ps1 (wetness.wetness_density.value);
			value = (__m128i*) & wetness_density_val;
		} else if (wetness.wetness.value && hash == shader_hash::rain_timer) {
			const auto seconds = static_cast<float> (GetTickCount64 ()) * 0.001f;
			wetness_timer_val = _mm_set_ps1 (seconds * wetness.wetness_speed.value);
			value = (__m128i*) & wetness_timer_val;
		}
	}

	void scene::load_skybox_material (const char* path) {
		this->m_custom_sky_material = 0;
		if (!path || !*path) {
			return;
		}

		if (const auto cached = this->m_skybox_materials.find (path);
			cached != this->m_skybox_materials.end ()) {
			this->m_custom_sky_material = cached->second.material;
			return;
		}

		if (!is_safe_sky_texture_path (path)) {
			logging::console::print (xs ("invalid skybox texture path."));
			return;
		}

		struct buffer_string {
			std::uint32_t m_unknown1 {};
			std::uint32_t m_unknown2 {0xc00000c8};

			union {
				std::uintptr_t m_str_ptr;
				std::uint8_t data [0xc8];
			};

			std::uintptr_t m_unknown3 {};
			std::uintptr_t m_unknown4 {};
		} buffer;

		const auto init_path_buffer = PATTERN (patterns::init_particle_path_buffer);
		const auto precache_resource = PATTERN (patterns::resource_system_precache);
		if (!init_path_buffer || !precache_resource || !addresses::globals::resource_system) {
			return;
		}

		memory::call<void> (init_path_buffer, &buffer, path);
		buffer.m_unknown4 = 'xetv';

		memory::call<void> (precache_resource, addresses::globals::resource_system, &buffer, "");

		memory::call<void> (init_path_buffer, &buffer, path);
		buffer.m_unknown4 = 'xetv';

		const auto binding = memory::call_vfunc<std::uintptr_t> (addresses::globals::resource_system, 79, &buffer, 0ll);
		const auto texture = binding ? memory::safe_read<std::uintptr_t> (binding) : std::nullopt;
		if (!texture || !*texture) {
			logging::console::print (xs ("failed to preload skybox texture."));
			return;
		}

		const auto material_source = std::format (R"VMAT(<!-- kv3 encoding:text:version{{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d}} format:generic:version{{7412167c-06e9-4698-aff2-e63eb59037e7}} -->
{{
	Shader = "sky.vfx"
	g_flBrightnessExposureBias = 0.0
	g_flRenderOnlyExposureBias = 0.0
	SkyTexture = resource:"{}"
	g_tSkyTexture = resource:"{}"
}}
)VMAT", path, path);
		const auto material_name = std::format (
			"velocity_skybox_{:08x}", fnv1a::runtime_hash (path));
		const auto material = systems::materials::load (material_source.c_str (), material_name.c_str ());
		if (!material) {
			logging::console::print (xs ("failed to create skybox material."));
			return;
		}

		this->m_skybox_materials.emplace (path, cached_skybox_material {binding, material});
		this->m_custom_sky_material = material;
	}

} // namespace features::world
