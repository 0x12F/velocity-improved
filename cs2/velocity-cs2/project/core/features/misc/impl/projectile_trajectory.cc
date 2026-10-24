module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>
#include <external/xdraw/xdraw.hpp>

#include "core/systems/schema_macros.h"
#include "protection/address_macros.h"

module velocity;

import <cstddef>;
import <filesystem>;
import <limits>;
import <optional>;
import <string_view>;
import <type_traits>;
import <utility>;

namespace features::misc {

namespace {

// Hull sim mask used before, merged with solids mask from visibility traces so
// thin breakables (e.g. glass) collide.
constexpr std::uint32_t kGrenadeCollisionMaskV =
    static_cast<std::uint32_t>(0x2000c3001u | 0x001c3003u);

// Penetration fudge after a pawn hit so the next trace does not re-penetrate
// the same hull.
constexpr float kPassPlayerNudge{3.5f};

[[nodiscard]] bool GrenadePassesThroughHitEntity(std::uintptr_t entity) {
  if (!entity) {
    return false;
  }

  const auto* name = systems::g_entities.GetSchemaName(entity);
  if (!name) {
    return false;
  }

  // CS grenade simulation does not collide with player pawns; traces that stop
  // on them fake bounces.
  return fnv1a::RuntimeHash(name) == "C_CSPlayerPawn"_hash;
}

[[nodiscard]] systems::Tracing::Filter MakeGrenadeHullFilter(
    std::uintptr_t thrower_pawn, int sim_tick) {
  const auto skip_thrower =
      thrower_pawn &&
      sim_tick < ProjectileTrajectory::kThrowerCollisionSkipTicks;
  return systems::g_tracing.MakeFilter(skip_thrower ? thrower_pawn : 0,
                                       kGrenadeCollisionMaskV, 4);
}

// Segment trace: advance through player hulls to match in-game grenades, stop
// on world/geo.
[[nodiscard]] std::pair<math::Vector3, std::optional<systems::Tracing::Result>>
GrenadeTraceHullSegment(const math::Vector3& from, const math::Vector3& to,
                        const math::Vector3& hull_mins,
                        const math::Vector3& hull_maxs,
                        std::uintptr_t thrower_pawn, int sim_tick) {
  auto cursor = from;

  for (auto pass = 0; pass < 16; ++pass) {
    const auto remainder = to - cursor;
    const auto remainder_len_sq = remainder.LengthSqr();
    if (remainder_len_sq < 1.0e-10f) {
      return {to, std::nullopt};
    }

    const auto filter = MakeGrenadeHullFilter(thrower_pawn, sim_tick);
    const auto trace =
        systems::g_tracing.TraceHull(cursor, to, hull_mins, hull_maxs, filter);

    if (trace.fraction >= 1.0f) {
      return {to, std::nullopt};
    }

    if (GrenadePassesThroughHitEntity(trace.hit_entity)) {
      const auto rem_len = std::sqrt(remainder_len_sq);
      const auto nd = remainder * (1.0f / rem_len);
      cursor = trace.end_pos + nd * kPassPlayerNudge;
      continue;
    }

    return {trace.end_pos, trace};
  }

  return {cursor, std::nullopt};
}

}  // namespace

void ProjectileTrajectory::OnRender(xdraw::draw_list& draw_list) {
  const auto local = systems::g_local.Get();
  if (!settings::g_misc.m_projectile_trajectory.enabled.value ||
      systems::g_local.IsInCinematic() || !local.is_alive ||
      !local.controller) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  const auto local_pawn_handle = memory::Read<std::uint32_t>(
      local.controller + SCHEMA("CBasePlayerController", "m_hPawn"_hash));

  if (!local_pawn_handle) {
    return;
  }

  for (auto& gren : this->m_in_flight_) {
    if (!gren.traj.valid) {
      continue;
    }

    if (gren.thrower_handle != local_pawn_handle) {
      continue;
    }

    if (!gren.detonated) {
      const auto elapsed =
          std::chrono::duration<float>(now - gren.throw_time).count();
      if (elapsed >= gren.traj.duration) {
        gren.detonated = true;
        gren.detonate_time = now;
      }
    }

    auto alpha{1.0f};

    if (gren.detonated) {
      const auto elapsed =
          std::chrono::duration<float>(now - gren.detonate_time).count();
      alpha = std::clamp(1.0f - elapsed / kFadeDuration, 0.0f, 1.0f);
    }

    if (alpha > 0.0f) {
      this->RenderTrajectory(draw_list, gren.traj, alpha, true, local);
    }
  }

  this->RenderPreview(draw_list, local);
}

void ProjectileTrajectory::OnCreateMove(systems::Input::Usercmd* cmd) {
  this->m_should_preview_ = false;
  this->m_needs_air_stop_ = false;

  if (!settings::g_misc.m_projectile_trajectory.enabled.value ||
      systems::g_local.IsInCinematic()) {
    this->m_delay_release_ = false;
    this->m_delay_ticks_ = 0;
    return;
  }

  const auto local = systems::g_local.Get();
  if (!local.pawn || !local.is_alive) {
    this->m_delay_release_ = false;
    this->m_delay_ticks_ = 0;
    return;
  }

  this->m_sv_gravity_ = CONVAR("sv_gravity")->Get<float>();
  this->m_molotov_max_slope_z_ =
      std::cos(CONVAR("weapon_molotov_maxdetonateslope")->Get<float>() *
               std::numbers::pi_v<float> / 180.0f);

  this->UpdateInFlight(local);

  const auto& ctx = combat::g_shared.Ctx();
  if (ctx.weapon_type != cstypes::weapon_type::kGrenade) {
    this->m_delay_release_ = false;
    this->m_delay_ticks_ = 0;
    return;
  }

  this->UpdateWeaponProperties(ctx.weapon, ctx.weapon_vdata);

  const auto pin_pulled = memory::Read<bool>(
      ctx.weapon + SCHEMA("C_BaseCSGrenade", "m_bPinPulled"_hash));
  const auto throw_time = memory::Read<float>(
      ctx.weapon + SCHEMA("C_BaseCSGrenade", "m_fThrowTime"_hash));
  const auto attacking =
      (cmd->buttons.value & cstypes::command_buttons::kInAttack) != 0;
  const auto attacking2 =
      (cmd->buttons.value & cstypes::command_buttons::kInSecondAttack) != 0;
  const auto holding_attack = attacking || attacking2;

  if (throw_time > 0.0f) {
    this->m_delay_release_ = false;
    this->m_delay_ticks_ = 0;
    this->m_throw_stopping_ = true;

    if (settings::g_misc.m_projectile_trajectory.straight_throw.value) {
      this->m_needs_air_stop_ = !this->m_was_forward_only_;
      this->CorrectThrowAngles(cmd, local, ctx.weapon);
    }

    return;
  } else {
    if (this->m_throw_stopping_) {
      this->m_last_throw_time_ = std::chrono::steady_clock::now();
    }

    this->m_throw_stopping_ = false;
  }

  if (holding_attack && pin_pulled) {
    this->m_delay_release_ = false;
    this->m_delay_ticks_ = 0;

    const auto has_forward =
        (cmd->buttons.value & cstypes::command_buttons::kInForward) != 0;
    const auto has_back =
        (cmd->buttons.value & cstypes::command_buttons::kInBack) != 0;
    const auto has_left =
        (cmd->buttons.value & cstypes::command_buttons::kInMoveleft) != 0;
    const auto has_right =
        (cmd->buttons.value & cstypes::command_buttons::kInMoveright) != 0;

    this->m_was_forward_only_ =
        has_forward && !has_back && !has_left && !has_right;
  } else if (pin_pulled && !holding_attack &&
             settings::g_misc.m_projectile_trajectory.straight_throw.value) {
    if (!this->m_delay_release_) {
      this->m_delay_release_ = true;
      this->m_delay_ticks_ = 0;
      this->m_delayed_strength_ = std::clamp(
          memory::Read<float>(
              ctx.weapon + SCHEMA("C_BaseCSGrenade", "m_flThrowStrength"_hash)),
          0.0f, 1.0f);
      this->m_delayed_attack2_ = !attacking && attacking2;
    }

    const auto current_velocity =
        systems::g_prediction.Pre().networked_velocity;

    const auto vel_contribution = current_velocity * kVelocityInherit;
    const auto throw_vel =
        std::clamp(this->m_throw_velocity_ * 0.9f, 15.0f, 750.0f);
    const auto throw_speed =
        (this->m_delayed_strength_ * 0.7f + 0.3f) * throw_vel;

    math::Vector3 desired_forward{};
    this->ComputeDesiredDirection(desired_forward);

    const auto vel_along =
        desired_forward * vel_contribution.Dot(desired_forward);
    const auto vel_perp = vel_contribution - vel_along;
    const auto perp_len = std::sqrt(vel_perp.LengthSqr());
    const auto can_compensate = perp_len < throw_speed;

    if (can_compensate || this->m_was_forward_only_ ||
        this->m_delay_ticks_ >= kMaxDelayTicks) {
      this->m_delay_release_ = false;
      this->m_delay_ticks_ = 0;
      this->m_needs_air_stop_ = !this->m_was_forward_only_;
    } else {
      if (this->m_delayed_attack2_) {
        cmd->buttons.value |= cstypes::command_buttons::kInSecondAttack;
        cmd->buttons.value_changed |= cstypes::command_buttons::kInSecondAttack;
      } else {
        cmd->buttons.value |= cstypes::command_buttons::kInAttack;
        cmd->buttons.value_changed |= cstypes::command_buttons::kInAttack;
      }

      this->m_needs_air_stop_ = true;
      ++this->m_delay_ticks_;
      this->m_should_preview_ = true;
      return;
    }
  }

  this->m_was_holding_ = pin_pulled;

  if (!pin_pulled) {
    const auto since_throw =
        std::chrono::duration<float>(std::chrono::steady_clock::now() -
                                     this->m_last_throw_time_)
            .count();
    if (since_throw < kThrowCooldown) {
      return;
    }
  }

  this->m_should_preview_ = true;
}

void ProjectileTrajectory::SetupThrow(std::uintptr_t local_pawn,
                                      std::uintptr_t weapon,
                                      math::Vector3& origin,
                                      math::Vector3& velocity) {
  auto strength{1.0f};

  const auto pin_pulled = memory::Read<bool>(
      weapon + SCHEMA("C_BaseCSGrenade", "m_bPinPulled"_hash));
  const auto throw_time = memory::Read<float>(
      weapon + SCHEMA("C_BaseCSGrenade", "m_fThrowTime"_hash));

  if (pin_pulled || throw_time > 0.0f) {
    strength = std::clamp(
        memory::Read<float>(
            weapon + SCHEMA("C_BaseCSGrenade", "m_flThrowStrength"_hash)),
        0.0f, 1.0f);

    if (std::abs(strength - 0.5f) <= 0.1f) {
      strength = 0.5f;
    }
  }

  auto angles = systems::g_input.GetViewAngles();
  math::helpers::NormalizeAngle(angles.x);
  angles.x -= (90.0f - std::abs(angles.x)) * 10.0f / 90.0f;

  auto eye_pos =
      systems::g_frame_data.Origin() +
      memory::Read<math::Vector3>(
          local_pawn + SCHEMA("C_BaseModelEntity", "m_vecViewOffset"_hash));
  eye_pos.z += strength * 12.0f - 12.0f;

  math::Vector3 forward{};
  {
    const auto pitch = angles.x * (std::numbers::pi_v<float> / 180.0f);
    const auto yaw = angles.y * (std::numbers::pi_v<float> / 180.0f);

    forward = {std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw),
               -std::sin(pitch)};
  }

  const auto hull_mins = math::Vector3{-kHullSize, -kHullSize, -kHullSize};
  const auto hull_maxs = math::Vector3{kHullSize, kHullSize, kHullSize};
  const auto trace = systems::g_tracing.TraceHull(
      eye_pos, eye_pos + forward * kForwardOffset, hull_mins, hull_maxs,
      local_pawn, kGrenadeCollisionMaskV);
  origin = trace.end_pos - forward * kPullBack;

  const auto throw_velocity =
      std::clamp(this->m_throw_velocity_ * 0.9f, 15.0f, 750.0f);
  const auto throw_speed = (strength * 0.7f + 0.3f) * throw_velocity;
  velocity = forward * throw_speed +
             systems::g_prediction.Pre().networked_velocity * kVelocityInherit;
}

void ProjectileTrajectory::Simulate(const math::Vector3& start,
                                    const math::Vector3& velocity,
                                    std::uintptr_t thrower_pawn,
                                    Trajectory& out) {
  out.points.clear();
  out.points.reserve(kMaxTicks / kTicksPerPoint);
  out.bounces.clear();
  out.valid = false;
  out.end_tick = -1;

  auto pos = start;
  auto vel = velocity;
  auto bounce_count{0};
  auto tick_timer{0};

  for (auto tick = 0; tick < kMaxTicks; ++tick) {
    if (tick_timer == 0) {
      out.points.push_back({pos});
    }

    auto hit{false};
    auto impact_detonate{false};
    this->StepSimulation(pos, vel, thrower_pawn, tick, hit, impact_detonate);

    if (hit) {
      ++bounce_count;
      out.bounces.push_back(pos);
    }

    const auto velocity_stopped = std::abs(vel.x) < 20.0f &&
                                  std::abs(vel.y) < 20.0f &&
                                  vel.LengthSqr() < kStopSpeedSq;

    if (impact_detonate || this->ShouldDetonate(vel, tick) ||
        bounce_count > kMaxBounces || velocity_stopped) {
      out.end_tick = tick;
      out.end_pos = pos;
      out.duration = static_cast<float>(tick) * cstypes::kTickInterval;
      break;
    }

    if (hit || ++tick_timer >= kTicksPerPoint) {
      tick_timer = 0;
    }
  }

  if (!out.points.empty() && out.end_tick >= 0) {
    if (out.points.back().DistanceSqr(out.end_pos) > 1.0f) {
      out.points.push_back({out.end_pos});
    }
  }

  out.valid = out.end_tick >= 0;
}

void ProjectileTrajectory::StepSimulation(math::Vector3& pos,
                                          math::Vector3& vel,
                                          std::uintptr_t thrower_pawn,
                                          int sim_tick, bool& hit,
                                          bool& detonated) {
  detonated = false;

  const auto gravity = this->m_sv_gravity_ * kGravityScale;
  const auto new_vel_z = vel.z - gravity * cstypes::kTickInterval;

  const math::Vector3 move{vel.x * cstypes::kTickInterval,
                           vel.y * cstypes::kTickInterval,
                           (vel.z + new_vel_z) * 0.5f * cstypes::kTickInterval};

  vel.z = new_vel_z;

  const auto hull_mins = math::Vector3{-kHullSize, -kHullSize, -kHullSize};
  const auto hull_maxs = math::Vector3{kHullSize, kHullSize, kHullSize};
  const auto move_dest = pos + move;

  const auto [travel_end, world_hit_trace] = GrenadeTraceHullSegment(
      pos, move_dest, hull_mins, hull_maxs, thrower_pawn, sim_tick);

  pos = travel_end;

  if (world_hit_trace.has_value()) {
    hit = true;
    this->ResolveCollision(*world_hit_trace, pos, vel, thrower_pawn, sim_tick,
                           detonated);
  } else {
    hit = false;
  }
}

void ProjectileTrajectory::ResolveCollision(
    const systems::Tracing::Result& trace, math::Vector3& pos,
    math::Vector3& vel, std::uintptr_t thrower_pawn, int sim_tick,
    bool& detonated) const {
  detonated = false;

  if (this->m_weapon_hash_ == "weapon_molotov"_hash ||
      this->m_weapon_hash_ == "weapon_incgrenade"_hash) {
    if (trace.normal.z >= this->m_molotov_max_slope_z_ ||
        vel.LengthSqr() < kStopSpeedSq) {
      detonated = true;
      vel = {};
      return;
    }
  }

  auto new_vel = ClipVelocity(vel, trace.normal, 2.0f);

  new_vel.x *= kElasticity;
  new_vel.y *= kElasticity;
  new_vel.z *= kElasticity;

  if (trace.hit_entity) {
    if (!GrenadePassesThroughHitEntity(trace.hit_entity)) {
      const auto health = memory::Read<int>(
          trace.hit_entity + SCHEMA("C_BaseEntity", "m_iHealth"_hash));
      if (health > 0 && health <= 100) {
        if (trace.fraction > kPlayerHitFractionThreshold) {
          const auto speed_sq = new_vel.LengthSqr();

          if (speed_sq > kSteepBounceSpeedSq) {
            const auto dot = new_vel.Normalized().Dot(trace.normal);

            if (dot > kPlayerDampenDotThreshold) {
              const auto dampen = (1.0f - dot) + 0.5f;
              new_vel.x *= dampen;
              new_vel.y *= dampen;
              new_vel.z *= dampen;
            }
          }
        }
      }
    }
  }

  if (trace.normal.z > kSteepBounceNormalZ) {
    const auto speed_sq = new_vel.LengthSqr();

    if (speed_sq > kSteepBounceSpeedSq) {
      const auto dot = new_vel.Normalized().Dot(trace.normal);
      if (dot > 0.5f) {
        const auto dampen = 1.5f - dot;
        new_vel.x *= dampen;
        new_vel.y *= dampen;
        new_vel.z *= dampen;
      }
    }
  }

  if (new_vel.LengthSqr() < kStopSpeedSq) {
    vel = {};
    return;
  }

  vel = new_vel;

  const auto remaining = 1.0f - trace.fraction;
  if (remaining > 0.0f) {
    const auto hull_mins = math::Vector3{-kHullSize, -kHullSize, -kHullSize};
    const auto hull_maxs = math::Vector3{kHullSize, kHullSize, kHullSize};
    const auto slide_goal =
        pos + new_vel * (remaining * cstypes::kTickInterval);

    const auto [slide_end, slide_hit] = GrenadeTraceHullSegment(
        pos, slide_goal, hull_mins, hull_maxs, thrower_pawn, sim_tick);

    (void)slide_hit;

    pos = slide_end;
  }
}

void ProjectileTrajectory::SetSimulationParams(std::uintptr_t weapon_hash) {
  switch (weapon_hash) {
    case "weapon_molotov"_hash:
    case "weapon_incgrenade"_hash:
      this->m_detonate_time_ = 2.0f;
      this->m_velocity_threshold_ = 0.0f;
      break;
    case "weapon_decoy"_hash:
      this->m_detonate_time_ = 2.0f;
      this->m_velocity_threshold_ = 0.2f;
      break;
    default:
      this->m_detonate_time_ = 1.5f;
      this->m_velocity_threshold_ = 0.1f;
      break;
  }
}

bool ProjectileTrajectory::ShouldDetonate(const math::Vector3& vel,
                                          int tick) const {
  switch (this->m_weapon_hash_) {
    case "weapon_smokegrenade"_hash:
    case "weapon_decoy"_hash: {
      const auto speed_2d = std::sqrt(vel.x * vel.x + vel.y * vel.y);
      const auto check_ticks = static_cast<int>(0.2f / cstypes::kTickInterval);
      return speed_2d < this->m_velocity_threshold_ &&
             (tick % check_ticks) == 0;
    }
    case "weapon_molotov"_hash:
    case "weapon_incgrenade"_hash:
      return static_cast<float>(tick) * cstypes::kTickInterval >
             this->m_detonate_time_;
    case "weapon_flashbang"_hash:
    case "weapon_hegrenade"_hash:
      return static_cast<float>(tick - 8) * cstypes::kTickInterval >
             this->m_detonate_time_;
    default:
      return false;
  }
}

math::Vector3 ProjectileTrajectory::ClipVelocity(const math::Vector3& velocity,
                                                 const math::Vector3& normal,
                                                 float overbounce) {
  const auto backoff = velocity.Dot(normal) * overbounce;

  math::Vector3 out{velocity.x - normal.x * backoff,
                    velocity.y - normal.y * backoff,
                    velocity.z - normal.z * backoff};

  if (std::abs(out.x) < 0.1f) out.x = 0.0f;
  if (std::abs(out.y) < 0.1f) out.y = 0.0f;
  if (std::abs(out.z) < 0.1f) out.z = 0.0f;

  return out;
}

void ProjectileTrajectory::UpdateWeaponProperties(std::uintptr_t weapon,
                                                  std::uintptr_t weapon_vdata) {
  if (!weapon_vdata || weapon_vdata == this->m_weapon_vdata_) {
    return;
  }

  this->m_weapon_vdata_ = weapon_vdata;
  this->m_throw_velocity_ = std::clamp(
      memory::Read<float>(weapon_vdata + SCHEMA("CCSWeaponBaseVData",
                                                "m_flThrowVelocity"_hash)),
      1.0f, 10000.0f);

  const auto name_ptr = memory::Read<std::uintptr_t>(
      weapon_vdata + SCHEMA("CCSWeaponBaseVData", "m_szName"_hash));
  if (!name_ptr) {
    this->m_weapon_hash_ = 0;
    this->m_detonate_time_ = 1.5f;
    this->m_velocity_threshold_ = 0.1f;
    return;
  }

  const auto name = memory::ReadString(name_ptr, 64);
  this->m_weapon_hash_ = fnv1a::RuntimeHash(name.c_str());

  switch (this->m_weapon_hash_) {
    case "weapon_molotov"_hash:
    case "weapon_incgrenade"_hash:
      this->m_detonate_time_ = 2.0f;
      this->m_velocity_threshold_ = 0.0f;
      break;
    case "weapon_decoy"_hash:
      this->m_detonate_time_ = 2.0f;
      this->m_velocity_threshold_ = 0.2f;
      break;
    default:
      this->m_detonate_time_ = 1.5f;
      this->m_velocity_threshold_ = 0.1f;
      break;
  }
}

void ProjectileTrajectory::UpdateInFlight(
    const systems::Local::Snapshot& local) {
  this->m_sv_gravity_ = CONVAR("sv_gravity")->Get<float>();
  this->m_molotov_max_slope_z_ =
      std::cos(CONVAR("weapon_molotov_maxdetonateslope")->Get<float>() *
               std::numbers::pi_v<float> / 180.0f);

  const auto local_pawn_handle = memory::Read<std::uint32_t>(
      local.controller + SCHEMA("CBasePlayerController", "m_hPawn"_hash));
  const auto local_team = memory::Read<std::int32_t>(
      local.pawn + SCHEMA("C_BaseEntity", "m_iTeamNum"_hash));
  const auto now = std::chrono::steady_clock::now();
  const auto projectiles =
      systems::g_entities.GetByType(systems::Entities::Type::kProjectile);

  std::unordered_set<std::uintptr_t> alive{};

  for (const auto& entry : projectiles) {
    if (!entry.ptr) {
      continue;
    }

    const auto wh = this->GetOtherName(entry.schema_hash);
    if (!wh) {
      continue;
    }

    const auto thrower_handle = memory::Read<std::uint32_t>(
        entry.ptr + SCHEMA("C_BaseGrenade", "m_hThrower"_hash));
    auto is_enemy{true};

    if (thrower_handle == local_pawn_handle) {
      is_enemy = false;
    } else if (thrower_handle && thrower_handle != 0xffffffff) {
      const auto thrower = systems::g_entities.Lookup(thrower_handle);
      if (thrower) {
        const auto thrower_team = memory::Read<std::int32_t>(
            thrower + SCHEMA("C_BaseEntity", "m_iTeamNum"_hash));
        is_enemy = thrower_team != local_team;
      }
    }

    alive.insert(entry.ptr);

    InFlightGrenade* existing{nullptr};

    for (auto& gren : this->m_in_flight_) {
      if (gren.entity == entry.ptr) {
        existing = &gren;
        break;
      }
    }

    if (existing) {
      existing->last_seen = now;

      if (!existing->corrected) {
        const auto initial_pos = memory::Read<math::Vector3>(
            entry.ptr +
            SCHEMA("C_BaseCSGrenadeProjectile", "m_vInitialPosition"_hash));
        const auto initial_vel = memory::Read<math::Vector3>(
            entry.ptr +
            SCHEMA("C_BaseCSGrenadeProjectile", "m_vInitialVelocity"_hash));

        if (initial_vel.LengthSqr() >= 1.0f) {
          const auto saved_hash = this->m_weapon_hash_;
          const auto saved_detonate = this->m_detonate_time_;
          const auto saved_threshold = this->m_velocity_threshold_;

          this->m_weapon_hash_ = existing->weapon_hash;
          this->SetSimulationParams(existing->weapon_hash);

          const auto ignore =
              existing->thrower_handle
                  ? systems::g_entities.Lookup(existing->thrower_handle)
                  : local.pawn;
          this->Simulate(initial_pos, initial_vel, ignore ? ignore : local.pawn,
                         existing->traj);

          this->m_weapon_hash_ = saved_hash;
          this->m_detonate_time_ = saved_detonate;
          this->m_velocity_threshold_ = saved_threshold;

          existing->corrected = true;
        }
      }

      if (!existing->detonated) {
        auto effect_started{false};

        switch (wh) {
          case "weapon_hegrenade"_hash:
          case "weapon_flashbang"_hash:
            effect_started =
                memory::Read<std::int32_t>(
                    entry.ptr + SCHEMA("C_BaseCSGrenadeProjectile",
                                       "m_nExplodeEffectTickBegin"_hash)) > 0;
            break;
          case "weapon_smokegrenade"_hash:
            effect_started =
                memory::Read<std::int32_t>(
                    entry.ptr + SCHEMA("C_SmokeGrenadeProjectile",
                                       "m_nSmokeEffectTickBegin"_hash)) > 0;
            break;
          case "weapon_decoy"_hash:
            effect_started =
                memory::Read<std::int32_t>(
                    entry.ptr +
                    SCHEMA("C_DecoyProjectile", "m_nDecoyShotTick"_hash)) > 0;
            break;
          default:
            break;
        }

        if (effect_started) {
          existing->detonated = true;
          existing->detonate_time = now;
        }
      }

      continue;
    }

    {
      auto already_detonated{false};

      switch (wh) {
        case "weapon_hegrenade"_hash:
        case "weapon_flashbang"_hash:
          already_detonated =
              memory::Read<std::int32_t>(
                  entry.ptr + SCHEMA("C_BaseCSGrenadeProjectile",
                                     "m_nExplodeEffectTickBegin"_hash)) > 0;
          break;
        case "weapon_smokegrenade"_hash:
          already_detonated =
              memory::Read<std::int32_t>(
                  entry.ptr + SCHEMA("C_SmokeGrenadeProjectile",
                                     "m_nSmokeEffectTickBegin"_hash)) > 0;
          break;
        case "weapon_decoy"_hash:
          already_detonated =
              memory::Read<std::int32_t>(
                  entry.ptr +
                  SCHEMA("C_DecoyProjectile", "m_nDecoyShotTick"_hash)) > 0;
          break;
        default:
          break;
      }

      if (already_detonated) {
        continue;
      }
    }

    const auto initial_pos = memory::Read<math::Vector3>(
        entry.ptr +
        SCHEMA("C_BaseCSGrenadeProjectile", "m_vInitialPosition"_hash));
    const auto initial_vel = memory::Read<math::Vector3>(
        entry.ptr +
        SCHEMA("C_BaseCSGrenadeProjectile", "m_vInitialVelocity"_hash));

    if (initial_vel.LengthSqr() < 1.0f) {
      continue;
    }

    const auto saved_hash = this->m_weapon_hash_;
    const auto saved_detonate = this->m_detonate_time_;
    const auto saved_threshold = this->m_velocity_threshold_;

    this->m_weapon_hash_ = wh;
    this->SetSimulationParams(wh);

    InFlightGrenade gren{};
    gren.entity = entry.ptr;
    gren.weapon_hash = wh;
    gren.thrower_handle = thrower_handle;
    gren.is_enemy = is_enemy;
    gren.throw_time = now;
    gren.last_seen = now;
    gren.corrected = true;

    const auto ignore = thrower_handle
                            ? systems::g_entities.Lookup(thrower_handle)
                            : local.pawn;
    this->Simulate(initial_pos, initial_vel, ignore ? ignore : local.pawn,
                   gren.traj);

    this->m_weapon_hash_ = saved_hash;
    this->m_detonate_time_ = saved_detonate;
    this->m_velocity_threshold_ = saved_threshold;

    this->m_in_flight_.push_back(std::move(gren));
  }

  for (auto& gren : this->m_in_flight_) {
    if (gren.detonated || alive.contains(gren.entity)) {
      continue;
    }

    if (std::chrono::duration<float>(now - gren.last_seen).count() >=
        kMissingGrace) {
      gren.detonated = true;
      gren.detonate_time = now;
    }
  }

  std::erase_if(this->m_in_flight_, [&now, &alive](const InFlightGrenade& g) {
    if (!g.detonated) {
      return false;
    }

    return std::chrono::duration<float>(now - g.detonate_time).count() >
               kFadeDuration &&
           !alive.contains(g.entity);
  });
}

std::uintptr_t ProjectileTrajectory::GetOtherName(std::uint32_t schema_hash) {
  switch (schema_hash) {
    case "C_HEGrenadeProjectile"_hash:
      return "weapon_hegrenade"_hash;
    case "C_FlashbangProjectile"_hash:
      return "weapon_flashbang"_hash;
    case "C_SmokeGrenadeProjectile"_hash:
      return "weapon_smokegrenade"_hash;
    case "C_MolotovProjectile"_hash:
      return "weapon_molotov"_hash;
    case "C_DecoyProjectile"_hash:
      return "weapon_decoy"_hash;
    default:
      return 0;
  }
}

void ProjectileTrajectory::CorrectThrowAngles(
    systems::Input::Usercmd* cmd, const systems::Local::Snapshot& local,
    std::uintptr_t weapon) {
  const auto throw_time = memory::Read<float>(
      weapon + SCHEMA("C_BaseCSGrenade", "m_fThrowTime"_hash));
  if (throw_time <= 0.0f) {
    return;
  }

  auto strength =
      std::clamp(memory::Read<float>(weapon + SCHEMA("C_BaseCSGrenade",
                                                     "m_flThrowStrength"_hash)),
                 0.0f, 1.0f);
  if (std::abs(strength - 0.5f) <= 0.1f) {
    strength = 0.5f;
  }

  const auto throw_vel =
      std::clamp(this->m_throw_velocity_ * 0.9f, 15.0f, 750.0f);
  const auto throw_speed = (strength * 0.7f + 0.3f) * throw_vel;

  math::Vector3 desired_forward{};
  this->ComputeDesiredDirection(desired_forward);

  const auto base = cmd->csgo_user_cmd.MutableBase();
  if (!base) {
    return;
  }

  const auto current_velocity = systems::g_prediction.Pre().networked_velocity;

  if (current_velocity.LengthSqr() < 1.0f) {
    return;
  }

  const auto vel_contribution = current_velocity * kVelocityInherit;
  const auto vel_along =
      desired_forward * vel_contribution.Dot(desired_forward);
  const auto vel_perp = vel_contribution - vel_along;
  const auto perp_len_sq = vel_perp.LengthSqr();

  if (perp_len_sq >= throw_speed * throw_speed) {
    return;
  }

  const auto forward_component =
      std::sqrt(throw_speed * throw_speed - perp_len_sq);
  const auto total_along_desired =
      vel_contribution.Dot(desired_forward) + forward_component;

  if (total_along_desired <= 0.0f) {
    return;
  }

  auto corrected = (desired_forward * total_along_desired - vel_contribution) *
                   (1.0f / throw_speed);
  auto len = corrected.Length();

  if (len < 0.001f) {
    return;
  }

  corrected = corrected * (1.0f / len);

  auto corrected_pitch = -std::asin(std::clamp(corrected.z, -1.0f, 1.0f)) *
                         (180.0f / std::numbers::pi_v<float>);
  auto corrected_yaw = std::atan2(corrected.y, corrected.x) *
                       (180.0f / std::numbers::pi_v<float>);
  auto input_pitch = corrected_pitch;

  for (auto i = 0; i < 16; ++i) {
    const auto bias = (90.0f - std::abs(input_pitch)) * 10.0f / 90.0f;
    input_pitch = corrected_pitch + bias;
  }

  input_pitch = std::clamp(input_pitch, -89.0f, 89.0f);

  base->MutableViewangles()->SetX(input_pitch);
  base->MutableViewangles()->SetY(corrected_yaw);
}

void ProjectileTrajectory::ComputeDesiredDirection(
    math::Vector3& desired_forward) const {
  auto angles = systems::g_input.GetViewAngles();

  if (angles.x > 90.0f) {
    angles.x -= 360.0f;
  } else if (angles.x < -90.0f) {
    angles.x += 360.0f;
  }

  angles.x -= (90.0f - std::abs(angles.x)) * 10.0f / 90.0f;

  const auto pitch = angles.x * (std::numbers::pi_v<float> / 180.0f);
  const auto yaw = angles.y * (std::numbers::pi_v<float> / 180.0f);

  desired_forward = {std::cos(pitch) * std::cos(yaw),
                     std::cos(pitch) * std::sin(yaw), -std::sin(pitch)};
}

void ProjectileTrajectory::ComputeDamageAtEndpoint(
    const Trajectory& traj, std::vector<DamageInfo>& damages,
    const systems::Local::Snapshot& local) const {
  damages.clear();

  if (!traj.valid || traj.end_tick < 0) {
    return;
  }

  if (this->m_weapon_hash_ != "weapon_hegrenade"_hash) {
    return;
  }

  constexpr auto kBaseDamage{99.0f};
  constexpr auto kDamageRadius{350.0f};
  constexpr auto kSigma{116.6f};

  const auto trace_start = traj.end_pos + math::Vector3{0.0f, 0.0f, 1.0f};
  const auto players =
      systems::g_entities.GetByType(systems::Entities::Type::kPlayer);

  for (const auto& player : players) {
    if (!player.ptr || player.ptr == local.controller) {
      continue;
    }

    if (!memory::Read<bool>(player.ptr + SCHEMA("CCSPlayerController",
                                                "m_bPawnIsAlive"_hash))) {
      continue;
    }

    const auto pawn_handle = memory::Read<std::uint32_t>(
        player.ptr + SCHEMA("CBasePlayerController", "m_hPawn"_hash));
    const auto pawn = systems::g_entities.Lookup(pawn_handle);

    if (!pawn || pawn == local.pawn) {
      continue;
    }

    const auto team = memory::Read<std::int32_t>(
        pawn + SCHEMA("C_BaseEntity", "m_iTeamNum"_hash));
    if (!local.IsThisOtherTeam(team)) {
      continue;
    }

    const auto health = memory::Read<std::int32_t>(
        pawn + SCHEMA("C_BaseEntity", "m_iHealth"_hash));
    if (health <= 0) {
      continue;
    }

    const auto game_scene_node = memory::Read<std::uintptr_t>(
        pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
    if (!game_scene_node) {
      continue;
    }

    if (memory::Read<bool>(game_scene_node +
                           SCHEMA("CGameSceneNode", "m_bDormant"_hash))) {
      continue;
    }

    const auto player_origin = memory::Read<math::Vector3>(
        game_scene_node + SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_hash));
    const auto center = player_origin + math::Vector3{0.0f, 0.0f, 36.0f};
    const auto delta = center - traj.end_pos;
    const auto distance = std::sqrt(delta.LengthSqr());

    if (distance > kDamageRadius) {
      continue;
    }

    const auto trace =
        systems::g_tracing.Trace(trace_start, center, local.pawn);
    const auto visible = trace.fraction >= 0.97f || trace.hit_entity == pawn;

    if (!visible) {
      continue;
    }

    const auto falloff =
        std::exp(-(distance * distance) / (2.0f * kSigma * kSigma));
    auto adjusted_damage = kBaseDamage * falloff;
    const auto armor = memory::Read<std::int32_t>(
        pawn + SCHEMA("C_CSPlayerPawn", "m_ArmorValue"_hash));

    if (armor > 0) {
      constexpr auto kArmorRatio{1.2f * 0.5f};
      const auto damage_to_health = adjusted_damage * kArmorRatio;
      const auto damage_to_armor = (adjusted_damage - damage_to_health);

      if (damage_to_armor <= static_cast<float>(armor)) {
        adjusted_damage = damage_to_health;
      } else {
        adjusted_damage = adjusted_damage - static_cast<float>(armor);
      }
    }

    const auto final_damage = static_cast<int>(adjusted_damage);
    if (final_damage <= 0) {
      continue;
    }

    damages.push_back({final_damage, final_damage >= health, center});
  }
}

void ProjectileTrajectory::RenderPreview(
    xdraw::draw_list& draw_list, const systems::Local::Snapshot& local) {
  if (!this->m_should_preview_ || !systems::g_frame_data.Valid()) {
    return;
  }

  const auto weapon_services = memory::Read<std::uintptr_t>(
      local.pawn + SCHEMA("C_BasePlayerPawn", "m_pWeaponServices"_hash));
  if (!weapon_services) {
    return;
  }

  const auto weapon_handle = memory::Read<std::uint32_t>(
      weapon_services +
      SCHEMA("CPlayer_WeaponServices", "m_hActiveWeapon"_hash));
  if (!weapon_handle || weapon_handle == 0xffffffff) {
    return;
  }

  const auto weapon = systems::g_entities.Lookup(weapon_handle);
  if (!weapon) {
    return;
  }

  math::Vector3 origin{}, velocity{};
  this->SetupThrow(local.pawn, weapon, origin, velocity);

  if (settings::g_misc.m_projectile_trajectory.straight_throw.value) {
    const auto player_velocity = systems::g_prediction.Pre().networked_velocity;
    if (player_velocity.LengthSqr() >= 1.0f) {
      velocity = velocity - player_velocity * kVelocityInherit;

      const auto dir = velocity.Normalized();
      const auto vel_along =
          dir * (player_velocity * kVelocityInherit).Dot(dir);
      velocity = velocity + vel_along;
    }
  }

  Trajectory frame_traj{};
  this->Simulate(origin, velocity, local.pawn, frame_traj);

  if (frame_traj.valid) {
    this->RenderTrajectory(draw_list, frame_traj, 1.0f, false, local);
  }
}

void ProjectileTrajectory::RenderTrajectory(
    xdraw::draw_list& draw_list, const Trajectory& traj, float alpha,
    bool is_thrown, const systems::Local::Snapshot& local) const {
  if (!traj.valid || traj.points.size() < 2) {
    return;
  }

  const auto& cfg = settings::g_misc.m_projectile_trajectory;
  const auto& points = traj.points;
  const auto count = points.size();

  std::vector<DamageInfo> damages{};
  this->ComputeDamageAtEndpoint(traj, damages, local);

  const auto has_damage = !damages.empty();
  const auto color =
      is_thrown
          ? (has_damage ? cfg.will_deal_damage_thrown_color : cfg.thrown_color)
          : (has_damage ? cfg.will_deal_damage_held_color : cfg.held_color);

  std::vector<float> seg_points{};
  std::vector<xdraw::color> seg_colors{};

  seg_points.reserve(count * 2);
  seg_colors.reserve(count);

  for (auto i = 0ull; i < count; ++i) {
    const auto screen = systems::g_view.Project(points[i]);
    if (!systems::g_view.ProjectionValid(screen)) {
      seg_points.push_back(std::numeric_limits<float>::quiet_NaN());
      seg_points.push_back(std::numeric_limits<float>::quiet_NaN());
      seg_colors.push_back({0, 0, 0, 0});
      continue;
    }

    const auto t = static_cast<float>(i) / static_cast<float>(count - 1);
    const auto fade = alpha * (1.0f - t * 0.6f);
    const auto a =
        static_cast<std::uint8_t>(static_cast<float>(color.value.a) * fade);

    seg_points.push_back(screen.x);
    seg_points.push_back(screen.y);
    seg_colors.push_back({color.value.r, color.value.g, color.value.b, a});
  }

  const auto flush_to = [&](xdraw::draw_list& target,
                            const std::vector<xdraw::color>& colors,
                            float thickness) {
    std::vector<float> run_pts{};
    std::vector<xdraw::color> run_cols{};

    for (auto i = 0ull; i < colors.size(); ++i) {
      const auto px = seg_points[i * 2];
      const auto py = seg_points[i * 2 + 1];

      if (std::isnan(px)) {
        if (run_pts.size() >= 4) {
          target.polyline_gradient(run_pts, run_cols, false, thickness);
        }

        run_pts.clear();
        run_cols.clear();
        continue;
      }

      run_pts.push_back(px);
      run_pts.push_back(py);
      run_cols.push_back(colors[i]);
    }

    if (run_pts.size() >= 4) {
      target.polyline_gradient(run_pts, run_cols, false, thickness);
    }
  };

  if (cfg.glow && alpha > 0.0f) {
    auto& glow = xdraw::get_glow(xdraw::layer::middle);

    std::vector<xdraw::color> glow_colors{};
    glow_colors.reserve(seg_colors.size());

    const auto strength = std::clamp(cfg.glow_strength.value, 0.0f, 1.0f);

    for (const auto& c : seg_colors) {
      const auto ga =
          static_cast<std::uint8_t>(static_cast<float>(c.a) * strength);
      glow_colors.push_back({c.r, c.g, c.b, ga});
    }

    flush_to(glow, glow_colors, 2.5f);
  }

  flush_to(draw_list, seg_colors, 1.5f);

  if (traj.end_tick >= 0) {
    const auto screen = systems::g_view.Project(traj.end_pos);
    if (systems::g_view.ProjectionValid(screen)) {
      const auto det_a =
          static_cast<std::uint8_t>(static_cast<float>(color.value.a) * alpha);
      const auto outline_a = static_cast<std::uint8_t>(200.0f * alpha);

      draw_list.circle_filled(screen.x, screen.y, 3.5f, {0, 0, 0, outline_a},
                              16);
      draw_list.circle_filled(
          screen.x, screen.y, 2.5f,
          {color.value.r, color.value.g, color.value.b, det_a}, 16);
    }
  }
}

}  // namespace features::misc
