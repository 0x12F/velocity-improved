module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>
#include <external/xdraw/xdraw.hpp>

export module velocity:core.features.esp.esp;

import <array>;
import <atomic>;
import <chrono>;
import <cstddef>;
import <cstdint>;
import <filesystem>;
import <string>;
import <string_view>;
import <unordered_map>;
import <vector>;

export import :core.settings;
export import :core.systems.systems;
export import :utilities.animation;
export import :utilities.math.math;
export import :utilities.fnv1a;

export {
namespace features::esp {

namespace player {

class Chams {
 public:
  bool OnGeneratePrimitives(
      std::uintptr_t owner_entity, std::uint32_t owner_hash,
      std::uintptr_t scene_object, std::uintptr_t primitive_buffer,
      void(__fastcall* original_fn)(std::uintptr_t, std::uintptr_t,
                                    std::uintptr_t, std::uintptr_t),
      std::uintptr_t a1, std::uintptr_t scene_view);
  void OnSortPrimitives(std::uintptr_t entries, std::uint32_t count);

  class Backtrack {
   public:
    void Update();
    void Shutdown();

    [[nodiscard]] bool IsActive(std::uintptr_t scene_object) const;
    [[nodiscard]] bool HasActive(std::uintptr_t pawn) const;
    [[nodiscard]] std::uintptr_t GetSceneObject(std::uintptr_t pawn) const;

    struct SceneObject {
      std::uintptr_t scene_object{};
      std::uintptr_t pawn{};
      bool active{};
      float spawn_time{};

      void Create(std::uintptr_t pawn);
      void Destroy();
      void SetupBones(systems::Bones::Data* bones, int count) const;
    };

   private:
    std::unordered_map<std::uintptr_t, SceneObject> m_objects_{};
  };

  class Onshot {
   public:
    void Push(std::uintptr_t pawn);
    void Update();
    void Shutdown();

    [[nodiscard]] bool HasActive(std::uintptr_t pawn) const;
    [[nodiscard]] bool IsActive(std::uintptr_t scene_object) const;
    [[nodiscard]] std::uintptr_t GetSceneObject(std::uintptr_t pawn) const;
    [[nodiscard]] float GetAlpha(std::uintptr_t pawn) const;

   private:
    struct PendingEntry {
      std::array<systems::Bones::Data, 27> bones{};
      int bone_count{};
    };

    // Scene objects must be created from the frame-stage callback, not
    // CreateMove.
    std::unordered_map<std::uintptr_t, PendingEntry> m_pending_{};
    std::unordered_map<std::uintptr_t, Backtrack::SceneObject> m_entries_{};
  };

  [[nodiscard]] Onshot& Os() { return this->m_onshot_; }
  [[nodiscard]] Backtrack& Bt() { return this->m_backtrack_; }

 private:
  void ApplyLayer(std::uintptr_t primitive_buffer,
                  void(__fastcall* original_fn)(std::uintptr_t, std::uintptr_t,
                                                std::uintptr_t, std::uintptr_t),
                  std::uintptr_t a1, std::uintptr_t scene_object,
                  std::uintptr_t scene_view, const xdraw::color& color,
                  settings::Esp::ChamIds material_id);
  void ApplyOverlay(
      std::uintptr_t primitive_buffer,
      void(__fastcall* original_fn)(std::uintptr_t, std::uintptr_t,
                                    std::uintptr_t, std::uintptr_t),
      std::uintptr_t a1, std::uintptr_t scene_object, std::uintptr_t scene_view,
      const xdraw::color& color, settings::Esp::ChamIds material_id);
  void ApplyClone(std::uintptr_t primitive_buffer,
                  void(__fastcall* original_fn)(std::uintptr_t, std::uintptr_t,
                                                std::uintptr_t, std::uintptr_t),
                  std::uintptr_t a1, std::uintptr_t scene_object,
                  std::uintptr_t scene_view,
                  systems::Materials::CloneType type);

  bool IsOverlayMaterial(std::uintptr_t mat) const;
  void AddOverlayMaterial(std::uintptr_t mat);

  Backtrack m_backtrack_{};
  Onshot m_onshot_{};

  static constexpr auto kMaxOverlayMaterials{16};
  std::array<std::atomic<std::uintptr_t>, kMaxOverlayMaterials>
      m_overlay_materials_{};
  std::atomic<int> m_overlay_material_count_{0};
};

class Glow {
 public:
  bool OnIsGlowing(std::uintptr_t owner_entity, std::uint32_t owner_hash) const;
  bool OnGetGlowColor(std::uintptr_t owner_entity, std::uint32_t owner_hash,
                      float* color) const;
};

class Overlay {
 public:
  void OnRender(xdraw::draw_list& draw_list);

 private:
  struct DrawOffsets {
    float left{};
    float top{};
    float bottom{};
  };

  struct Info {
    std::uintptr_t controller{};
    std::uintptr_t pawn{};
    int health{};
    int team{};
    int money{};
    int armor{};
    int ping{};
    float distance{};
    bool is_other_team{};
    bool is_visible{};
    bool has_helmet{};
    bool has_defuser{};
    bool is_scoped{};
    bool is_defusing{};
    bool is_flashed{};
    std::string name{};
    std::array<systems::Bones::Data, 27> bones{};
    math::Vector3 origin{};

    struct WeaponInfo {
      std::uintptr_t ptr{};
      std::uintptr_t vdata{};
      int ammo{};
      int max_ammo{};
      std::string name{};

      [[nodiscard]] bool Valid() const { return this->ptr && this->vdata; }
    };

    WeaponInfo weapon{};

    [[nodiscard]] bool Valid() const {
      return this->controller && this->pawn && this->health > 0;
    }
  };

  void AddBox(xdraw::draw_list& draw_list, const systems::Bounds::Data& bounds,
              const settings::Esp::Player::Overlay::Box& cfg, bool visible);
  void AddSkeleton(xdraw::draw_list& draw_list, const Info& info,
                   const settings::Esp::Player::Overlay::Skeleton& cfg,
                   bool visible, const systems::Local::Snapshot& local);
  void AddHealthBar(xdraw::draw_list& draw_list,
                    const systems::Bounds::Data& bounds, const Info& info,
                    const settings::Esp::Player::Overlay::HealthBar& cfg,
                    DrawOffsets& offsets);
  void AddAmmoBar(xdraw::draw_list& draw_list,
                  const systems::Bounds::Data& bounds, const Info& info,
                  const settings::Esp::Player::Overlay::AmmoBar& cfg,
                  DrawOffsets& offsets);
  void AddName(xdraw::draw_list& draw_list, const systems::Bounds::Data& bounds,
               const Info& info,
               const settings::Esp::Player::Overlay::Name& cfg,
               DrawOffsets& offsets);
  void AddWeapon(xdraw::draw_list& draw_list,
                 const systems::Bounds::Data& bounds, const Info& info,
                 const settings::Esp::Player::Overlay::Weapon& cfg,
                 DrawOffsets& offsets);
  void AddFlags(xdraw::draw_list& draw_list,
                const systems::Bounds::Data& bounds, const Info& info,
                const settings::Esp::Player::Overlay::InfoFlags& cfg,
                DrawOffsets& offsets);
  void AddOofArrow(xdraw::draw_list& draw_list, const Info& info,
                   const settings::Esp::Player::Overlay::OofArrow& cfg);
  [[nodiscard]] Info GetInfo(const systems::Entities::Cached& player,
                             const systems::Local::Snapshot& local);

  struct AnimationData {
    animation::Spring health{};
    animation::Spring ammo{};
    bool initialized{false};
  };

  std::unordered_map<std::uintptr_t, AnimationData> m_animations_{};
};

}  // namespace player

namespace item {

class Chams {
 public:
  bool OnGeneratePrimitives(
      std::uintptr_t owner_entity, std::uint32_t owner_hash,
      std::uintptr_t scene_object, std::uintptr_t primitive_buffer,
      void(__fastcall* original_fn)(std::uintptr_t, std::uintptr_t,
                                    std::uintptr_t, std::uintptr_t),
      std::uintptr_t a1, std::uintptr_t scene_view);

 private:
  void ApplyLayer(std::uintptr_t primitive_buffer,
                  void(__fastcall* original_fn)(std::uintptr_t, std::uintptr_t,
                                                std::uintptr_t, std::uintptr_t),
                  std::uintptr_t a1, std::uintptr_t scene_object,
                  std::uintptr_t scene_view, const xdraw::color& color,
                  settings::Esp::ChamIds material_id);
  [[nodiscard]] std::uint32_t GetItemGroup(std::uint32_t schema_hash);
};

class Glow {
 public:
  bool OnIsGlowing(std::uintptr_t owner_entity, std::uint32_t owner_hash) const;
  bool OnGetGlowColor(std::uintptr_t owner_entity, std::uint32_t owner_hash,
                      float* color) const;

 private:
  [[nodiscard]] std::uint32_t GetItemGroup(std::uint32_t schema_hash) const;
};

class Overlay {
 public:
  void OnRender(xdraw::draw_list& draw_list);

 private:
  struct Info {
    std::uintptr_t entity{};
    std::uintptr_t vdata{};
    std::uint32_t schema_hash{};
    std::string name{};
    math::Vector3 origin{};
    float distance{};

    [[nodiscard]] bool Valid() const {
      return this->entity && !this->name.empty();
    }
  };

  void AddLabel(xdraw::draw_list& draw_list, const Info& info,
                const settings::Esp::Item::Overlay::Group& cfg);

  [[nodiscard]] Info GetInfo(const systems::Entities::Cached& entity);
  [[nodiscard]] std::uint32_t GetItemGroup(std::uint32_t schema_hash);
};

}  // namespace item

namespace projectile {

class Overlay {
 public:
  void OnRender(xdraw::draw_list& draw_list,
                xdraw::draw_list& middle_draw_list);

 private:
  struct Info {
    std::uintptr_t entity{};
    std::uint32_t schema_hash{};
    std::uint32_t group_id{};
    math::Vector3 origin{};
    float distance{};
    bool detonated{};
    bool smoke_active{};
    int effect_tick_begin{};

    [[nodiscard]] bool Valid() const {
      return this->entity && group_id != UINT32_MAX;
    }
  };

  struct InfernoState {
    std::array<float, 64 * 15> current_radii{};
    std::vector<math::Vector3> last_world_points{};
    float fade_alpha{};
    bool was_active{};
    std::chrono::steady_clock::time_point spawn_time{};
    math::Vector3 last_avg_pos{};
  };

  struct IndicatorState {
    float fade_alpha{};
    bool was_active{};
  };

  void AddLandingIndicators(xdraw::draw_list& draw_list);
  void AddIndicator(
      xdraw::draw_list& draw_list, float cx, float cy, float dir_angle,
      bool has_arrow, float timer_frac, float alpha, bool is_fire,
      const settings::Esp::Projectile::Overlay::Indicator::Group& cfg);

  void AddInferno(xdraw::draw_list& draw_list,
                  xdraw::draw_list& middle_draw_list,
                  const systems::Entities::Cached& entity,
                  const settings::Esp::Projectile::Overlay::Infernos& cfg);
  void AddLabel(xdraw::draw_list& draw_list, const math::Vector2& screen,
                const Info& info,
                const settings::Esp::Projectile::Overlay::Group& cfg);

  [[nodiscard]] Info GetInfo(const systems::Entities::Cached& entity);
  [[nodiscard]] std::uint32_t GetProjectileGroup(std::uint32_t schema_hash);

  std::unordered_map<std::uintptr_t, InfernoState> m_inferno_states_{};
  std::unordered_map<std::uintptr_t, IndicatorState> m_indicator_states_{};
};

class Tracers {
 public:
};

}  // namespace projectile

namespace other {

class Overlay {
 public:
  void OnRender(xdraw::draw_list& draw_list);

 private:
  void AddBomb(xdraw::draw_list& draw_list);
  void AddSpectators(xdraw::draw_list& draw_list);
};

}  // namespace other

}  // namespace features::esp
}  // export
