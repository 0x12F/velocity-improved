module;

#include "platform/platform.h"

export module velocity:core.features.world.world;

import <array>;
import <cstddef>;
import <cstdint>;
import <string>;
import <unordered_map>;
import <vector>;

export {
namespace features::world {

class Weather {
 public:
  void OnFrameStageNotify();
  void Release();

 private:
  static constexpr std::uint32_t kInvalidEffectIndex{
      static_cast<std::uint32_t>(-1)};

  void CreateParticle();
  void UpdateParticles();
  void ReleaseParticles();

  std::uint32_t m_effect_index_{kInvalidEffectIndex};
  int m_last_particle_type_{-1};
  float m_last_round_start_time_{};
  bool m_particle_loaded_{};
};

class Scene {
 public:
  struct SkyboxEntry {
    std::string display_name;
    std::string resource_path;
  };

  void DiscoverSkyboxes();
  void ResetSkyboxState();

  void OnFrameStageNotify();
  void OnDrawSkyboxArrayPre(std::uintptr_t mesh_array, int mesh_count);
  void OnDrawSkyboxArrayPost();
  void OnLightSceneObjectPre(std::uintptr_t object) const;
  void OnLightSceneObjectPost(std::uintptr_t object) const;
  void OnDrawSceneObjectArray(std::uintptr_t object_array) const;
  void OnDrawSceneObject(std::uintptr_t batch, int batch_count) const;
  [[nodiscard]] bool OnSetupFog(__m128i* output, int* mode) const;
  void OnSetShaderParam(__m128i*& value, std::uint32_t hash) const;

  [[nodiscard]] const std::vector<SkyboxEntry>& GetSkyboxes() const {
    return this->m_skyboxes_;
  }

 private:
  void LoadSkyboxMaterial(const char* path);

  std::vector<SkyboxEntry> m_skyboxes_{};
  struct CachedSkyboxMaterial {
    std::uintptr_t texture_binding{};
    std::uintptr_t material{};
  };

  std::unordered_map<std::string, CachedSkyboxMaterial> m_skybox_materials_{};
  std::uintptr_t m_custom_sky_material_{};
  int m_loaded_skybox_index_{-1};

  std::uintptr_t m_active_material_binding_{};
  std::uintptr_t m_active_original_material_{};
  std::uintptr_t m_active_skybox_descriptor_{};
  std::array<float, 3> m_active_original_sky_color_{};
  bool m_active_sky_tinted_{};
};

class Smoke {
 public:
  void OnRenderSmokePre() { this->m_active_ = true; }
  void OnRenderSmokePost() { this->m_active_ = false; }

  void OnMap(std::uintptr_t token, std::size_t size, std::uintptr_t buf_ptr);
  void OnUnmap(std::uintptr_t token);

 private:
  static inline bool m_active_{};
  static inline std::uintptr_t m_buf_{};
  static inline std::uintptr_t m_token_{};
};

}  // namespace features::world
}  // export
