module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>
#include <external/xdraw/xdraw.hpp>

export module velocity:core.features.misc.misc;

import <array>;
import <atomic>;
import <chrono>;
import <cstddef>;
import <cstdint>;
import <filesystem>;
import <limits>;
import <mutex>;
import <span>;
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
namespace features::misc {

class ProjectileTrajectory {
 public:
  struct Trajectory {
    std::vector<math::Vector3> points{};
    std::vector<math::Vector3> bounces{};
    math::Vector3 end_pos{};
    float duration{};
    int end_tick{-1};
    bool valid{};
  };

  struct InFlightGrenade {
    std::uintptr_t entity{};
    std::uintptr_t weapon_hash{};
    std::uint32_t thrower_handle{};
    bool is_enemy{};
    Trajectory traj{};
    std::chrono::steady_clock::time_point throw_time{};
    std::chrono::steady_clock::time_point last_seen{};
    std::chrono::steady_clock::time_point detonate_time{};
    bool corrected{};
    bool detonated{};
  };

  void OnRender(xdraw::draw_list& draw_list);
  void OnCreateMove(systems::Input::Usercmd* cmd);

  [[nodiscard]] bool ShouldStop() const { return this->m_needs_air_stop_; }
  [[nodiscard]] const std::vector<InFlightGrenade>& InFlight() const {
    return this->m_in_flight_;
  }

  /// Skip collision with thrower only for opening sim ticks so the nade clears
  /// the hand/body.
  static constexpr int kThrowerCollisionSkipTicks{18};

 private:
  static constexpr auto kGravityScale{0.4f};
  static constexpr auto kElasticity{0.45f};
  static constexpr auto kHullSize{2.0f};
  static constexpr auto kForwardOffset{22.0f};
  static constexpr auto kPullBack{6.0f};
  static constexpr auto kVelocityInherit{1.25f};
  static constexpr auto kStopSpeedSq{400.0f};
  static constexpr auto kSteepBounceSpeedSq{96000.0f};
  static constexpr auto kSteepBounceNormalZ{0.7f};
  static constexpr auto kMaxTicks{4096};
  static constexpr auto kTicksPerPoint{2};
  static constexpr auto kMaxBounces{20};
  static constexpr auto kMaxDelayTicks{32};
  static constexpr auto kThrowCooldown{0.75f};
  static constexpr auto kMissingGrace{0.1f};
  static constexpr auto kFadeDuration{0.75f};
  static constexpr auto kPlayerHitFractionThreshold{0.7f};
  static constexpr auto kPlayerDampenDotThreshold{0.5f};

  struct DamageInfo {
    int damage{};
    bool is_lethal{};
    math::Vector3 position{};
  };

  void SetupThrow(std::uintptr_t local_pawn, std::uintptr_t weapon,
                  math::Vector3& origin, math::Vector3& velocity);
  void Simulate(const math::Vector3& start, const math::Vector3& velocity,
                std::uintptr_t thrower_pawn, Trajectory& out);
  void StepSimulation(math::Vector3& pos, math::Vector3& vel,
                      std::uintptr_t thrower_pawn, int sim_tick, bool& hit,
                      bool& detonated);
  void ResolveCollision(const systems::Tracing::Result& trace,
                        math::Vector3& pos, math::Vector3& vel,
                        std::uintptr_t thrower_pawn, int sim_tick,
                        bool& detonated) const;
  void SetSimulationParams(std::uintptr_t weapon_hash);
  [[nodiscard]] bool ShouldDetonate(const math::Vector3& vel, int tick) const;
  [[nodiscard]] static math::Vector3 ClipVelocity(const math::Vector3& velocity,
                                                  const math::Vector3& normal,
                                                  float overbounce);

  void UpdateWeaponProperties(std::uintptr_t weapon,
                              std::uintptr_t weapon_vdata);
  void UpdateInFlight(const systems::Local::Snapshot& local);
  [[nodiscard]] std::uintptr_t GetOtherName(std::uint32_t schema_hash);

  void CorrectThrowAngles(systems::Input::Usercmd* cmd,
                          const systems::Local::Snapshot& local,
                          std::uintptr_t weapon);
  void ComputeDesiredDirection(math::Vector3& desired_forward) const;

  void ComputeDamageAtEndpoint(const Trajectory& traj,
                               std::vector<DamageInfo>& damages,
                               const systems::Local::Snapshot& local) const;

  void RenderPreview(xdraw::draw_list& draw_list,
                     const systems::Local::Snapshot& local);
  void RenderTrajectory(xdraw::draw_list& draw_list, const Trajectory& traj,
                        float alpha, bool is_thrown,
                        const systems::Local::Snapshot& local) const;

  std::uintptr_t m_weapon_vdata_{};
  std::uintptr_t m_weapon_hash_{};
  float m_throw_velocity_{};
  float m_detonate_time_{1.5f};
  float m_velocity_threshold_{0.1f};

  float m_sv_gravity_{};
  float m_molotov_max_slope_z_{};

  std::vector<InFlightGrenade> m_in_flight_{};

  std::chrono::steady_clock::time_point m_last_throw_time_{};
  bool m_was_holding_{};
  bool m_should_preview_{};

  bool m_was_forward_only_{};
  bool m_delay_release_{};
  bool m_delayed_attack2_{};
  bool m_needs_air_stop_{};
  bool m_throw_stopping_{};
  float m_delayed_strength_{1.0f};
  int m_delay_ticks_{};
};

class Impacts {
 public:
  void OnRenderEarly(xdraw::draw_list& draw_list);
  void OnFrameStageNotify();
  void OnLevelChange();
  void OnRender(xdraw::draw_list& draw_list);
  void OnReportHit(std::uintptr_t msg);
  void OnPlayerHurt(std::uintptr_t event);
  void OnBulletImpact(std::uintptr_t event);
  void OnBaseFireGunsGetInaccuracy(std::uintptr_t weapon, float inaccuracy);
  void OnGetInterpolatedShootPosition(std::uintptr_t weapon_services,
                                      float* out);
  void OnBoom(std::uintptr_t victim_pawn, int hitgroup, float damage,
              float hitchance, float inaccuracy, float spread,
              const math::Vector3& aim_angle,
              const math::Vector3& shoot_position, int tick,
              const std::array<systems::Bones::Data, 27>& skeleton,
              bool forced);

  [[nodiscard]] static std::vector<std::string> ListCustomSounds();
  [[nodiscard]] static std::string CustomSoundsDirectoryNarrow();
  void PlayCustomSound(std::string_view filename, float volume) const;

 private:
  struct ShotRecord {
    std::uintptr_t victim_pawn{};
    int hitgroup{};
    float damage{};
    float hitchance{};
    float predicted_inaccuracy{};
    float predicted_spread{};
    float server_inaccuracy{};
    math::Vector3 aim_angle{};
    math::Vector3 shoot_position{};
    math::Vector3 impact_position{};
    float best_impact_dist_sq{FLT_MAX};
    int tick{};
    float time{};
    float impact_time{};
    std::array<systems::Bones::Data, 27> skeleton{};
    bool resolved{};
    bool server_confirmed{};
    bool impact_confirmed{};
    math::Vector3 server_shoot_position{};
    bool server_shoot_position_confirmed{};
    math::Vector3 target_velocity{};
    bool forced{};
    std::uint32_t weapon_type{};
  };

  struct HitData {
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

  struct Hitmarker {
    math::Vector3 position{};
    float time{};
    int damage{};
  };

  struct Log {
    std::string name{};
    std::string hitgroup{};
    std::string reason{};
    int damage{};
    int health{};
    float time{};
    float duration{};
    animation::Spring offset{};
    animation::Fade alpha{};
    bool snapped{};
    bool is_miss{};
    std::uint32_t weapon_type{};
  };

  struct PendingHit {
    math::Vector3 position{};
    float time{};
  };

  struct BulletImpact {
    math::Vector3 position{};
    float time{};
  };

  [[nodiscard]] const char* ClassifyShotDeviation(const ShotRecord& shot) const;
  [[nodiscard]] HitData ParseEvent(std::uintptr_t event);
  [[nodiscard]] std::string GetPlayerName(std::uintptr_t controller);
  [[nodiscard]] std::string GetPlayerNameFromPawn(std::uintptr_t pawn);
  [[nodiscard]] float DistanceToNearestHitbox(const ShotRecord& shot) const;
  [[nodiscard]] float RayDistanceToNearestHitbox(
      const ShotRecord& shot, const math::Vector3& direction) const;

  void AddHitLog(const HitData& data);
  void AddMissLog(const ShotRecord& shot, const char* reason);
  void CheckMisses();

  void RenderHitMarkers(xdraw::draw_list& draw_list, float time);
  void RenderLogs(xdraw::draw_list& draw_list, float time);
  void RenderHitEffect(xdraw::draw_list& draw_list, float time);
  void RenderBulletImpactOverlays(xdraw::draw_list& draw_list, float time);

  void PlaySound(settings::Misc::Impacts::SoundType type, float volume,
                 std::string_view custom_file = {});
  void PlayHitEffect(std::uintptr_t victim_pawn);
  void PlayDeathEffect(std::uintptr_t victim_pawn);
  void PlayBulletImpactEffect(const math::Vector3& position);
  void PlayBulletTracer(const math::Vector3& position);
  void FlushBufferedImpacts();

  std::vector<Hitmarker> m_hitmarkers_{};
  std::vector<Log> m_logs_{};
  std::vector<PendingHit> m_pending_hits_{};
  std::vector<ShotRecord> m_pending_shots_{};
  std::vector<BulletImpact> m_bullet_impacts_{};
  mutable std::mutex m_mtx_{};

  bool m_death_effect_loaded_{};
  bool m_bullet_impact_effect_loaded_{};
  bool m_bullet_tracers_loaded_{};

  float m_hit_effect_time_{};

  std::vector<math::Vector3> m_buffered_impacts_{};
  math::Vector3 m_buffered_eye_position_{};
  float m_buffered_impact_time_{-1.0f};
};

class Removals {
 public:
  void OnPrepareSceneMaterial(std::uintptr_t material) const;
  void OnOverrideView(std::uintptr_t view_setup) const;
};

class Camera {
 public:
  void OnOverrideView(std::uintptr_t view_setup);
  void UpdateFovSensitivity(std::uintptr_t player_pawn) const;

 private:
  void DoThirdperson(std::uintptr_t view_setup,
                     std::uintptr_t local_pawn) const;
  void DoFovChange(std::uintptr_t view_setup, std::uintptr_t local_pawn) const;
  void DoAspectRatioChange(std::uintptr_t view_setup);

  mutable float m_cached_fov_sensitivity_{-1.0f};
  mutable bool m_cached_scoped_{};
  mutable float m_cached_target_fov_{};
};

class Hud {
 public:
  void OnRender(xdraw::draw_list& draw_list);

 private:
  void DoCrosshair(xdraw::draw_list& draw_list, float cx, float cy) const;
  void DoScope(xdraw::draw_list& draw_list, float cx, float cy, float screen_h,
               std::uintptr_t local_pawn);
  void DoHat(xdraw::draw_list& draw_list, std::uintptr_t local_pawn) const;
  void DoVelocity(xdraw::draw_list& draw_list, float cx, float screen_h,
                  std::uintptr_t local_pawn);

  static constexpr std::size_t kVelocityHistory{120};

  float m_scope_anim_{};
  float m_spread_smooth_{};
  std::uint32_t m_last_weapon_{};
  float m_cached_spread_pixels_{};
  int m_scope_update_frame_{};
  std::array<float, kVelocityHistory> m_velocity_history_{};
  std::size_t m_velocity_history_count_{};
  std::size_t m_velocity_history_head_{};
  float m_velocity_smoothed_{};
  float m_velocity_scale_{};
};

class Dlight {
 public:
  void OnPresent();
  void OnFrameStageNotify();
  void OnLevelShutdown();
  void ApplySceneColor(std::uintptr_t object) const;

 private:
  struct ConfigSnapshot {
    bool enabled{};
    std::uint32_t packed_color{};
    float radius{300.0f};
    float z_offset{2.0f};
  };

  void RetireEntry();

  std::mutex m_mutex_{};
  ConfigSnapshot m_config_{};
  std::atomic<std::uintptr_t> m_scene_object_{};
  std::atomic<std::uint32_t> m_packed_color_{};
  std::atomic<float> m_scene_color_scale_{};
  std::uintptr_t m_manager_{};
  std::uintptr_t m_entry_{};
  bool m_logged_submission_{};
  bool m_logged_scene_{};
};

class Other {
 public:
  void OnRoundStart();
  void OnFrameStageNotify();
  void DoKillFeedPreservation();
  void OnPlayerDeath(std::uintptr_t event);

  [[nodiscard]] bool IsAlphaChanged() const {
    return this->m_is_alpha_changed_;
  }

  static inline std::string s_display_name_{};
  static inline bool s_name_change_pending_{};

 private:
  void DoAutobuy() const;
  void DoPlayerAlphaChanging();
  void DoRevealRadar() const;
  void DoNameChanging();
  void DoViewmodelAdjust();
  bool m_is_alpha_changed_{};
  bool m_name_changer_active_{};
  std::uintptr_t m_name_changer_controller_{};
  std::string m_original_name_{};
  std::string m_last_sent_name_{};
  float m_last_spawntime_{};
  float m_cached_vm_x_{std::numeric_limits<float>::quiet_NaN()};
  float m_cached_vm_y_{std::numeric_limits<float>::quiet_NaN()};
  float m_cached_vm_z_{std::numeric_limits<float>::quiet_NaN()};
  float m_cached_vm_fov_{std::numeric_limits<float>::quiet_NaN()};
};

// this is so ghetto but fuck it for now it works

struct CPanel2d {};

struct CTopLevelWindowSource2 {};

class CUiPanel {
 public:
  void* m_vtable;                        // 0x0000
  CPanel2d* m_panel;                     // 0x0008
  char* m_panel_name;                    // 0x0010
  CUiPanel* m_parent_panel;              // 0x0018
  CTopLevelWindowSource2* m_ui_window;   // 0x0020
  int32_t m_child_ui_panel_count;        // 0x0028
  std::uint8_t pad_0000[0x4];            // 0x002C
  CUiPanel** m_children_ui_panel_array;  // 0x0030
  std::uint8_t pad_0001[0x18];           // 0x0038
  float m_width;                         // 0x0050
  float m_height;                        // 0x0054
  std::uint8_t pad_0002[0x270];          // 0x0058
};

// class c_ui_engine {
// public:
//	void run_script (c_ui_panel* panel, const char* script) {
//		const auto vmt = *reinterpret_cast<std::uintptr_t*>(this);
//		const auto fn = *reinterpret_cast<std::uintptr_t*>(vmt + 77 *
// sizeof (std::uintptr_t));

//		using fn_t = void (__thiscall*)(c_ui_engine*, c_ui_panel*, const
// char*, const char*, std::uint64_t); 		reinterpret_cast<fn_t>(fn)(this,
// panel, script, nullptr, 0);
//	}
//};

struct PanelDataT {
  std::uint8_t pad_0000[8];
  uint32_t m_flags;
  uint32_t m_visible;
  CUiPanel* m_panel;
  uint32_t m_panel_id;
  std::uint8_t pad_0001[4];
};

class CUiEngine {
 public:
  void* m_vtable;                // 0x0000
  std::uint8_t pad_0000[0x220];  // 0x002C
  PanelDataT* m_panels_array;    // 0x0228
  int32_t m_panel_count;         // 0x0230
  std::uint8_t pad_0001[0x3CC];  // 0x002C
  void* m_isolate;               // 0x0600
  std::uint8_t pad_0002[0x2E0];  // 0x002C

  void* m_panel_stack[63];      // 0x08E8
  int32_t m_panel_stack_depth;  // 0x0AE0
  std::uint8_t pad_0003[0x8C];  // 0x002C
  void* m_script_cache;         // 0x0B70
  std::uint8_t pad_0004[0x10];  // 0x002C

  void RunScript(CUiPanel* panel, const char* script);
};

class CPanoramaUiEngine {
 private:
  std::uint8_t pad_0001_[40];  // 0x002C
  CUiEngine* m_ui_engine_;     // 0x0028
 public:
  CUiEngine* GetUiEngine();
};

class ScoreboardWeapons {
 public:
  void OnFrameStageNotify();
  void OnLevelChange();

 private:
  struct WeaponEntry {
    std::string name{};
    int type{};

    bool operator==(const WeaponEntry& o) const {
      return name == o.name && type == o.type;
    }
  };

  struct PlayerWeaponState {
    std::vector<WeaponEntry> weapons{};
    std::string active_name{};

    bool operator==(const PlayerWeaponState& o) const {
      return weapons == o.weapons && active_name == o.active_name;
    }
  };

  void TryInitialize();
  [[nodiscard]] CUiPanel* FindHudPanel() const;
  [[nodiscard]] bool RunScript(const std::string& script);
  void SendPlayerWeapons(std::uintptr_t controller,
                         std::span<const systems::Entities::Cached> items);
  [[nodiscard]] bool SendClear(std::uint64_t steamid);
  void ClearAll();

  bool m_script_injected_{};
  bool m_scoreboard_open_{};
  std::unordered_map<std::uint64_t, PlayerWeaponState> m_cache_{};
  int m_throttle_{};
  int m_init_throttle_{};

  // The persistent HUD panel hosts scripts; the scoreboard itself is rebuilt
  // whenever it is opened and is resolved from JavaScript at update time.
  CUiEngine* m_ui_engine_{};
  CUiPanel* m_script_panel_{};
};

}  // namespace features::misc
}  // export
