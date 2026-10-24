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
import <string_view>;
import <type_traits>;

namespace features::combat {

void Legit::OnCreateMove(systems::Input::Usercmd* cmd) {
  if (!settings::g_combat.m_legitbot.enabled.value) {
    return;
  }

  auto& ctx = g_shared.Ctx();
  if (!ctx.valid) {
    return;
  }

  if (ctx.weapon_type < cstypes::weapon_type::kPistol ||
      ctx.weapon_type > cstypes::weapon_type::kLmg) {
    return;
  }

  const auto& config = settings::g_combat.m_legitbot.GetGroup(ctx.weapon_type);
  const auto view_angles = systems::g_input.GetViewAngles();
  const auto local = systems::g_local.Get();
  const auto aim_punch = g_shared.GetAimPunch(local.pawn);

  this->m_cached_view_angles_ = view_angles;
  this->m_cached_aim_punch_ = aim_punch;

  if (config.standalone_rcs.value) {
    this->UpdateStandaloneRcs(
        view_angles, aim_punch, config.standalone_rcs_strength.value,
        config.standalone_rcs_min.value, config.standalone_rcs_max.value,
        !config.aimbot.value, local);
  }

  this->m_target_ = {};

  if (!config.aimbot.value && !config.triggerbot.value) {
    return;
  }

  if (!g_shared.CanShoot(cmd, local.controller, false)) {
    return;
  }

  // Legit aiming follows the current view, independently of rage's predicted
  // history.
  const auto shoot_position = g_shared.GetEyePosition(local.pawn);
  ctx.spread = g_shared.GetSpread();
  ctx.inaccuracy = g_shared.GetInaccuracy(true);

  auto detection_angles = view_angles;
  if (config.rcs.value && aim_punch.LengthSqr() > 0.0001f) {
    detection_angles.x += aim_punch.x;
    detection_angles.y += aim_punch.y;
    math::helpers::NormalizeAngles(detection_angles);
  }

  if (config.aimbot.value) {
    this->m_target_ =
        this->FindTarget(shoot_position, detection_angles, config, local);
    if (this->m_target_.HasTarget()) {
      this->ApplyAimbot(cmd, this->m_target_, view_angles, aim_punch, config,
                        local);
    }
  }

  const auto can_shoot = g_shared.CanShoot(cmd, local.controller);
  static ULONGLONG next_diagnostic_time{};
  const auto now = GetTickCount64();
  if (now >= next_diagnostic_time) {
    next_diagnostic_time = now + 5000;
    logging::console::Print(
        xs("[legit] aim={} trigger={} target={} can_shoot={} "
           "eye=({:.1f},{:.1f},{:.1f}) fov={}"),
        config.aimbot.value, config.triggerbot.value,
        this->m_target_.HasTarget(), can_shoot, shoot_position.x,
        shoot_position.y, shoot_position.z, config.fov.value);
  }

  if (!can_shoot) {
    return;
  }

  if (config.triggerbot.value) {
    this->ApplyTriggerbot(cmd, shoot_position, view_angles, aim_punch, config,
                          local);
  }
}

void Legit::OnRender(xdraw::draw_list& draw_list) {
  if (!settings::g_combat.m_legitbot.enabled.value) {
    return;
  }

  const auto& ctx = g_shared.Ctx();
  if (!ctx.valid) {
    return;
  }

  if (ctx.weapon_type < cstypes::weapon_type::kPistol ||
      ctx.weapon_type > cstypes::weapon_type::kLmg) {
    return;
  }

  const auto& config = settings::g_combat.m_legitbot.GetGroup(ctx.weapon_type);

  if (config.visualize_fov.value) {
    this->DrawFov(draw_list, this->m_cached_view_angles_,
                  this->m_cached_aim_punch_, config.fov.value, config.fov_color,
                  config.rcs.value);
  }
}

void Legit::InvalidateIfNeeded() {
  const auto local = systems::g_local.Get();
  if (!local.is_alive || !local.pawn || !local.controller) {
    this->m_trigger_release_time_ = 0.0f;
    this->m_trigger_pending_pawn_ = 0;
    this->m_trigger_delay_start_ = 0.0f;
  }
}

Legit::TargetResult Legit::FindTarget(
    const math::Vector3& shoot_position, const math::Vector3& view_angles,
    const settings::Combat::Legitbot::WeaponGroup& config,
    const systems::Local::Snapshot& local) const {
  TargetResult best{};

  for (const auto& p :
       systems::g_entities.GetByType(systems::Entities::Type::kPlayer)) {
    if (!p.ptr || p.ptr == local.controller) {
      continue;
    }

    if (!memory::Read<bool>(
            p.ptr + SCHEMA("CCSPlayerController", "m_bPawnIsAlive"_hash))) {
      continue;
    }

    const auto pawn_handle = memory::Read<std::uint32_t>(
        p.ptr + SCHEMA("CBasePlayerController", "m_hPawn"_hash));
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

    if (memory::Read<bool>(
            pawn + SCHEMA("C_CSPlayerPawn", "m_bGunGameImmunity"_hash))) {
      continue;
    }

    const auto records = g_shared.Lc().GetValidRecords(pawn);
    if (records.empty()) {
      continue;
    }

    const auto record = records.front();
    if (!record || !record->valid) {
      continue;
    }

    const auto point = this->ScanPlayer(pawn, record, shoot_position,
                                        view_angles, config, local);
    if (!point.valid) {
      continue;
    }

    const auto aim =
        math::helpers::CalculateAngle(shoot_position, point.position);
    const auto fov = math::helpers::AngleDistance(view_angles, aim);

    const auto can_kill = point.damage >= static_cast<float>(health);
    const auto best_can_kill =
        best.HasTarget() &&
        best.best_point.damage >= static_cast<float>(best.health);

    auto score{1000.0f};

    if (can_kill && !best_can_kill) {
      score += 10000.0f + point.damage;
    } else if (can_kill == best_can_kill) {
      score += point.damage * 100.0f + (180.0f - fov);
    } else {
      continue;
    }

    if (!best.HasTarget() || score > best.score) {
      best.pawn = pawn;
      best.aim_angle = aim;
      best.hitchance = 1.0f;
      best.score = score;
      best.fov = fov;
      best.health = health;
      best.record = record;
      best.best_point = point;
    }
  }

  return best;
}

Legit::ScanPoint Legit::ScanPlayer(
    std::uintptr_t pawn, Shared::Lagcomp::Record* record,
    const math::Vector3& shoot_position, const math::Vector3& view_angles,
    const settings::Combat::Legitbot::WeaponGroup& config,
    const systems::Local::Snapshot& local) const {
  const auto pen_ctx = g_shared.Pen().PrepareTarget(pawn, record);
  const auto& skeleton = record->bones;

  ScanPoint best{};
  best.fov = 999.0f;
  best.damage = -1.0f;

  for (const auto& hitbox : pen_ctx.hitboxes) {
    const auto cfg_idx =
        HitgroupToCfg(systems::g_hitboxes.HitgroupFromHitbox(hitbox.index));
    if (cfg_idx < 0 || !config.hitboxes.values[cfg_idx]) {
      continue;
    }

    const auto bone_index = hitbox.bone;
    if (bone_index < 0 || bone_index >= record->bone_count) {
      continue;
    }

    const auto& bone = skeleton[bone_index];
    if (bone.position.LengthSqr() < 1.0f) {
      continue;
    }

    const auto center = (hitbox.mins + hitbox.maxs) * 0.5f;
    const auto position =
        bone.position +
        (hitbox.translation_only ? center : bone.rotation.RotateVector(center));
    const auto aim = math::helpers::CalculateAngle(shoot_position, position);
    const auto fov = math::helpers::AngleDistance(view_angles, aim);

    if (fov > config.fov.value) {
      continue;
    }

    Shared::Penetration::Result pen{};
    if (!g_shared.Pen().Run(shoot_position, position, pen_ctx, local.pawn,
                            local.team, pen)) {
      continue;
    }

    const auto visible = !pen.penetrated;
    if (!visible && !config.autowall.value) {
      continue;
    }

    if (!visible && pen.damage < static_cast<float>(config.min_damage.value)) {
      continue;
    }

    const auto is_better = (pen.damage > best.damage) ||
                           (pen.damage == best.damage && fov < best.fov);
    if (is_better) {
      best.position = position;
      best.damage = pen.damage;
      best.fov = fov;
      best.hitgroup = pen.hitgroup;
      best.cfg_index = cfg_idx;
      best.bone_index = static_cast<int>(bone_index);
      best.visible = visible;
      best.is_center = true;
      best.valid = true;
    }
  }

  return best;
}

void Legit::ApplyAimbot(systems::Input::Usercmd* cmd, const TargetResult& tgt,
                        const math::Vector3& view_angles,
                        const math::Vector3& aim_punch,
                        const settings::Combat::Legitbot::WeaponGroup& config,
                        const systems::Local::Snapshot& local) {
  auto aim_angle = tgt.aim_angle;

  if (config.rcs.value) {
    this->ApplyRcs(aim_angle, aim_punch, config.rcs_min.value,
                   config.rcs_max.value);
  }

  if (config.smooth.value > 0) {
    auto delta = aim_angle - view_angles;
    math::helpers::NormalizeAngles(delta);

    const auto delta_length = std::sqrtf(delta.x * delta.x + delta.y * delta.y);
    if (delta_length < 0.001f) {
      this->m_remainder_x_ = 0.0f;
      this->m_remainder_y_ = 0.0f;
      return;
    }

    const auto base_smooth = static_cast<float>(config.smooth.value);
    const auto distance_factor = std::clamp(delta_length / 10.0f, 0.0f, 1.0f);
    const auto ease = 1.0f - std::powf(distance_factor, 2.0f);
    auto smooth_factor = (0.3f + ease * 0.7f) / base_smooth;

    smooth_factor *= random::NormalClamped(1.0f, 0.06f, 0.85f, 1.15f);

    const auto x_bias = random::NormalClamped(1.0f, 0.02f, 0.95f, 1.05f);
    const auto y_bias = random::NormalClamped(0.97f, 0.03f, 0.90f, 1.04f);

    auto move_x = delta.x * smooth_factor * x_bias;
    auto move_y = delta.y * smooth_factor * y_bias;

    if (delta_length < 2.0f && delta_length > 0.3f &&
        random::Floating(0.0f, 1.0f) < 0.15f) {
      const auto overshoot = random::NormalClamped(1.2f, 0.08f, 1.05f, 1.4f);
      move_x *= overshoot;
      move_y *= overshoot;
    }

    aim_angle = view_angles + math::Vector3{move_x, move_y, 0.0f};
    math::helpers::NormalizeAngles(aim_angle);
  }

  auto want_x = aim_angle.x - view_angles.x;
  auto want_y = aim_angle.y - view_angles.y;

  want_x += this->m_remainder_x_;
  want_y += this->m_remainder_y_;

  const auto sensitivity = CONVAR("sensitivity")->Get<float>();
  const auto fov_adjust = memory::Read<float>(
      local.pawn + SCHEMA("C_BasePlayerPawn", "m_flFOVSensitivityAdjust"_hash));
  const auto deg_per_count = sensitivity * 0.022f * fov_adjust;

  const auto counts_x = std::roundf(want_x / deg_per_count);
  const auto counts_y = std::roundf(want_y / deg_per_count);

  this->m_remainder_x_ = want_x - counts_x * deg_per_count;
  this->m_remainder_y_ = want_y - counts_y * deg_per_count;

  systems::g_legit_input.AddMouseDelta(counts_x * deg_per_count,
                                       counts_y * deg_per_count);
}

void Legit::ApplyTriggerbot(
    systems::Input::Usercmd* cmd, const math::Vector3& shoot_position,
    const math::Vector3& view_angles, const math::Vector3& aim_punch,
    const settings::Combat::Legitbot::WeaponGroup& config,
    const systems::Local::Snapshot& local) {
  const auto& ctx = g_shared.Ctx();
  const auto seed_mode = config.give_me_your_seed.value;

  if (this->m_trigger_release_time_ > 0.0f) {
    if (ctx.current_time >= this->m_trigger_release_time_) {
      this->m_trigger_release_time_ = 0.0f;
      this->m_trigger_pending_pawn_ = 0;
      return;
    }

    cmd->buttons.value |= cstypes::command_buttons::kInAttack;
    cmd->buttons.value_changed |= cstypes::command_buttons::kInAttack;
    return;
  }

  auto corrected_angles = view_angles;
  if (config.rcs.value && aim_punch.LengthSqr() > 0.0001f) {
    corrected_angles.x += aim_punch.x;
    corrected_angles.y += aim_punch.y;
    math::helpers::NormalizeAngles(corrected_angles);
  }

  math::Vector3 bullet_dir{};
  std::uint32_t seed{};
  math::Vector2 spread{};

  if (seed_mode) {
    const auto tick_base = memory::Read<std::int32_t>(
        local.controller + SCHEMA("CBasePlayerController", "m_nTickBase"_hash));
    seed = g_shared.GetSpreadSeed(corrected_angles, tick_base);
    spread = g_shared.CalculateSpread(seed, ctx.inaccuracy, ctx.spread,
                                      ctx.recoil_index, ctx.item_def_idx,
                                      ctx.num_bullets);

    math::Vector3 forward{}, left{}, up{};
    math::helpers::AngleVectorsLeft(corrected_angles, &forward, &left, &up);

    bullet_dir = (forward + left * spread.x + up * spread.y).Normalized();
  }

  auto hit_pawn{0ull};
  Shared::Penetration::Result pen{};
  Shared::Lagcomp::Record* hit_record{nullptr};
  auto found{false};

  const auto gather_records =
      [&](std::uintptr_t pawn) -> std::array<Shared::Lagcomp::Record*, 3> {
    std::array<Shared::Lagcomp::Record*, 3> out{nullptr, nullptr, nullptr};

    const auto records = g_shared.Lc().GetValidRecords(pawn);
    if (records.empty() || !records.front() || !records.front()->valid) {
      return out;
    }

    out[0] = records.front();

    if (records.size() > 2) {
      const auto mid = records.size() / 2;

      if (records[mid] && records[mid]->valid) {
        out[1] = records[mid];
      }

      if (records.back() && records.back()->valid) {
        out[2] = records.back();
      }
    } else if (records.size() > 1) {
      if (records.back() && records.back()->valid) {
        out[1] = records.back();
      }
    }

    return out;
  };

  for (const auto& p :
       systems::g_entities.GetByType(systems::Entities::Type::kPlayer)) {
    if (!p.ptr || p.ptr == local.controller) {
      continue;
    }

    if (!memory::Read<bool>(
            p.ptr + SCHEMA("CCSPlayerController", "m_bPawnIsAlive"_hash))) {
      continue;
    }

    const auto pawn_handle = memory::Read<std::uint32_t>(
        p.ptr + SCHEMA("CBasePlayerController", "m_hPawn"_hash));
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

    if (memory::Read<bool>(
            pawn + SCHEMA("C_CSPlayerPawn", "m_bGunGameImmunity"_hash))) {
      continue;
    }

    const auto recs = gather_records(pawn);
    if (!recs[0]) {
      continue;
    }

    const auto game_scene_node = memory::Read<std::uintptr_t>(
        pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
    const auto hitbox_set = systems::g_hitboxes.Query(game_scene_node);

    for (const auto rec : recs) {
      if (!rec) {
        continue;
      }

      const auto& skeleton = rec->bones;

      if (seed_mode) {
        for (const auto& hb : hitbox_set) {
          if (hb.bone < 0 || hb.bone >= rec->bone_count) {
            continue;
          }

          const auto& bone = skeleton[hb.bone];
          if (bone.position.LengthSqr() < 1.0f) {
            continue;
          }

          const auto capsule_start =
              bone.rotation.RotateVector(hb.mins) + bone.position;
          const auto capsule_end =
              bone.rotation.RotateVector(hb.maxs) + bone.position;
          const auto radius = hb.radius > 0.0f ? hb.radius * 0.9f : 1.8f;

          auto fraction{1.0f};
          if (!g_shared.RayVsCapsule(shoot_position, bullet_dir * ctx.range,
                                     capsule_start, capsule_end, radius,
                                     fraction)) {
            continue;
          }

          const auto hitgroup =
              systems::g_hitboxes.HitgroupFromHitbox(hb.index);

          if (config.trigger_head_only.value && hitgroup != 1) {
            continue;
          }

          const auto hit_point =
              shoot_position + bullet_dir * ctx.range * fraction;
          const auto pen_ctx = g_shared.Pen().PrepareTarget(pawn, rec);

          Shared::Penetration::Result candidate_pen{};
          if (!g_shared.Pen().Run(shoot_position, hit_point, pen_ctx,
                                  local.pawn, local.team, candidate_pen)) {
            continue;
          }

          if (candidate_pen.penetrated &&
              candidate_pen.damage <
                  static_cast<float>(config.min_damage.value)) {
            continue;
          }

          if (!found || candidate_pen.damage > pen.damage) {
            hit_pawn = pawn;
            hit_record = rec;
            pen = candidate_pen;
            found = true;
          }

          break;
        }
      } else {
        const systems::Hitboxes::Entry* closest_hb{nullptr};
        auto closest_fov{FLT_MAX};

        for (const auto& entry : hitbox_set) {
          if (entry.bone < 0 || entry.bone >= rec->bone_count) {
            continue;
          }

          const auto& bone = skeleton[entry.bone];
          if (bone.position.LengthSqr() < 1.0f) {
            continue;
          }

          const auto center =
              bone.rotation.RotateVector((entry.mins + entry.maxs) * 0.5f) +
              bone.position;
          const auto aim =
              math::helpers::CalculateAngle(shoot_position, center);
          const auto fov = math::helpers::AngleDistance(corrected_angles, aim);

          if (fov > 2.0f) {
            continue;
          }

          if (fov < closest_fov) {
            closest_fov = fov;
            closest_hb = &entry;
          }
        }

        if (!closest_hb) {
          continue;
        }

        const auto& bone = skeleton[closest_hb->bone];
        const auto target_point =
            bone.rotation.RotateVector((closest_hb->mins + closest_hb->maxs) *
                                       0.5f) +
            bone.position;
        const auto pen_ctx = g_shared.Pen().PrepareTarget(pawn, rec);

        Shared::Penetration::Result candidate_pen{};
        if (!g_shared.Pen().Run(shoot_position, target_point, pen_ctx,
                                local.pawn, local.team, candidate_pen)) {
          continue;
        }

        const auto visible = !candidate_pen.penetrated;
        if (!visible) {
          if (!config.autowall.value) {
            continue;
          }

          if (candidate_pen.damage <
              static_cast<float>(config.min_damage.value)) {
            continue;
          }
        }

        if (!found || candidate_pen.damage > pen.damage) {
          hit_pawn = pawn;
          hit_record = rec;
          pen = candidate_pen;
          found = true;
        }
      }
    }
  }

  if (!found) {
    this->m_trigger_pending_pawn_ = 0;
    return;
  }

  if (config.trigger_head_only.value && pen.hitgroup != 1) {
    this->m_trigger_pending_pawn_ = 0;
    return;
  }

  if (pen.penetrated &&
      pen.damage < static_cast<float>(config.min_damage.value)) {
    this->m_trigger_pending_pawn_ = 0;
    return;
  }

  if (!seed_mode) {
    const auto& skeleton = hit_record->bones;
    const auto game_scene_node = memory::Read<std::uintptr_t>(
        hit_pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
    const auto hitbox_set = systems::g_hitboxes.Query(game_scene_node);

    const systems::Hitboxes::Entry* best_hb{nullptr};

    for (const auto& entry : hitbox_set) {
      if (entry.index == pen.hitbox) {
        best_hb = &entry;
        break;
      }
    }

    if (best_hb && best_hb->bone >= 0 &&
        best_hb->bone < hit_record->bone_count) {
      const auto hc = g_shared.CalculateHitchance(
          shoot_position, corrected_angles, *best_hb, skeleton[best_hb->bone],
          ctx.inaccuracy, ctx.spread);

      const auto min_hc =
          static_cast<float>(config.trigger_hitchance.value) / 100.0f;
      if (hc < min_hc && !g_shared.IsMaxAccuracy(ctx.inaccuracy)) {
        this->m_trigger_pending_pawn_ = 0;
        return;
      }
    }

    const auto delay_ms = static_cast<float>(config.trigger_delay.value);

    if (this->m_trigger_pending_pawn_ != hit_pawn) {
      this->m_trigger_pending_pawn_ = hit_pawn;
      this->m_trigger_delay_start_ = ctx.current_time;
    }

    const auto elapsed_ms =
        (ctx.current_time - this->m_trigger_delay_start_) * 1000.0f;
    if (elapsed_ms < delay_ms) {
      return;
    }
  }

  g_shared.LastShootTick() = memory::Read<std::int32_t>(
      local.controller + SCHEMA("CBasePlayerController", "m_nTickBase"_hash));

  const auto record_time = cstypes::TickFraction::FromValue(
      hit_record->simulation_time / cstypes::kTickInterval);
  const auto input_history_size = cmd->csgo_user_cmd.InputHistorySize();
  const auto history_angles =
      seed_mode ? corrected_angles
                : math::Vector3{view_angles.x - aim_punch.x,
                                view_angles.y - aim_punch.y, 0.0f};

  for (auto i = 0; i < input_history_size; ++i) {
    const auto entry = cmd->csgo_user_cmd.MutableInputHistory(i);
    if (!entry) {
      continue;
    }

    if (const auto angles = entry->MutableViewAngles()) {
      angles->SetX(history_angles.x);
      angles->SetY(history_angles.y);
    }

    entry->SetRenderTickCount(record_time.tick + 1);
    entry->SetRenderTickFraction(0.0f);

    if (entry->HasSvInterp0()) {
      const auto interp = entry->MutableSvInterp0();
      interp->SetSrcTick(-1);
      interp->SetDstTick(-1);
      interp->SetFrac(0.0f);
    }

    if (entry->HasSvInterp1()) {
      const auto interp = entry->MutableSvInterp1();
      interp->SetSrcTick(-1);
      interp->SetDstTick(-1);
      interp->SetFrac(0.0f);
    }

    if (entry->HasClInterp()) {
      const auto interp = entry->MutableClInterp();
      interp->SetFrac(0.0f);
    }
  }

  cmd->buttons.value |= cstypes::command_buttons::kInAttack;
  cmd->buttons.value_changed |= cstypes::command_buttons::kInAttack;

  if (input_history_size > 0) {
    cmd->csgo_user_cmd.SetAttack1StartHistoryIndex(input_history_size - 1);
  }

  this->m_trigger_release_time_ = ctx.current_time + random::HoldDuration();
}

void Legit::ApplyRcs(math::Vector3& aim_angle, const math::Vector3& aim_punch,
                     int rand_min, int rand_max) const {
  if (aim_punch.LengthSqr() < 0.0001f) {
    return;
  }

  const auto factor = this->ComputeRcsFactor(rand_min, rand_max);

  aim_angle.x -= aim_punch.x * factor;
  aim_angle.y -= aim_punch.y * factor;
  math::helpers::NormalizeAngles(aim_angle);
}

void Legit::UpdateStandaloneRcs(const math::Vector3& view_angles,
                                const math::Vector3& aim_punch, int amount,
                                int rand_min, int rand_max, bool apply,
                                const systems::Local::Snapshot& local) {
  const auto shots_fired = memory::Read<int>(
      local.pawn + SCHEMA("C_CSPlayerPawn", "m_iShotsFired"_hash));
  if (shots_fired > 1) {
    const auto factor = this->ComputeRcsFactor(rand_min, rand_max);
    const auto scale = static_cast<float>(amount) / 100.0f;

    const auto punch_scaled = math::Vector3{aim_punch.x * scale * factor,
                                            aim_punch.y * scale * factor, 0.0f};

    if (apply) {
      auto new_angles = view_angles;
      new_angles.x += this->m_old_punch_.x - punch_scaled.x;
      new_angles.y += this->m_old_punch_.y - punch_scaled.y;
      math::helpers::NormalizeAngles(new_angles);

      systems::g_input.SetViewAngles(new_angles);
    }

    this->m_old_punch_ = punch_scaled;
  } else {
    this->m_old_punch_ = {};
  }
}

float Legit::ComputeRcsFactor(int rand_min, int rand_max) const {
  const auto seed =
      static_cast<std::uint32_t>(g_shared.Ctx().current_time * 1000.0f);
  const auto t = static_cast<float>(seed % 1000) / 1000.0f;
  const auto min_scale = static_cast<float>(rand_min) / 100.0f;
  const auto max_scale = static_cast<float>(rand_max) / 100.0f;
  return min_scale + (max_scale - min_scale) * t;
}

void Legit::DrawFov(xdraw::draw_list& draw_list,
                    const math::Vector3& view_angles,
                    const math::Vector3& aim_punch, float fov_degrees,
                    const config::col& color, bool rcs_active) const {
  const auto [screen_w, screen_h] = xdraw::viewport_size();
  const auto sw = static_cast<float>(screen_w);
  const auto sh = static_cast<float>(screen_h);

  const auto camera_fov_rad = math::helpers::DegToRad(systems::g_view.Fov());
  const auto aimbot_fov_rad = math::helpers::DegToRad(fov_degrees);
  const auto radius = std::tanf(aimbot_fov_rad) /
                      std::tanf(camera_fov_rad * 0.5f) * (sw * 0.5f);

  auto offset_x{0.0f};
  auto offset_y{0.0f};

  if (rcs_active) {
    const auto punch_magnitude = aim_punch.LengthSqr();
    const auto current_time = g_shared.Ctx().current_time;

    if (punch_magnitude > 0.5f) {
      this->m_last_significant_punch_time_ = current_time;
    }

    const auto time_since = current_time - this->m_last_significant_punch_time_;
    if (time_since < 0.3f && punch_magnitude > 0.01f) {
      auto corrected = view_angles;
      corrected.x -= aim_punch.x;
      corrected.y -= aim_punch.y;
      math::helpers::NormalizeAngles(corrected);

      math::Vector3 center_dir{}, corrected_dir{};
      math::helpers::AngleVectorsLeft(view_angles, &center_dir);
      math::helpers::AngleVectorsLeft(corrected, &corrected_dir);

      const auto render_origin = systems::g_frame_data.Origin();
      const auto cs =
          systems::g_view.Project(render_origin + center_dir * 1000.0f);
      const auto ns =
          systems::g_view.Project(render_origin + corrected_dir * 1000.0f);

      if (systems::g_view.ProjectionValid(cs) &&
          systems::g_view.ProjectionValid(ns)) {
        offset_x = (cs.x - ns.x) * 0.5f;
        offset_y = (cs.y - ns.y) * 0.5f;
      }
    }
  }

  const auto cx = sw * 0.5f + offset_x;
  const auto cy = sh * 0.5f + offset_y;
  const auto& c = color.value;

  draw_list.circle(cx, cy, radius, xdraw::color{c.r, c.g, c.b, c.a}, 1.5f, 64);
}

int Legit::HitgroupToCfg(int hitgroup) {
  static constexpr int kTable[]{-1, 0, 1, 2, 3, 3, 4, 4};

  if (hitgroup < 0 || hitgroup >= static_cast<int>(std::size(kTable))) {
    return -1;
  }

  return kTable[hitgroup];
}

}  // namespace features::combat
