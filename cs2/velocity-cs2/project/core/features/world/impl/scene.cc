module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>

#include "protection/address_macros.h"

module velocity;

import <cctype>;
import <cstddef>;
import <cstring>;
import <filesystem>;
import <string_view>;
import <type_traits>;

namespace features::world {

namespace {
constexpr std::array kSkyboxNames{
    "Map default", "Vertigo",  "Mirage", "Dust 2", "Nuke",   "Anubis",
    "Overpass",    "Train",    "Aztec",  "Italy",  "Office", "Cloudy",
    "Rain Night",  "Daylight", "Jungle", "Sunset"};

constexpr std::array kSkyboxPaths{
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
    "materials/skybox/tests/src/"
    "lightingtest_sky_sunset_light_exr_f7b19a45.vtex"};
static_assert(kSkyboxNames.size() == kSkyboxPaths.size());

namespace shader_hash {
// Source 2's case-insensitive Murmur2 shader parameter hashes.
constexpr std::uint32_t kWindDirection{0xCDE54581};
constexpr std::uint32_t kWindStrengthFrequency{0xEB0D997E};
constexpr std::uint32_t kRainExposureToSky{0x374C1B3C};
constexpr std::uint32_t kRainTimer{0x2DBEE393};
constexpr std::uint32_t kRainWetness{0x0F592812};
constexpr std::uint32_t kGradientFog{0x4B01FF63};
constexpr std::uint32_t kGradientFog2{0x0AA49C2A};
constexpr std::uint32_t kGradientFog3{0xFBF6448D};
constexpr std::uint32_t kEnableGradientFog{0x6E0FAD7E};
}  // namespace shader_hash

[[nodiscard]] std::uint32_t SafeMaterialHashImpl(
    std::uintptr_t material) noexcept {
  if (!material) {
    return 0;
  }

  __try {
    const auto name = memory::CallVfunc<const char*>(material, 0);
    return name ? fnv1a::RuntimeHash(name) : 0;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0;
  }
}

[[nodiscard]] std::uint32_t SafeMaterialHash(std::uintptr_t material) noexcept {
  diag::ProbeScope probe;
  return SafeMaterialHashImpl(material);
}

[[nodiscard]] std::filesystem::path FindSkyboxDirectory() {
  std::array<wchar_t, 32768> module_path{};
  const auto length = GetModuleFileNameW(
      nullptr, module_path.data(), static_cast<DWORD>(module_path.size()));
  if (!length || length >= module_path.size()) {
    return {};
  }

  std::filesystem::path root{module_path.data(), module_path.data() + length};
  std::error_code error;
  for (auto depth = 0; depth < 6 && root.has_parent_path(); ++depth) {
    root = root.parent_path();
    const auto candidate = root / L"game" / L"csgo" / L"materials" / L"skybox";
    if (std::filesystem::is_directory(candidate, error)) {
      return candidate;
    }
    error.clear();
  }

  return {};
}

[[nodiscard]] std::string PrettifySkyboxName(std::string name) {
  constexpr std::array kPrefixes{"sky_de_",   "sky_hr_", "sky_cs15_", "sky_cs_",
                                 "sky_csgo_", "sky_",    "cs_"};
  for (const auto* prefix : kPrefixes) {
    const auto length = std::strlen(prefix);
    if (name.size() > length && name.compare(0, length, prefix) == 0) {
      name.erase(0, length);
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

    const auto value = static_cast<unsigned char>(character);
    character = static_cast<char>(capitalize ? std::toupper(value)
                                             : std::tolower(value));
    capitalize = false;
  }

  return name;
}

[[nodiscard]] bool IsSafeSkyTexturePath(std::string_view path) {
  if (path.empty() || path.size() > 512 || !path.ends_with(".vtex")) {
    return false;
  }

  return std::ranges::none_of(path, [](const unsigned char character) {
    return character < 0x20 || character == '"' || character == '\'';
  });
}

}  // namespace

void Scene::DiscoverSkyboxes() {
  this->m_skyboxes_.clear();
  this->m_skyboxes_.reserve(kSkyboxPaths.size() + 16);
  for (auto index = std::size_t{}; index < kSkyboxPaths.size(); ++index) {
    this->m_skyboxes_.push_back({kSkyboxNames[index], kSkyboxPaths[index]});
  }

  const auto directory = FindSkyboxDirectory();
  if (directory.empty()) {
    return;
  }

  std::vector<SkyboxEntry> custom_skyboxes;
  std::error_code error;
  for (const auto& file : std::filesystem::directory_iterator(
           directory,
           std::filesystem::directory_options::skip_permission_denied, error)) {
    if (error) {
      break;
    }
    if (!file.is_regular_file(error)) {
      continue;
    }

    const auto filename = file.path().filename().string();
    if (filename.size() < 8 || !filename.ends_with(".vtex_c")) {
      continue;
    }

    const auto base = filename.substr(0, filename.size() - 7);
    auto lower = base;
    std::ranges::transform(lower, lower.begin(),
                           [](const unsigned char character) {
                             return static_cast<char>(std::tolower(character));
                           });
    if (lower.find("_fog") != std::string::npos ||
        lower.find("_lighting") != std::string::npos ||
        lower.find("_v1") != std::string::npos) {
      continue;
    }

    auto resource_path = std::string{"materials/skybox/"} + base + ".vtex";
    const auto duplicate = [&resource_path](const SkyboxEntry& entry) {
      return entry.resource_path == resource_path;
    };
    if (std::ranges::any_of(this->m_skyboxes_, duplicate) ||
        std::ranges::any_of(custom_skyboxes, duplicate)) {
      continue;
    }

    custom_skyboxes.push_back(
        {PrettifySkyboxName(base), std::move(resource_path)});
  }

  std::ranges::sort(custom_skyboxes, {}, &SkyboxEntry::display_name);
  this->m_skyboxes_.insert(this->m_skyboxes_.end(),
                           std::make_move_iterator(custom_skyboxes.begin()),
                           std::make_move_iterator(custom_skyboxes.end()));
}

void Scene::ResetSkyboxState() {
  this->m_custom_sky_material_ = 0;
  this->m_loaded_skybox_index_ = -1;
}

void Scene::OnFrameStageNotify() {
  const auto local = systems::g_local.Get();

  if (!local.pawn || !local.is_alive) {
    this->ResetSkyboxState();
    return;
  }

  const auto& config = settings::g_world.m_scene.skybox;
  if (!config.custom_skybox || this->m_skyboxes_.empty()) {
    return;
  }

  const auto idx = std::clamp(config.selected_skybox.value, 0,
                              static_cast<int>(this->m_skyboxes_.size()) - 1);
  if (idx != this->m_loaded_skybox_index_) {
    this->LoadSkyboxMaterial(this->m_skyboxes_[idx].resource_path.c_str());
    this->m_loaded_skybox_index_ = idx;
  }
}

void Scene::OnDrawSkyboxArrayPre(std::uintptr_t mesh_array, int mesh_count) {
  this->m_active_material_binding_ = 0;
  this->m_active_original_material_ = 0;
  this->m_active_skybox_descriptor_ = 0;
  this->m_active_sky_tinted_ = false;

  if (!mesh_array || mesh_count <= 0 || !systems::g_local.Get().pawn) {
    return;
  }

  const auto& config = settings::g_world.m_scene.skybox;
  const auto replace_material =
      config.custom_skybox.value && this->m_custom_sky_material_;
  if (!replace_material && !config.custom_color.value) {
    return;
  }

  // Current scenesystem sky records use a 0x70-byte stride; the final
  // record stores its descriptor pointer at -0x58.
  const auto skybox_object = memory::SafeRead<std::uintptr_t>(
      mesh_array + (static_cast<std::size_t>(mesh_count) * 0x70) - 0x58);
  if (!skybox_object || !*skybox_object) {
    return;
  }
  this->m_active_skybox_descriptor_ = *skybox_object;

  if (replace_material) {
    const auto material_binding =
        memory::SafeRead<std::uintptr_t>(*skybox_object + 0xD0);
    const auto original_material =
        material_binding && *material_binding
            ? memory::SafeRead<std::uintptr_t>(*material_binding)
            : std::nullopt;
    if (original_material && *original_material &&
        memory::SafeWrite<std::uintptr_t>(*material_binding,
                                          this->m_custom_sky_material_)) {
      this->m_active_material_binding_ = *material_binding;
      this->m_active_original_material_ = *original_material;
    }
  }

  if (config.custom_color.value) {
    const auto original_color =
        memory::SafeRead<std::array<float, 3>>(*skybox_object + 0xE8);
    const auto color = config.skybox_color.value.to_float();
    const std::array<float, 3> replacement{color[0], color[1], color[2]};
    if (original_color &&
        memory::SafeWrite(*skybox_object + 0xE8, replacement)) {
      this->m_active_original_sky_color_ = *original_color;
      this->m_active_sky_tinted_ = true;
    }
  }
}

void Scene::OnDrawSkyboxArrayPost() {
  if (this->m_active_material_binding_ && this->m_active_original_material_) {
    (void)memory::SafeWrite<std::uintptr_t>(this->m_active_material_binding_,
                                            this->m_active_original_material_);
  }

  if (this->m_active_sky_tinted_ && this->m_active_skybox_descriptor_) {
    (void)memory::SafeWrite(this->m_active_skybox_descriptor_ + 0xE8,
                            this->m_active_original_sky_color_);
  }

  this->m_active_material_binding_ = 0;
  this->m_active_original_material_ = 0;
  this->m_active_skybox_descriptor_ = 0;
  this->m_active_sky_tinted_ = false;
}

void Scene::OnLightSceneObjectPre(std::uintptr_t object) const {
  if (!object || !settings::g_world.m_scene.lighting.value) {
    return;
  }

  const auto color = settings::g_world.m_scene.lighting_color.value.to_float();

  memory::Write<float>(object + 0xe4,
                       color[0] * settings::g_world.m_scene.lighting_intensity);
  memory::Write<float>(object + 0xe8,
                       color[1] * settings::g_world.m_scene.lighting_intensity);
  memory::Write<float>(object + 0xec,
                       color[2] * settings::g_world.m_scene.lighting_intensity);
}

void Scene::OnLightSceneObjectPost(std::uintptr_t object) const {
  if (!object || !settings::g_world.m_scene.lighting.value) {
    return;
  }

  // auto rotation = settings::g_world.m_scene.lighting_rotation;
  // rotation.normalize( );

  // memory::write<math::vector3>( object + 0x184, rotation );
}

void Scene::OnDrawSceneObjectArray(std::uintptr_t object_array) const {
  if (!object_array || !settings::g_world.m_scene.world_setting.value) {
    return;
  }

  const auto object_data = memory::SafeRead<std::uintptr_t>(object_array + 0x8);
  if (!object_data || !*object_data) {
    return;
  }

  const auto light_data_queue =
      memory::SafeRead<std::uintptr_t>(addresses::globals::light_data_queue);
  if (!light_data_queue || !*light_data_queue) {
    return;
  }

  const auto light_data_base =
      memory::SafeRead<std::uintptr_t>(*light_data_queue + 0x18);
  if (!light_data_base || !*light_data_base) {
    return;
  }

  const auto count = memory::SafeRead<int>(*object_data + 0x4);
  // +0x30 is the source index-array pointer; +0x38 is the output record index.
  const auto index = memory::SafeRead<int>(*object_data + 0x38);
  if (!count || !index || *count <= 0 || *count > (1 << 20) || *index < 0 ||
      *index > (1 << 22)) {
    return;
  }

  const auto& configured = settings::g_world.m_scene.world_color.value;
  for (auto i = 0; i < *count; ++i) {
    const auto color_addr =
        *light_data_base + ((static_cast<std::size_t>(*index) + i) << 5);
    const auto current = memory::SafeRead<xdraw::color>(color_addr);
    if (!current) {
      continue;
    }

    (void)memory::SafeWrite<xdraw::color>(
        color_addr, {configured.r, configured.g, configured.b, current->a});
  }
}

void Scene::OnDrawSceneObject(std::uintptr_t batch, int batch_count) const {
  if (!batch || batch_count <= 0 || batch_count > (1 << 20)) {
    return;
  }

  const auto& config = settings::g_world.m_scene.skybox;
  if (!config.custom_color.value &&
      !settings::g_world.m_scene.world_setting.value) {
    return;
  }

  constexpr std::array kCloudMaterials{
      "materials/effects/clouds_001.vmat"_hash,
      "materials/de_vertigo/vertigo_clouds_001.vmat"_hash,
      "materials/models/props/de_nuke/hr_nuke/nuke_skydome_001/nuke_clouds_003.vmat"_hash,
      "materials/models/props/de_nuke/hr_nuke/nuke_skydome_001/nuke_clouds_002.vmat"_hash,
      "materials/models/props/de_nuke/hr_nuke/nuke_skydome_001/nuke_clouds_001.vmat"_hash};

  constexpr std::array kSunMaterials{
      "materials/sun/overlay.vmat"_hash,
      "materials/effects/glows/sun_glow_001.vmat"_hash,
      "materials/effects/glows/sun_disc_glow_001.vmat"_hash,
      "materials/effects/glows/sun_disc_glow_003.vmat"_hash,
      "materials/effects/glows/sun_disc_glow_004.vmat"_hash,
      "materials/de_train/hr_train_s2/effects/sun_disc_glow_01_clouded.vmat"_hash};

  for (auto i = 0; i < batch_count; ++i) {
    // Current scenesystem.dll mesh primitives are 0x70 bytes.
    const auto mesh = batch + (static_cast<std::size_t>(i) * 0x70);
    const auto material = memory::SafeRead<std::uintptr_t>(mesh + 0x20);

    if (!material || !*material) {
      continue;
    }

    const auto material_hash = SafeMaterialHash(*material);
    if (!material_hash) {
      continue;
    }

    const auto is_cloud = std::ranges::find(kCloudMaterials, material_hash) !=
                          std::end(kCloudMaterials);
    const auto is_sun = std::ranges::find(kSunMaterials, material_hash) !=
                        std::end(kSunMaterials);

    if ((is_cloud || is_sun) && config.custom_color.value) {
      const auto& color =
          is_cloud ? config.cloud_color.value : config.sun_color.value;
      (void)memory::SafeWrite<std::uint32_t>(mesh + 0x50, color);
    } else if (!is_cloud && !is_sun &&
               settings::g_world.m_scene.world_setting.value) {
      (void)memory::SafeWrite<std::uint32_t>(
          mesh + 0x50, settings::g_world.m_scene.world_color.value);
    }
  }
}

bool Scene::OnSetupFog(__m128i* output, int* mode) const {
  const auto& fog = settings::g_world.m_weather;
  if (!fog.fog_enabled.value || !output || !mode) {
    return false;
  }

  const auto set_param_f = PATTERN(patterns::set_shader_param);
  const auto set_param_i = PATTERN(patterns::set_shader_param_i);
  if (!set_param_f || !set_param_i) {
    return false;
  }

  const auto falloff = 0.5f + fog.fog_anisotropy.value * 8.0f;
  const auto& color = fog.fog_color.value;
  const __m128 params =
      _mm_set_ps(0.0f, 0.0f, fog.fog_draw_distance.value, 0.0f);
  const __m128 params_2 =
      _mm_set_ps(0.0f, 0.0f, falloff, fog.fog_density.value);
  const __m128 params_3 = _mm_set_ps(0.0f, static_cast<float>(color.b) / 255.0f,
                                     static_cast<float>(color.g) / 255.0f,
                                     static_cast<float>(color.r) / 255.0f);

  memory::Call<std::uintptr_t>(set_param_f, output, shader_hash::kGradientFog,
                               &params);
  memory::Call<std::uintptr_t>(set_param_f, output, shader_hash::kGradientFog2,
                               &params_2);
  memory::Call<std::uintptr_t>(set_param_f, output, shader_hash::kGradientFog3,
                               &params_3);
  memory::Call<std::uintptr_t>(set_param_i, output + 17,
                               shader_hash::kEnableGradientFog, 1);
  *mode = 0;
  return true;
}

void Scene::OnSetShaderParam(__m128i*& value, std::uint32_t hash) const {
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
    dof_val = _mm_set_ps(settings::g_world.m_scene.dof_far_blurry,
                         settings::g_world.m_scene.dof_far_crisp,
                         settings::g_world.m_scene.dof_near_crisp,
                         settings::g_world.m_scene.dof_near_blurry);
    value = reinterpret_cast<__m128i*>(&dof_val);
  }

  const auto& weather = settings::g_world.m_weather;
  if (weather.wind.value && hash == shader_hash::kWindDirection) {
    const auto direction =
        weather.wind_direction.value * (std::numbers::pi_v<float> / 180.0f);
    wind_direction_val =
        _mm_set_ps(0.0f, 0.0f, std::sin(direction), std::cos(direction));
    value = reinterpret_cast<__m128i*>(&wind_direction_val);
  } else if (weather.wind.value &&
             hash == shader_hash::kWindStrengthFrequency) {
    // Foliage shaders pack the low/high sway strength and frequency into one
    // vector.
    wind_strength_frequency_val =
        _mm_set_ps(weather.wind_turbulence.value, weather.wind_strength.value,
                   weather.wind_turbulence.value, weather.wind_strength.value);
    value = reinterpret_cast<__m128i*>(&wind_strength_frequency_val);
  }

  if (settings::g_world.m_scene.bloom.value) {
    const auto t = settings::g_world.m_scene.bloom_value;
    if (hash == 0x565EAF76) {
      bloom_scale_val = _mm_set_ps1(0.3f + t * 1.2f);
      value = (__m128i*)&bloom_scale_val;
    } else if (hash == 0xBA98A9B0) {
      bloom_threshold_val = _mm_set_ps1(1.5f - t * 1.2f);
      value = (__m128i*)&bloom_threshold_val;
    } else if (hash == 0x2AE72B37) {
      bloom_width_val = _mm_set_ps1(0.5f + t * 1.5f);
      value = (__m128i*)&bloom_width_val;
    } else if (hash == 0xB692902E) {
      bloom_strength_val = _mm_set_ps1(0.2f + t * 0.6f);
      value = (__m128i*)&bloom_strength_val;
    } else if (hash == 0x1313A424) {
      bloom_skybox_val = _mm_set_ps1(0.1f + t * 0.4f);
      value = (__m128i*)&bloom_skybox_val;
    }
  }

  if (settings::g_world.m_scene.gamma.value && hash == 0x24470A87) {
    gamma_val = _mm_set_ps1(settings::g_world.m_scene.gamma_value);
    value = (__m128i*)&gamma_val;
  }

  const auto& wetness = settings::g_world.m_weather;
  if (wetness.wetness.value && hash == shader_hash::kRainExposureToSky) {
    wetness_sky_val = _mm_set_ps1(1.0f);
    value = (__m128i*)&wetness_sky_val;
  } else if (wetness.wetness.value && hash == shader_hash::kRainWetness) {
    wetness_density_val = _mm_set_ps1(wetness.wetness_density.value);
    value = (__m128i*)&wetness_density_val;
  } else if (wetness.wetness.value && hash == shader_hash::kRainTimer) {
    const auto seconds = static_cast<float>(GetTickCount64()) * 0.001f;
    wetness_timer_val = _mm_set_ps1(seconds * wetness.wetness_speed.value);
    value = (__m128i*)&wetness_timer_val;
  }
}

void Scene::LoadSkyboxMaterial(const char* path) {
  this->m_custom_sky_material_ = 0;
  if (!path || !*path) {
    return;
  }

  if (const auto cached = this->m_skybox_materials_.find(path);
      cached != this->m_skybox_materials_.end()) {
    this->m_custom_sky_material_ = cached->second.material;
    return;
  }

  if (!IsSafeSkyTexturePath(path)) {
    logging::console::Print(xs("invalid skybox texture path."));
    return;
  }

  struct BufferString {
    std::uint32_t m_unknown1{};
    std::uint32_t m_unknown2{0xc00000c8};

    union {
      std::uintptr_t m_str_ptr;
      std::uint8_t data[0xc8];
    };

    std::uintptr_t m_unknown3{};
    std::uintptr_t m_unknown4{};
  } buffer;

  const auto init_path_buffer = PATTERN(patterns::init_particle_path_buffer);
  const auto precache_resource = PATTERN(patterns::resource_system_precache);
  if (!init_path_buffer || !precache_resource ||
      !addresses::globals::resource_system) {
    return;
  }

  memory::Call<void>(init_path_buffer, &buffer, path);
  buffer.m_unknown4 = 'xetv';

  memory::Call<void>(precache_resource, addresses::globals::resource_system,
                     &buffer, "");

  memory::Call<void>(init_path_buffer, &buffer, path);
  buffer.m_unknown4 = 'xetv';

  const auto binding = memory::CallVfunc<std::uintptr_t>(
      addresses::globals::resource_system, 79, &buffer, 0ll);
  const auto texture =
      binding ? memory::SafeRead<std::uintptr_t>(binding) : std::nullopt;
  if (!texture || !*texture) {
    logging::console::Print(xs("failed to preload skybox texture."));
    return;
  }

  const auto material_source = std::format(
      R"VMAT(<!-- kv3 encoding:text:version{{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d}} format:generic:version{{7412167c-06e9-4698-aff2-e63eb59037e7}} -->
{{
	Shader = "sky.vfx"
	g_flBrightnessExposureBias = 0.0
	g_flRenderOnlyExposureBias = 0.0
	SkyTexture = resource:"{}"
	g_tSkyTexture = resource:"{}"
}}
)VMAT",
      path, path);
  const auto material_name =
      std::format("velocity_skybox_{:08x}", fnv1a::RuntimeHash(path));
  const auto material =
      systems::Materials::Load(material_source.c_str(), material_name.c_str());
  if (!material) {
    logging::console::Print(xs("failed to create skybox material."));
    return;
  }

  this->m_skybox_materials_.emplace(path,
                                    CachedSkyboxMaterial{binding, material});
  this->m_custom_sky_material_ = material;
}

}  // namespace features::world
