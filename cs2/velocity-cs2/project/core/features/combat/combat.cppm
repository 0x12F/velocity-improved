module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>
#include <external/xdraw/xdraw.hpp>

export module velocity:core.features.combat.combat;

import <array>;
import <atomic>;
import <cstddef>;
import <cstdint>;
import <deque>;
import <filesystem>;
import <mutex>;
import <optional>;
import <shared_mutex>;
import <string_view>;
import <unordered_map>;
import <vector>;

export import :core.settings;
export import :core.systems.systems;
export import :utilities.math.math;

export {
namespace features::combat {

class Shared {
 public:
  class Lagcomp {
   public:
    struct Record {
      std::uintptr_t pawn{};
      std::uintptr_t game_scene_node{};
      std::uintptr_t bone_cache{};
      int bone_count{};

      systems::Bones::Data bones[128]{};
      systems::Bones::Data bones_backup[128]{};

      math::Vector3 origin{};
      math::Vector3 rotation{};

      float simulation_time{};
      int tick{};
      bool valid{};
      bool was_valid{};
      bool is_applied{};
      bool extrapolated{};

      bool Setup(std::uintptr_t pawn);
      [[nodiscard]] bool IsValid() const;

      void Apply();
      void Restore();
    };

    struct VisualRecord {
      math::Vector3 origin{};
      std::array<systems::Bones::Data, 27> bones{};
    };

    struct ExtrapolationData {
      math::Vector3 origin{};
      math::Vector3 velocity{};
      math::Vector3 obb_mins{};
      math::Vector3 obb_maxs{};
      std::uint32_t flags{};
      float sim_time{};
      float direction{};
    };

    void Run();

    [[nodiscard]] Record* GetOldestValid(std::uintptr_t pawn);
    [[nodiscard]] Record* GetOldestWasValid(std::uintptr_t pawn);
    [[nodiscard]] std::optional<VisualRecord> GetOldestWasValidVisual(
        std::uintptr_t pawn) const;
    [[nodiscard]] std::vector<Record*> GetValidRecords(std::uintptr_t pawn);
    [[nodiscard]] std::array<systems::Bones::Data, 27> GetSkeleton(
        const Record& record) const;

    [[nodiscard]] std::optional<Record> Extrapolate(std::uintptr_t pawn);

   private:
    void PredictMovement(ExtrapolationData& data,
                         std::uintptr_t skip_entity) const;

    std::unordered_map<std::uintptr_t, std::deque<Record>> m_records_{};
    mutable std::shared_mutex m_records_mtx_{};
  };

  class Penetration {
   public:
    struct WeaponData {
      float damage;
      float penetration;
      float range_modifier;
      float range;
      float armor_ratio;
      float headshot_multiplier;
    };

    struct DamageScales {
      float ct_head;
      float t_head;
      float ct_body;
      float t_body;
    };

    struct RunContext {
      std::uintptr_t target_pawn{};
      int target_armor{};
      int target_team{};
      bool has_helmet{};
      DamageScales scales{};
      float armor_ratio{};
      float headshot_multiplier{};
      systems::Hitboxes::Set hitboxes{};
      Lagcomp::Record* record{};
    };

    struct Result {
      float damage{};
      int hitbox{-1};
      int hitgroup{-1};
      bool penetrated{};
    };

    void Prepare(std::uintptr_t weapon_vdata, std::uintptr_t weapon);

    [[nodiscard]] RunContext PrepareTarget(std::uintptr_t target_pawn,
                                           Lagcomp::Record* record) const;
    [[nodiscard]] bool Run(const math::Vector3& start, const math::Vector3& end,
                           const RunContext& ctx, std::uintptr_t local_pawn,
                           int local_team, Result& out) const;
    [[nodiscard]] bool Can(const math::Vector3& start,
                           const math::Vector3& direction, float& out_damage,
                           const systems::Local::Snapshot& local) const;
    [[nodiscard]] float GetMaxDamage(int hitgroup, int target_armor,
                                     bool has_helmet, int target_team) const;
    [[nodiscard]] float GetMaxDamage(int hitgroup, const RunContext& ctx) const;
    [[nodiscard]] const WeaponData& GetWeaponData() const {
      return this->m_weapon_data_;
    }

   private:
    void ScaleDamage(int hitgroup, int armor, bool has_helmet, int team,
                     float armor_ratio, float headshot_multiplier,
                     const DamageScales& scales, float& damage) const;
    WeaponData m_weapon_data_{};
  };

  class ShootHistory {
   public:
    struct RingEntry {
      int tick{};
      float fraction{};
      math::Vector3 position{};
    };

    struct EyeCandidate {
      math::Vector3 position{};
      int player_tick{};
      float player_frac{};
      int lerp_ticks_int{};
      float lerp_ticks_frac{};
      bool is_uninterpolated{};
    };

    struct EyeCandidates {
      EyeCandidate entries[2]{};
      int count{};
    };

    void Snapshot(std::uintptr_t local_pawn, std::uintptr_t weapon_services);
    [[nodiscard]] EyeCandidates GetCandidates() const;
    [[nodiscard]] bool HasData() const { return this->m_count_ > 0; }
    [[nodiscard]] int ClientTick() const { return this->m_client_tick_; }
    [[nodiscard]] float ClientTickFrac() const {
      return this->m_client_tick_frac_;
    }
    [[nodiscard]] int ServerTick() const { return this->m_server_tick_; }
    [[nodiscard]] int LerpTicksInt() const { return this->m_lerp_ticks_int_; }
    [[nodiscard]] float LerpTicksFrac() const {
      return this->m_lerp_ticks_frac_;
    }
    [[nodiscard]] int Count() const { return this->m_count_; }
    [[nodiscard]] int OldestTick() const {
      return this->m_count_ > 0 ? this->m_entries_[0].tick : -1;
    }
    [[nodiscard]] int NewestTick() const {
      return this->m_count_ > 0 ? this->m_entries_[this->m_count_ - 1].tick
                                : -1;
    }

   private:
    RingEntry m_entries_[32]{};
    int m_count_{};
    int m_client_tick_{};
    float m_client_tick_frac_{};
    int m_server_tick_{};
    int m_lerp_ticks_int_{};
    float m_lerp_ticks_frac_{};
  };

  struct Context {
    std::uintptr_t weapon{};
    std::uintptr_t weapon_services{};
    std::uintptr_t weapon_vdata{};
    std::uint32_t weapon_type{};
    std::uint16_t item_def_idx{};
    int num_bullets{};
    float recoil_index{};
    int current_tick{};
    int ticks_since_land{};
    float current_time{};
    float weapon_max_speed{};
    float range{};
    bool is_jump_scouting{};
    bool is_scoped{};
    bool valid{};

    float inaccuracy{};
    float spread{};
  };

  void Update();
  void InvalidateIfNeeded();

  [[nodiscard]] Context& Ctx() { return this->m_ctx_; }
  [[nodiscard]] Penetration& Pen() { return this->m_pen_; }
  [[nodiscard]] Lagcomp& Lc() { return this->m_lc_; }
  [[nodiscard]] ShootHistory& Sh() { return this->m_sh_; }

  [[nodiscard]] bool Autowalling() const { return this->m_autowalling_; }
  [[nodiscard]] Lagcomp::Record* CurrentAutowallRecord() const {
    return this->m_current_autowall_record_;
  }

  [[nodiscard]] int& LastShootTick() { return this->m_last_shoot_tick_; }

  [[nodiscard]] std::uint32_t GetSpreadSeed(const math::Vector3& angles,
                                            int tick) const;
  [[nodiscard]] math::Vector2 CalculateSpread(int seed, float accuracy,
                                              float spread, float recoil_index,
                                              int item_def_idx,
                                              int num_bullets) const;
  [[nodiscard]] math::Vector3 GetAimPunch(std::uintptr_t local_pawn) const;
  [[nodiscard]] float CalculateHitchance(const math::Vector3& shoot_position,
                                         const math::Vector3& aim_angle,
                                         const systems::Hitboxes::Entry& hitbox,
                                         const systems::Bones::Data& bone,
                                         float inaccuracy, float spread,
                                         int samples = 256) const;
  [[nodiscard]] math::Vector3 FindSpreadCorrection(
      const math::Vector3& aim_angle, int tick) const;
  [[nodiscard]] math::Vector3 GetEyePosition(std::uintptr_t local_pawn) const;
  [[nodiscard]] math::Vector3 GetShootPosition() const;
  [[nodiscard]] math::Vector3 GetInterpolatedShootPosition(
      std::uintptr_t local_pawn, bool newest = false) const;
  [[nodiscard]] int CalculateStopTicks(const math::Vector3& velocity,
                                       float max_speed,
                                       std::uintptr_t local_pawn) const;
  [[nodiscard]] float GetSpread() const;
  [[nodiscard]] float GetInaccuracy(bool update_accuracy_penalty) const;
  [[nodiscard]] float GetInaccuracyAtVelocity(
      std::uintptr_t local_pawn, const math::Vector3& velocity) const;
  [[nodiscard]] float GetAirInaccuracy(float vertical_speed, float jump_initial,
                                       float jump_apex) const;
  [[nodiscard]] bool CanShoot(systems::Input::Usercmd* cmd,
                              std::uintptr_t local_controller,
                              bool check_next_attack = true) const;
  [[nodiscard]] bool IsMaxAccuracy(float inaccuracy) const;
  [[nodiscard]] math::Vector3 SimulateAimPunch(int recoil_index) const;

  bool RayVsCapsule(const math::Vector3& ray_origin,
                    const math::Vector3& ray_dir,
                    const math::Vector3& capsule_a,
                    const math::Vector3& capsule_b, float radius,
                    float& out_fraction) const;

 private:
  Context m_ctx_{};
  Penetration m_pen_{};
  Lagcomp m_lc_{};
  ShootHistory m_sh_{};

  // Parallel rage workers must not overwrite each other's trace record.
  inline static thread_local bool m_autowalling_{};
  inline static thread_local Lagcomp::Record* m_current_autowall_record_{
      nullptr};

  int m_last_shoot_tick_{};
};

class Misc {
 private:
  class Antiaim {
   public:
    void OnCreateMove(systems::Input::Usercmd* cmd);
    void OnRender(xdraw::draw_list& draw_list) const;

    [[nodiscard]] bool HasModifiedAngles() const {
      return this->m_should_correct_ ||
             this->m_modified_angles_.y != this->m_old_angles_.y;
    }
    [[nodiscard]] const math::Vector3& GetModifiedAngles() const {
      return this->m_modified_angles_;
    }

   private:
    [[nodiscard]] float GetPitch(float view_pitch);
    [[nodiscard]] float GetYaw(const math::Vector3& view_angles,
                               const systems::Local::Snapshot& local);
    void CorrectMovement(systems::Input::Usercmd* cmd);
    [[nodiscard]] bool IsNearLadder(std::uintptr_t local_pawn) const;

    math::Vector3 m_old_angles_{};
    math::Vector3 m_modified_angles_{};

    int m_yaw_side_{};
    bool m_should_correct_{};
    bool m_antiaim_active_{};
    float m_indicator_yaw_{};
  };

  class Duckpeek {
   public:
    void OnCreateMove(systems::Input::Usercmd* cmd);
    void OnOverrideView(std::uintptr_t view_setup);

   private:
    bool m_was_active_{};
    bool m_fake_stand_active_{};
  };

  class Quickpeek {
   public:
    void OnCreateMove(systems::Input::Usercmd* cmd);
    void ResetIfNeeded();

   private:
    static constexpr std::uint32_t kInvalidEffectIndex{
        static_cast<std::uint32_t>(-1)};

    void CreateParticle();
    void UpdateParticle();
    void ReleaseParticle();
    void Reset();

    math::Vector3 m_saved_origin_{};
    bool m_should_retrack_{};
    bool m_fired_{};
    bool m_active_{};
    std::uint32_t m_particle_effect_{kInvalidEffectIndex};
    bool m_particle_loaded_{};
    std::uintptr_t m_prev_movement_bits_{};
  };

  class Autostop {
   public:
    void OnCreateMove(systems::Input::Usercmd* cmd);

   private:
    [[nodiscard]] float GetEffectiveAccelBase(std::uintptr_t local_pawn,
                                              std::uintptr_t movement_services,
                                              std::uint32_t flags,
                                              float max_weapon_speed) const;
  };

  Antiaim m_antiaim_{};
  Duckpeek m_duckpeek_{};
  Quickpeek m_quickpeek_{};
  Autostop m_autostop_{};

 public:
  [[nodiscard]] Antiaim& Antiaim() { return this->m_antiaim_; }
  [[nodiscard]] Duckpeek& Duckpeek() { return this->m_duckpeek_; }
  [[nodiscard]] Quickpeek& Quickpeek() { return this->m_quickpeek_; }
  [[nodiscard]] Autostop& Autostop() { return this->m_autostop_; }
};

class Rage {
 public:
  void OnCreateMove(systems::Input::Usercmd* cmd);
  void OnRender(xdraw::draw_list& draw_list);

  [[nodiscard]] bool ShouldStop() const noexcept {
    return this->m_should_stop_;
  }
  [[nodiscard]] bool IsFiringThisTick() const noexcept {
    return this->m_firing_this_tick_;
  }
  [[nodiscard]] bool IsCockingRevolver() const noexcept {
    return this->m_revolver_cock_ticks_ > 0;
  }
  [[nodiscard]] bool ShouldReleaseDuckForShot() const noexcept {
    return this->m_release_duck_for_shot_;
  }
  [[nodiscard]] bool DuckpeekWantsReduck() const noexcept {
    return this->m_duckpeek_reduck_;
  }
  void ClearDuckpeekReduck() noexcept { this->m_duckpeek_reduck_ = false; }

  static constexpr auto kMaxLagcompRecords{16};
  // Scan the newest, a middle and the oldest valid record. Newest/oldest
  // alone can skip the server rewind pose that matches bullet impacts.
  static constexpr auto kMaxScanRecords{3};

 private:
  struct AimContext {
    math::Vector3 view_angles{};
    math::Vector3 velocity{};

    float predicted_inaccuracy{};
    float spread{};

    float weapon_max_speed{};
    float accurate_threshold{};
    bool on_ground{};
    bool is_scoped{};
  };

  struct StopPrediction {
    math::Vector3 eye{};
    float inaccuracy{};
  };

  struct HeadScanStats {
    int missing{};
    int invalid_bones{};
    int damage_pruned{};
    int points{};
    int traces{};
    int hits{};
    int wrong_hitbox{};
    int below_damage{};
    float max_damage{};
  };

  struct Candidate {
    std::uintptr_t pawn{};
    int health{};
    int armor{};
    float min_damage{};
    std::array<Shared::Lagcomp::Record*, kMaxLagcompRecords> records{};
    int record_count{};
    HeadScanStats head_scan{};
  };

  struct ScanHit {
    math::Vector3 position{};
    math::Vector3 aim_angle{};
    float damage{};
    float score{};
    float fov{};
    int hitbox_index{};
    int hitgroup{};
    int bone_index{};
    systems::Hitboxes::Entry hitbox{};
    bool is_center{};
    bool penetrated{};
    bool is_backstab{};
    int attack_type{};
    Shared::ShootHistory::EyeCandidate source_eye{};

    std::uintptr_t pawn{};
    int health{};
    Shared::Lagcomp::Record* record{};
  };

  struct Target {
    ScanHit hit{};
    float hitchance{};
    float head_hitchance{};
    float score{};
    bool valid{};

    [[nodiscard]] bool IsLethal() const noexcept {
      return this->hit.damage >= static_cast<float>(this->hit.health);
    }
  };

  struct KnifeInfo {
    bool can_slash{};
    bool can_stab{};
    bool charged{};
    float armor_ratio{};
  };

  [[nodiscard]] AimContext BuildContext(
      const systems::Local::Snapshot& local) const;
  [[nodiscard]] std::optional<StopPrediction> PredictStop(
      const AimContext& ctx, const math::Vector3& current_eye,
      const systems::Local::Snapshot& local) const;
  [[nodiscard]] std::vector<Candidate> GatherCandidates(
      const systems::Local::Snapshot& local,
      float max_distance_sq = 0.0f) const;

  void RunGun(systems::Input::Usercmd* cmd, const AimContext& ctx,
              const systems::Local::Snapshot& local, bool allow_fire = true);
  void RunTaser(systems::Input::Usercmd* cmd, const AimContext& ctx,
                const systems::Local::Snapshot& local);
  void RunKnife(systems::Input::Usercmd* cmd, const AimContext& ctx,
                const systems::Local::Snapshot& local);
  void AutoRevolver(systems::Input::Usercmd* cmd, const AimContext& ctx,
                    const systems::Local::Snapshot& local);

  [[nodiscard]] std::vector<ScanHit> ScanPlayers(
      const math::Vector3& eye, float inaccuracy, const AimContext& ctx,
      std::vector<Candidate>& candidates,
      const systems::Local::Snapshot& local) const;
  [[nodiscard]] std::vector<ScanHit> ScanPlayer(
      const math::Vector3& eye, float inaccuracy, const AimContext& ctx,
      Candidate& cand, Shared::Lagcomp::Record* record,
      const systems::Local::Snapshot& local) const;
  [[nodiscard]] Target SelectBest(const AimContext& aim_ctx,
                                  const std::vector<ScanHit>& hits,
                                  float eval_inaccuracy) const;
  [[nodiscard]] float GetStandingInaccuracy(
      const systems::Local::Snapshot& local, const AimContext& ctx) const;

  [[nodiscard]] std::vector<ScanHit> ScanTaser(
      const math::Vector3& eye, const AimContext& ctx,
      std::vector<Candidate>& candidates,
      const systems::Local::Snapshot& local) const;

  [[nodiscard]] KnifeInfo GetKnifeInfo(
      const systems::Local::Snapshot& local) const;
  [[nodiscard]] std::vector<ScanHit> ScanKnife(
      const math::Vector3& eye, const AimContext& ctx, const KnifeInfo& info,
      std::vector<Candidate>& candidates,
      const systems::Local::Snapshot& local) const;

  void FireGun(systems::Input::Usercmd* cmd, const Target& tgt, bool was_forced,
               const math::Vector3& shoot_eye,
               const systems::Local::Snapshot& local, bool subtick_attack);
  void FireMelee(systems::Input::Usercmd* cmd, const Target& tgt,
                 const systems::Local::Snapshot& local);

  [[nodiscard]] std::vector<math::Vector3> GenerateMultipoints(
      const systems::Hitboxes::Entry& hitbox, const math::Vector3& center,
      const math::Quaternion& bone_rot, float pointscale,
      const math::Vector3& shoot_pos, float inaccuracy) const;
  [[nodiscard]] bool ShouldStopMovement(const AimContext& ctx) const;
  [[nodiscard]] float GetMinDamage(
      const settings::Combat::Ragebot::WeaponGroup& config, int target_health,
      bool override_active) const;
  [[nodiscard]] float GetKnifeDamage(float raw, int armor,
                                     float armor_ratio) const;
  [[nodiscard]] systems::Tracing::Result TraceTaserHit(
      const math::Vector3& origin, const math::Vector3& forward, float range,
      std::uintptr_t target_pawn, std::uintptr_t local_pawn) const;
  [[nodiscard]] systems::Tracing::Result TraceKnifeHit(
      const math::Vector3& origin, const math::Vector3& forward, float reach,
      std::uintptr_t target_pawn, std::uintptr_t local_pawn) const;

  enum class PenetrationCrosshairState : std::uint8_t {
    kUnavailable,
    kBlocked,
    kPenetrable
  };

  void UpdatePenetrationCrosshair(const systems::Local::Snapshot& local);
  void DrawPenetrationCrosshair(xdraw::draw_list& draw_list) const;

  [[nodiscard]] bool ProcessDoubletap(systems::Input::Usercmd* cmd,
                                      const systems::Local::Snapshot& local,
                                      bool charge_dt);

  bool m_should_stop_{};
  bool m_firing_this_tick_{};
  bool m_release_duck_for_shot_{};
  bool m_duckpeek_reduck_{};

  std::uint8_t m_knife_attack_{};
  bool m_zeus_fired_{};

  int m_revolver_cock_ticks_{};
  std::atomic<PenetrationCrosshairState> m_penetration_crosshair_state_{
      PenetrationCrosshairState::kUnavailable};

  std::vector<Shared::Lagcomp::Record> m_extrapolated_records_{};

  struct DebugPoint {
    math::Vector3 position{};
    int hitbox_index{};
    bool is_center{};
  };

  mutable std::vector<DebugPoint> m_debug_points_{};
  mutable std::mutex m_debug_mtx_{};
};

class Legit {
 public:
  void OnCreateMove(systems::Input::Usercmd* cmd);
  void OnRender(xdraw::draw_list& draw_list);
  void InvalidateIfNeeded();

  [[nodiscard]] bool HasTarget() const noexcept {
    return this->m_target_.HasTarget();
  }

 private:
  struct ScanPoint {
    math::Vector3 position{};
    float damage{};
    float fov{};
    int hitgroup{};
    std::size_t cfg_index{};
    int bone_index{};
    systems::Hitboxes::Entry hitbox{};
    bool visible{};
    bool is_center{};
    bool valid{};
  };

  struct TargetResult {
    std::uintptr_t pawn{};
    ScanPoint best_point{};
    math::Vector3 aim_angle{};
    float hitchance{};
    float score{};
    float fov{};
    int health{};
    Shared::Lagcomp::Record* record{};

    [[nodiscard]] bool HasTarget() const noexcept {
      return this->best_point.valid;
    }
  };

  [[nodiscard]] TargetResult FindTarget(
      const math::Vector3& shoot_position, const math::Vector3& view_angles,
      const settings::Combat::Legitbot::WeaponGroup& config,
      const systems::Local::Snapshot& local) const;
  [[nodiscard]] ScanPoint ScanPlayer(
      std::uintptr_t pawn, Shared::Lagcomp::Record* record,
      const math::Vector3& shoot_position, const math::Vector3& view_angles,
      const settings::Combat::Legitbot::WeaponGroup& config,
      const systems::Local::Snapshot& local) const;

  void ApplyAimbot(systems::Input::Usercmd* cmd, const TargetResult& tgt,
                   const math::Vector3& view_angles,
                   const math::Vector3& aim_punch,
                   const settings::Combat::Legitbot::WeaponGroup& config,
                   const systems::Local::Snapshot& local);
  void ApplyTriggerbot(systems::Input::Usercmd* cmd,
                       const math::Vector3& shoot_position,
                       const math::Vector3& view_angles,
                       const math::Vector3& aim_punch,
                       const settings::Combat::Legitbot::WeaponGroup& config,
                       const systems::Local::Snapshot& local);
  void ApplyRcs(math::Vector3& aim_angle, const math::Vector3& aim_punch,
                int rand_min, int rand_max) const;

  void UpdateStandaloneRcs(const math::Vector3& view_angles,
                           const math::Vector3& aim_punch, int amount,
                           int rand_min, int rand_max, bool apply,
                           const systems::Local::Snapshot& local);
  [[nodiscard]] float ComputeRcsFactor(int rand_min, int rand_max) const;

  void DrawFov(xdraw::draw_list& draw_list, const math::Vector3& view_angles,
               const math::Vector3& aim_punch, float fov_degrees,
               const config::col& color, bool rcs_active) const;

  [[nodiscard]] static int HitgroupToCfg(int hitgroup);

  TargetResult m_target_{};
  math::Vector3 m_old_punch_{};
  mutable float m_last_significant_punch_time_{};

  float m_remainder_x_{};
  float m_remainder_y_{};

  float m_trigger_delay_start_{};
  float m_trigger_release_time_{};
  std::uintptr_t m_trigger_pending_pawn_{};

  math::Vector3 m_cached_view_angles_{};
  math::Vector3 m_cached_aim_punch_{};
};

}  // namespace features::combat
}  // export
