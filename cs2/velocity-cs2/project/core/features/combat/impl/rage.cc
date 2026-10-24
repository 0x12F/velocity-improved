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

void Rage::OnCreateMove(systems::Input::Usercmd* cmd) {
  auto& ctx = g_shared.Ctx();
  const auto local = systems::g_local.Get();
  this->UpdatePenetrationCrosshair(local);

  this->m_should_stop_ = false;
  this->m_firing_this_tick_ = false;

  if (!ctx.valid) {
    this->m_revolver_cock_ticks_ = 0;
    return;
  }

  if (!settings::g_combat.m_duckpeek.enabled.value) {
    this->m_release_duck_for_shot_ = false;
    this->m_duckpeek_reduck_ = false;
  }

  if (this->m_zeus_fired_) {
    this->m_zeus_fired_ = false;

    if (settings::g_combat.m_zeusbot.drop_after &&
        !systems::g_local.IsInDeathmatch()) {
      memory::Call<void>(PATTERN(patterns::engine_client_cmd),
                         addresses::globals::source2engine_to_client, 0, "drop",
                         0x7ffef001);
    }

    return;
  }

  const auto is_knife = ctx.weapon_type == cstypes::weapon_type::kNife;
  const auto is_taser = ctx.weapon_type == cstypes::weapon_type::kTaser;

  if (!is_knife && !is_taser &&
      (ctx.weapon_type < cstypes::weapon_type::kPistol ||
       ctx.weapon_type > cstypes::weapon_type::kLmg)) {
    return;
  }

  const auto enabled = is_knife   ? settings::g_combat.m_knifebot.enabled
                       : is_taser ? settings::g_combat.m_zeusbot.enabled
                                  : settings::g_combat.m_ragebot.enabled;
  if (!enabled) {
    this->m_revolver_cock_ticks_ = 0;
    return;
  }

  auto aim_ctx = this->BuildContext(local);

  if (is_knife) {
    if (!g_shared.CanShoot(cmd, local.controller)) {
      return;
    }

    this->RunKnife(cmd, aim_ctx, local);
  } else if (is_taser) {
    if (!g_shared.CanShoot(cmd, local.controller)) {
      return;
    }

    this->RunTaser(cmd, aim_ctx, local);
  } else if (ctx.item_def_idx ==
             cstypes::item_definition_index::kWeaponR8Revolver) {
    this->AutoRevolver(cmd, aim_ctx, local);
  } else {
    this->m_revolver_cock_ticks_ = 0;

    if (!g_shared.CanShoot(cmd, local.controller, false)) {
      return;
    }

    // Keep planning the stop while the weapon recovers between shots.
    this->RunGun(cmd, aim_ctx, local,
                 g_shared.CanShoot(cmd, local.controller));
  }
}

void Rage::OnRender(xdraw::draw_list& draw_list) {
  this->DrawPenetrationCrosshair(draw_list);

  const auto& config =
      settings::g_combat.m_ragebot.GetGroup(g_shared.Ctx().weapon_type);
  if (!config.debug_multipoints.value) {
    return;
  }

  std::lock_guard lock(m_debug_mtx_);

  for (const auto& pt : m_debug_points_) {
    const auto screen = systems::g_view.Project(pt.position);
    if (!systems::g_view.ProjectionValid(screen)) {
      continue;
    }

    xdraw::color col{};
    switch (pt.hitbox_index) {
      case 0:
        col = {255, 80, 80};
        break;  // head — red
      case 2:
      case 3:
        col = {220, 220, 60};
        break;  // stomach — yellow
      case 4:
      case 5:
      case 6:
        col = {255, 160, 60};
        break;  // chest — orange
      case 7:
      case 8:
      case 9:
      case 10:
      case 11:
      case 12:
        col = {80, 160, 255};
        break;  // legs — blue
      case 13:
      case 14:
      case 15:
      case 16:
      case 17:
      case 18:
        col = {180, 80, 255};
        break;  // arms — purple
      default:
        col = {200, 200, 200};
        break;
    }

    const auto alpha = pt.is_center ? std::uint8_t{255} : std::uint8_t{160};
    const auto radius = pt.is_center ? 3.5f : 2.0f;

    draw_list.circle_filled(screen.x, screen.y, radius, col.alpha(alpha));
  }
}

Rage::AimContext Rage::BuildContext(
    const systems::Local::Snapshot& local) const {
  auto& ctx = g_shared.Ctx();
  const auto& prestate = systems::g_prediction.Pre();

  g_shared.Sh().Snapshot(local.pawn, ctx.weapon_services);

  AimContext out{};
  out.velocity = prestate.velocity;
  out.spread = g_shared.GetSpread();
  out.predicted_inaccuracy = g_shared.GetInaccuracy(true);

  ctx.spread = out.spread;
  ctx.inaccuracy = out.predicted_inaccuracy;

  out.view_angles = systems::g_input.GetViewAngles();
  out.on_ground = (prestate.flags & cstypes::entity_flags::kOnGround) != 0;
  out.is_scoped = ctx.is_scoped;
  out.weapon_max_speed = ctx.weapon_max_speed;
  out.accurate_threshold = ctx.weapon_max_speed * 0.34f;

  return out;
}

bool Rage::ProcessDoubletap(systems::Input::Usercmd* cmd,
                            const systems::Local::Snapshot& local,
                            bool charge_dt) {
  const auto& shared_ctx = g_shared.Ctx();
  const auto& config =
      settings::g_combat.m_ragebot.GetGroup(shared_ctx.weapon_type);
  if (!config.doubletap.value) {
    return false;
  }

  const auto base_cmd = cmd->csgo_user_cmd.MutableBase();
  if (!base_cmd) {
    return false;
  }

  const auto should_attack =
      charge_dt && g_shared.CanShoot(cmd, local.controller);

  if (should_attack) {
    if (const auto subtick_moves = base_cmd->MutableSubtickMoves()) {
      const auto old_size = subtick_moves->m_current_size;
      const auto press = systems::g_input.AcquireSubtickStep(subtick_moves);
      const auto release = systems::g_input.AcquireSubtickStep(subtick_moves);
      if (press && release) {
        press->SetButton(cstypes::command_buttons::kInAttack);
        press->SetPressed(true);
        press->SetWhen(0.0f);
        press->SetAnalogForwardDelta(0.0f);
        press->SetAnalogLeftDelta(0.0f);

        release->SetButton(cstypes::command_buttons::kInAttack);
        release->SetPressed(false);
        release->SetWhen(std::nextafter(1.0f, 0.0f));
        release->SetAnalogForwardDelta(0.0f);
        release->SetAnalogLeftDelta(0.0f);

        // The subtick release is the final state. Do not let the
        // base command turn this pulse into a held attack.
        cmd->buttons.value &= ~cstypes::command_buttons::kInAttack;
        cmd->buttons.value_changed |= cstypes::command_buttons::kInAttack;
        cmd->buttons.value_scroll &= ~cstypes::command_buttons::kInAttack;
        cmd->csgo_user_cmd.SetAttack1StartHistoryIndex(-1);
        return true;
      }

      // Do not leave a partial press in the command if allocation of
      // the matching release failed.
      subtick_moves->m_current_size = old_size;
    }
  }

  cmd->buttons.value &= ~cstypes::command_buttons::kInAttack;
  cmd->buttons.value_changed |= cstypes::command_buttons::kInAttack;
  cmd->buttons.value_scroll &= ~cstypes::command_buttons::kInAttack;
  cmd->csgo_user_cmd.SetAttack1StartHistoryIndex(-1);
  return false;
}

std::optional<Rage::StopPrediction> Rage::PredictStop(
    const AimContext& ctx, const math::Vector3& current_eye,
    const systems::Local::Snapshot& local) const {
  const auto& shared_ctx = g_shared.Ctx();
  const auto& prestate = systems::g_prediction.Pre();
  auto predicted_velocity = ctx.velocity;
  const auto speed = predicted_velocity.Length2d();
  const auto will_stop = ctx.on_ground && (speed > ctx.accurate_threshold ||
                                           (ctx.is_scoped && speed > 1.0f));

  if (!will_stop) {
    return std::nullopt;
  }

  auto sim_vel = predicted_velocity;
  sim_vel.z = 0.0f;
  predicted_velocity.z = 0.0f;

  const auto sv_friction = CONVAR("sv_friction")->Get<float>();
  const auto sv_stopspeed = CONVAR("sv_stopspeed")->Get<float>();
  const auto sv_accelerate = CONVAR("sv_accelerate")->Get<float>();
  const auto surface_friction = prestate.surface_friction;

  const auto movement_services = memory::Read<std::uintptr_t>(
      local.pawn + SCHEMA("C_BasePlayerPawn", "m_pMovementServices"_hash));
  const auto max_move_speed =
      movement_services ? memory::Read<float>(movement_services +
                                              SCHEMA("CPlayer_MovementServices",
                                                     "m_flMaxspeed"_hash))
                        : 250.0f;

  for (auto i = 0; i < 15; ++i) {
    const auto sim_speed = sim_vel.Length2d();
    if (sim_speed < 1.0f) {
      break;
    }

    const auto control = std::fmaxf(sim_speed, sv_stopspeed);
    const auto drop =
        sv_friction * surface_friction * control * cstypes::kTickInterval;
    auto new_speed = std::fmaxf(sim_speed - drop, 0.0f);
    auto accel = sv_accelerate;

    if (shared_ctx.is_scoped) {
      const auto weapon_ratio =
          std::fminf(1.0f, shared_ctx.weapon_max_speed / 250.0f);
      const auto scoped_max =
          std::fmaxf(250.0f, max_move_speed) * weapon_ratio * 0.52f;

      if (new_speed > scoped_max - 5.0f) {
        const auto t =
            1.0f - std::fmaxf(0.0f, new_speed - (scoped_max - 5.0f)) /
                       std::fmaxf(0.01f, 5.0f);
        accel *= std::clamp(t, 0.0f, 1.0f);
      }
    }

    const auto accel_speed =
        std::fminf(accel * shared_ctx.weapon_max_speed * surface_friction *
                       cstypes::kTickInterval,
                   new_speed);
    new_speed = std::fmaxf(new_speed - accel_speed, 0.0f);

    if (new_speed > 0.0f) {
      sim_vel *= (new_speed / sim_speed);
    } else {
      sim_vel = {};
      break;
    }
  }

  const auto avg_vel = (predicted_velocity + sim_vel) * 0.5f;
  const auto stop_ticks = g_shared.CalculateStopTicks(
      predicted_velocity, shared_ctx.weapon_max_speed, local.pawn);
  const auto stop_time =
      static_cast<float>(stop_ticks) * cstypes::kTickInterval;

  return StopPrediction{
      .eye = {current_eye.x + avg_vel.x * stop_time,
              current_eye.y + avg_vel.y * stop_time, current_eye.z},
      .inaccuracy = g_shared.GetInaccuracyAtVelocity(local.pawn, sim_vel)};
}

std::vector<Rage::Candidate> Rage::GatherCandidates(
    const systems::Local::Snapshot& local, float max_distance_sq) const {
  const auto& shared_ctx = g_shared.Ctx();
  const auto players =
      systems::g_entities.GetByType(systems::Entities::Type::kPlayer);

  std::vector<Candidate> out;
  out.reserve(players.size());

  const_cast<Rage*>(this)->m_extrapolated_records_.clear();
  const_cast<Rage*>(this)->m_extrapolated_records_.reserve(players.size());

  for (const auto& p : players) {
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

    const auto team =
        memory::Read<int>(pawn + SCHEMA("C_BaseEntity", "m_iTeamNum"_hash));
    if (!local.IsThisOtherTeam(team)) {
      continue;
    }

    const auto health =
        memory::Read<int>(pawn + SCHEMA("C_BaseEntity", "m_iHealth"_hash));
    if (health <= 0) {
      continue;
    }

    if (memory::Read<bool>(
            pawn + SCHEMA("C_CSPlayerPawn", "m_bGunGameImmunity"_hash))) {
      continue;
    }

    auto records = g_shared.Lc().GetValidRecords(pawn);
    auto extrap = g_shared.Lc().Extrapolate(pawn);

    if (extrap) {
      const_cast<Rage*>(this)->m_extrapolated_records_.push_back(
          std::move(*extrap));
      records.push_back(
          &const_cast<Rage*>(this)->m_extrapolated_records_.back());
    }

    if (records.empty()) {
      continue;
    }

    if (max_distance_sq > 0.0f) {
      const auto& origin = systems::g_prediction.Pre().origin;
      const auto delta_front = records.front()->origin - origin;
      auto closest_sq = delta_front.x * delta_front.x +
                        delta_front.y * delta_front.y +
                        delta_front.z * delta_front.z;

      if (records.size() > 1) {
        const auto delta_back = records.back()->origin - origin;
        const auto back_sq = delta_back.x * delta_back.x +
                             delta_back.y * delta_back.y +
                             delta_back.z * delta_back.z;
        closest_sq = std::min(closest_sq, back_sq);
      }

      if (closest_sq > max_distance_sq) {
        continue;
      }
    }

    Candidate c{};
    c.pawn = pawn;
    c.health = health;
    c.armor =
        memory::Read<int>(pawn + SCHEMA("C_CSPlayerPawn", "m_ArmorValue"_hash));

    const auto pick_record_indices =
        [&records](std::array<int, kMaxScanRecords>& out_indices) -> int {
      const auto count = records.size();
      if (count == 0) {
        return 0;
      }

      auto picked{0};
      const auto add_index = [&](int idx) {
        if (picked >= kMaxScanRecords) {
          return;
        }

        for (auto i = 0; i < picked; ++i) {
          if (out_indices[i] == idx) {
            return;
          }
        }

        out_indices[picked++] = idx;
      };

      add_index(0);

      if (count > 2) {
        add_index(static_cast<int>(count / 2));
      }

      if (count > 1) {
        add_index(static_cast<int>(count - 1));
      }

      return picked;
    };

    std::array<int, kMaxScanRecords> record_indices{};
    const auto picked_count = pick_record_indices(record_indices);

    for (auto i = 0; i < picked_count; ++i) {
      c.records[i] = records[static_cast<std::size_t>(record_indices[i])];
    }

    c.record_count = picked_count;

    if (shared_ctx.weapon_type >= cstypes::weapon_type::kPistol &&
        shared_ctx.weapon_type <= cstypes::weapon_type::kLmg) {
      const auto& config =
          settings::g_combat.m_ragebot.GetGroup(shared_ctx.weapon_type);
      c.min_damage =
          this->GetMinDamage(config, health, config.min_damage_override.value);
    }

    out.push_back(c);
  }

  return out;
}

void Rage::RunGun(systems::Input::Usercmd* cmd, const AimContext& ctx,
                  const systems::Local::Snapshot& local, bool allow_fire) {
  if (!settings::g_combat.m_ragebot.enabled) {
    return;
  }

  auto& shared_ctx = g_shared.Ctx();
  const auto& config =
      settings::g_combat.m_ragebot.GetGroup(shared_ctx.weapon_type);

  const auto finish_doubletap = [&](bool charge_dt) {
    if (config.doubletap.value &&
        shared_ctx.item_def_idx !=
            cstypes::item_definition_index::kWeaponR8Revolver) {
      return this->ProcessDoubletap(cmd, local, charge_dt);
    }

    return false;
  };

  auto candidates = this->GatherCandidates(local);

  {
    std::lock_guard lock(m_debug_mtx_);
    m_debug_points_.clear();
  }

  if (candidates.empty()) {
    finish_doubletap(false);
    return;
  }

  auto eye_candidates = g_shared.Sh().GetCandidates();
  if (eye_candidates.count == 0) {
    auto shoot_position = g_shared.GetShootPosition();
    if (shoot_position.LengthSqr() < 1.0f) {
      shoot_position = g_shared.GetEyePosition(local.pawn);
    }

    eye_candidates.entries[0].position = shoot_position;
    eye_candidates.entries[0].is_uninterpolated = true;
    eye_candidates.count = 1;
  }

  const auto scan_from_eye_candidates = [&](const math::Vector3& eye_offset,
                                            float inaccuracy) {
    std::vector<ScanHit> hits_out;

    for (auto i = 0; i < eye_candidates.count; ++i) {
      const auto eye = eye_candidates.entries[i].position + eye_offset;
      auto hits = this->ScanPlayers(eye, inaccuracy, ctx, candidates, local);
      for (auto& hit : hits) {
        auto source_eye = eye_candidates.entries[i];
        source_eye.position = eye;
        hit.source_eye = source_eye;
        hits_out.push_back(std::move(hit));
      }
    }

    return hits_out;
  };

  if (config.no_spread.value) {
    shared_ctx.inaccuracy = g_shared.GetInaccuracy(false);
    auto all_hits = scan_from_eye_candidates({}, shared_ctx.inaccuracy);

    if (all_hits.empty()) {
      finish_doubletap(false);
      return;
    }

    const auto best = this->SelectBest(ctx, all_hits, shared_ctx.inaccuracy);
    if (!best.valid) {
      finish_doubletap(false);
      return;
    }

    if (!allow_fire) {
      return;
    }

    const auto subtick_attack = finish_doubletap(true);
    this->FireGun(cmd, best, false, best.hit.source_eye.position, local,
                  subtick_attack);
    return;
  }

  const auto primary_eye = eye_candidates.entries[0].position;
  const auto& prestate = systems::g_prediction.Pre();

  const auto needed_hc =
      config.hitchance_override.value
          ? static_cast<float>(config.hitchance_override_value) / 100.0f
          : static_cast<float>(config.hitchance) / 100.0f;
  const auto duckpeek_active =
      settings::g_combat.m_duckpeek.enabled.value && ctx.on_ground;
  const auto is_ducked =
      (prestate.flags & cstypes::entity_flags::kDucking) != 0;

  const auto standing_inaccuracy = duckpeek_active
                                       ? this->GetStandingInaccuracy(local, ctx)
                                       : ctx.predicted_inaccuracy;
  // select and gate the shot using the same accuracy state. Duckpeek fires
  // after standing up, so crouched accuracy must not determine its target
  auto current_hits = scan_from_eye_candidates({}, standing_inaccuracy);
  const auto best = this->SelectBest(ctx, current_hits, standing_inaccuracy);
  const auto standing_hc = best.valid ? best.hitchance : 0.0f;

  const auto accurate = best.valid && standing_hc >= needed_hc;
  const auto max_acc = g_shared.IsMaxAccuracy(standing_inaccuracy);
  const auto force = best.valid && standing_hc > 0.0f &&
                     (ctx.on_ground ? (config.force_shot.value && max_acc)
                                    : (config.force_shot_air.value && max_acc));
  const auto shot_viable = accurate || force;

  // log rejection stages even when no shot is fired, without per point i/o
  static auto last_report_time = ULONGLONG{};
  const auto report_time = GetTickCount64();
  if (report_time - last_report_time >= 1000) {
    last_report_time = report_time;
    HeadScanStats stats{};
    for (const auto& candidate : candidates) {
      const auto& head = candidate.head_scan;
      stats.missing += head.missing;
      stats.invalid_bones += head.invalid_bones;
      stats.damage_pruned += head.damage_pruned;
      stats.points += head.points;
      stats.traces += head.traces;
      stats.hits += head.hits;
      stats.wrong_hitbox += head.wrong_hitbox;
      stats.below_damage += head.below_damage;
      stats.max_damage = std::max(stats.max_damage, head.max_damage);
    }
    logging::console::Print(
        "[rage scan] weapon={} head={} body_aim={} min_damage={} "
        "head_multiplier={:.2f} missing={} invalid_bones={} damage_pruned={} "
        "points={} traces={} hits={} wrong_hitbox={} below_damage={} "
        "max_damage={:.0f} head_hc={:.0f}% required_hc={:.0f}% selected={} "
        "selected_hc={:.0f}% max_accuracy={} forced={}",
        shared_ctx.item_def_idx, config.hitboxes.values[0],
        config.body_aim.value,
        config.min_damage_override.value
            ? config.min_damage_override_value.value
            : config.min_damage.value,
        g_shared.Pen().GetWeaponData().headshot_multiplier, stats.missing,
        stats.invalid_bones, stats.damage_pruned, stats.points, stats.traces,
        stats.hits, stats.wrong_hitbox, stats.below_damage, stats.max_damage,
        best.head_hitchance * 100.0f, needed_hc * 100.0f,
        best.valid ? best.hit.hitbox_index : -1, best.hitchance * 100.0f,
        max_acc, force);
  }

  // autostop planning is independent from firing. Ground movement can use a
  // predicted stopped eye, airborne stopping keeps the current target context
  if (!shot_viable && this->ShouldStopMovement(ctx)) {
    const auto stop = this->PredictStop(ctx, primary_eye, local);
    if (stop) {
      const auto future_offset = stop->eye - primary_eye;
      auto planned_hits =
          scan_from_eye_candidates(future_offset, stop->inaccuracy);
      const auto planned =
          this->SelectBest(ctx, planned_hits, stop->inaccuracy);
      this->m_should_stop_ = best.valid || planned.valid;
    } else {
      this->m_should_stop_ = best.valid;
    }
  }

  if (!best.valid) {
    finish_doubletap(false);
    return;
  }

  if (duckpeek_active && allow_fire) {
    if (shot_viable) {
      this->m_release_duck_for_shot_ = true;
    } else if (!this->m_duckpeek_reduck_) {
      this->m_release_duck_for_shot_ = false;
    }
  }

  auto ready_to_fire = shot_viable;
  if (duckpeek_active) {
    if (is_ducked) {
      ready_to_fire = false;
    } else {
      ready_to_fire = ready_to_fire && this->m_release_duck_for_shot_;
    }
  }

  if (ready_to_fire && allow_fire) {
    const auto subtick_attack = finish_doubletap(true);
    this->FireGun(cmd, best, !accurate && force, best.hit.source_eye.position,
                  local, subtick_attack);

    if (duckpeek_active) {
      this->m_duckpeek_reduck_ = true;
      this->m_release_duck_for_shot_ = false;
    }
  } else {
    finish_doubletap(false);
  }
}

void Rage::RunTaser(systems::Input::Usercmd* cmd, const AimContext& ctx,
                    const systems::Local::Snapshot& local) {
  if (!settings::g_combat.m_zeusbot.enabled) {
    return;
  }

  auto candidates = this->GatherCandidates(local);
  if (candidates.empty()) {
    return;
  }

  auto eye_candidates = g_shared.Sh().GetCandidates();
  if (eye_candidates.count == 0) {
    eye_candidates.entries[0].position = g_shared.GetShootPosition();
    eye_candidates.entries[0].is_uninterpolated = true;
    eye_candidates.count = 1;
  }

  std::vector<ScanHit> all_hits;

  for (auto i = 0; i < eye_candidates.count; ++i) {
    auto hits = this->ScanTaser(eye_candidates.entries[i].position, ctx,
                                candidates, local);

    for (auto& h : hits) {
      h.source_eye = eye_candidates.entries[i];
      all_hits.push_back(std::move(h));
    }
  }

  if (all_hits.empty()) {
    return;
  }

  Target best{};

  for (const auto& h : all_hits) {
    if (!best.valid || h.score > best.score) {
      best.hit = h;
      best.hitchance = 1.0f;
      best.score = h.score;
      best.valid = true;
    }
  }

  if (best.valid) {
    this->m_zeus_fired_ = true;
    this->FireMelee(cmd, best, local);
  }
}

void Rage::RunKnife(systems::Input::Usercmd* cmd, const AimContext& ctx,
                    const systems::Local::Snapshot& local) {
  if (!settings::g_combat.m_knifebot.enabled) {
    return;
  }

  const auto info = this->GetKnifeInfo(local);
  if (!info.can_slash && !info.can_stab) {
    return;
  }

  constexpr auto kMaxKnifeDistSq = 150.0f * 150.0f;
  auto candidates = this->GatherCandidates(local, kMaxKnifeDistSq);
  if (candidates.empty()) {
    return;
  }

  auto eye_candidates = g_shared.Sh().GetCandidates();
  if (eye_candidates.count == 0) {
    eye_candidates.entries[0].position = g_shared.GetShootPosition();
    eye_candidates.entries[0].is_uninterpolated = true;
    eye_candidates.count = 1;
  }

  std::vector<ScanHit> all_hits;

  for (auto i = 0; i < eye_candidates.count; ++i) {
    auto hits = this->ScanKnife(eye_candidates.entries[i].position, ctx, info,
                                candidates, local);

    for (auto& h : hits) {
      h.source_eye = eye_candidates.entries[i];
      all_hits.push_back(std::move(h));
    }
  }

  if (all_hits.empty()) {
    return;
  }

  Target best{};
  Target best_backstab{};

  for (const auto& h : all_hits) {
    auto& dest = h.is_backstab ? best_backstab : best;

    if (!dest.valid || h.score > dest.score) {
      dest.hit = h;
      dest.hitchance = 1.0f;
      dest.score = h.score;
      dest.valid = true;
    }
  }

  auto& chosen = best_backstab.valid ? best_backstab : best;
  if (!chosen.valid) {
    return;
  }

  this->m_knife_attack_ = static_cast<std::uint8_t>(chosen.hit.attack_type);
  this->FireMelee(cmd, chosen, local);
}

void Rage::AutoRevolver(systems::Input::Usercmd* cmd, const AimContext& ctx,
                        const systems::Local::Snapshot& local) {
  if (!settings::g_combat.m_ragebot.enabled) {
    this->m_revolver_cock_ticks_ = 0;
    return;
  }

  if (!g_shared.CanShoot(cmd, local.controller)) {
    this->m_revolver_cock_ticks_ = 0;
    return;
  }

  if (!settings::g_combat.m_autos.revolver.value) {
    this->m_revolver_cock_ticks_ = 0;
    return;
  }

  constexpr auto kCockTicks{13};
  if (this->m_revolver_cock_ticks_ >= kCockTicks) {
    // End the held cycle. Target selection adds attack back on this
    // command only when the revolver should actually fire.
    cmd->buttons.value &= ~cstypes::command_buttons::kInAttack;
    cmd->buttons.value_changed |= cstypes::command_buttons::kInAttack;
    cmd->buttons.value_scroll &= ~cstypes::command_buttons::kInAttack;
    cmd->csgo_user_cmd.SetAttack1StartHistoryIndex(-1);
    this->m_revolver_cock_ticks_ = 0;

    this->RunGun(cmd, ctx, local);
    return;
  }

  // Keep target and hitchance planning active throughout the cock cycle.
  // Autostop consumes this decision later in the same command.
  this->RunGun(cmd, ctx, local, false);

  cmd->buttons.value |= cstypes::command_buttons::kInAttack;
  cmd->buttons.value_changed |= cstypes::command_buttons::kInAttack;
  cmd->buttons.value_scroll |= cstypes::command_buttons::kInAttack;

  const auto history_index = cmd->csgo_user_cmd.InputHistorySize() - 1;
  if (history_index >= 0) {
    cmd->csgo_user_cmd.SetAttack1StartHistoryIndex(history_index);
  }

  ++this->m_revolver_cock_ticks_;
}

std::vector<Rage::ScanHit> Rage::ScanPlayers(
    const math::Vector3& eye, float inaccuracy, const AimContext& ctx,
    std::vector<Candidate>& candidates,
    const systems::Local::Snapshot& local) const {
  std::vector<std::vector<ScanHit>> per_candidate(candidates.size());

  threadpool::ParallelFor(
      0, static_cast<int>(candidates.size()),
      [&](int begin, int end) {
        for (auto ci = begin; ci < end; ++ci) {
          auto& cand = candidates[ci];
          auto& candidate_hits = per_candidate[ci];
          candidate_hits.reserve(24);

          for (auto ri = 0; ri < cand.record_count; ++ri) {
            if (!cand.records[ri] || !cand.records[ri]->valid) {
              continue;
            }

            auto hits = this->ScanPlayer(eye, inaccuracy, ctx, cand,
                                         cand.records[ri], local);

            for (auto& h : hits) {
              candidate_hits.push_back(std::move(h));
            }
          }
        }
      },
      1);

  std::vector<ScanHit> flat;
  auto total_hits{std::size_t{}};
  for (const auto& hits : per_candidate) {
    total_hits += hits.size();
  }
  flat.reserve(total_hits);

  for (auto& v : per_candidate) {
    for (auto& h : v) {
      flat.push_back(std::move(h));
    }
  }

  return flat;
}

std::vector<Rage::ScanHit> Rage::ScanPlayer(
    const math::Vector3& eye, float inaccuracy, const AimContext& ctx,
    Candidate& cand, Shared::Lagcomp::Record* record,
    const systems::Local::Snapshot& local) const {
  if (!cand.pawn || cand.record_count <= 0 || cand.health <= 0 || !record ||
      !record->valid) {
    return {};
  }

  const auto& shared_ctx = g_shared.Ctx();
  const auto& config =
      settings::g_combat.m_ragebot.GetGroup(shared_ctx.weapon_type);

  const auto pen_ctx = g_shared.Pen().PrepareTarget(cand.pawn, record);
  const auto& hitbox_set = pen_ctx.hitboxes;

  const auto force_body = config.body_aim.value;

  std::array<int, 19> scan_order{};
  auto scan_count{0};

  if (!force_body && config.hitboxes.values[0]) {
    scan_order[scan_count++] = 0;
  }

  if (config.hitboxes.values[1]) {
    scan_order[scan_count++] = 4;
    scan_order[scan_count++] = 5;
    scan_order[scan_count++] = 6;
  }

  if (config.hitboxes.values[2]) {
    scan_order[scan_count++] = 3;
    scan_order[scan_count++] = 2;
  }

  if (config.hitboxes.values[3]) {
    for (auto idx : {13, 14, 15, 16, 17, 18}) {
      scan_order[scan_count++] = idx;
    }
  }

  if (config.hitboxes.values[4]) {
    for (auto idx : {7, 8, 9, 10}) {
      scan_order[scan_count++] = idx;
    }
  }

  if (config.hitboxes.values[5]) {
    for (auto idx : {11, 12}) {
      scan_order[scan_count++] = idx;
    }
  }

  // Old configs can deserialize with every hitbox disabled. Keep the
  // ragebot operational with the core head and torso hitboxes.
  if (scan_count == 0) {
    if (!force_body) {
      scan_order[scan_count++] = 0;
    }

    for (auto idx : {4, 5, 6, 3, 2}) {
      scan_order[scan_count++] = idx;
    }
  }

  struct TracePoint {
    math::Vector3 position;
    int hitbox_index;
    int bone_index;
    systems::Hitboxes::Entry hitbox;
    bool is_center;
  };

  std::vector<TracePoint> points;
  points.reserve(static_cast<std::size_t>(scan_count) * 12);

  for (auto idx = 0; idx < scan_count; ++idx) {
    const auto hitbox_index = scan_order[idx];
    const systems::Hitboxes::Entry* hb{nullptr};

    for (const auto& entry : hitbox_set) {
      if (entry.index == hitbox_index) {
        hb = &entry;
        break;
      }
    }

    if (!hb || hb->bone < 0 || hb->bone >= record->bone_count) {
      if (hitbox_index == 0) {
        if (!hb)
          ++cand.head_scan.missing;
        else
          ++cand.head_scan.invalid_bones;
      }
      continue;
    }

    // Range and penetration can only reduce this upper bound.
    const auto hitgroup = systems::g_hitboxes.HitgroupFromHitbox(hitbox_index);
    if (g_shared.Pen().GetMaxDamage(hitgroup, pen_ctx) < cand.min_damage) {
      if (hitbox_index == 0) ++cand.head_scan.damage_pruned;
      continue;
    }

    const auto& bone = record->bones[hb->bone];
    if (bone.position.LengthSqr() < 1.0f) {
      continue;
    }

    const auto hitbox_center = (hb->mins + hb->maxs) * 0.5f;
    const auto rotation = bone.rotation;
    const auto center = rotation.RotateVector(hitbox_center) + bone.position;

    TracePoint cp{};
    cp.position = center;
    cp.hitbox_index = hitbox_index;
    cp.bone_index = hb->bone;
    cp.hitbox = *hb;
    cp.is_center = true;
    points.push_back(cp);
    if (hitbox_index == 0) ++cand.head_scan.points;

    if (config.debug_multipoints.value) {
      std::lock_guard lock(m_debug_mtx_);
      m_debug_points_.push_back({center, hitbox_index, true});
    }

    if (config.pointscale > 0.0f) {
      const auto mps = this->GenerateMultipoints(
          *hb, center, rotation, config.pointscale, eye, inaccuracy);

      for (const auto& mp : mps) {
        const auto duplicate = std::any_of(
            points.begin(), points.end(), [&](const TracePoint& point) {
              return point.hitbox_index == hitbox_index &&
                     (point.position - mp).LengthSqr() < 0.01f;
            });
        if (duplicate) {
          continue;
        }

        TracePoint tp{};
        tp.position = mp;
        tp.hitbox_index = hitbox_index;
        tp.bone_index = hb->bone;
        tp.hitbox = *hb;
        tp.is_center = false;
        points.push_back(tp);
        if (hitbox_index == 0) ++cand.head_scan.points;

        if (config.debug_multipoints.value) {
          std::lock_guard lock(m_debug_mtx_);
          m_debug_points_.push_back({mp, hitbox_index, false});
        }
      }
    }
  }

  if (points.empty()) {
    return {};
  }

  std::vector<ScanHit> results;
  results.reserve(static_cast<std::size_t>(scan_count) * 2);

  for (const auto& tp : points) {
    const auto aim = math::helpers::CalculateAngle(eye, tp.position);
    const auto fov = math::helpers::AngleDistance(ctx.view_angles, aim);

    if (fov > config.max_fov) {
      continue;
    }

    if (tp.hitbox_index == 0) ++cand.head_scan.traces;
    Shared::Penetration::Result pen{};
    if (!g_shared.Pen().Run(eye, tp.position, pen_ctx, local.pawn, local.team,
                            pen)) {
      continue;
    }

    if (tp.hitbox_index == 0) {
      ++cand.head_scan.hits;
      cand.head_scan.max_damage =
          std::max(cand.head_scan.max_damage, pen.damage);
      if (pen.hitbox != 0) ++cand.head_scan.wrong_hitbox;
    }
    const auto is_lethal = pen.damage >= static_cast<float>(cand.health);
    if (!is_lethal && pen.damage < cand.min_damage) {
      if (tp.hitbox_index == 0) ++cand.head_scan.below_damage;
      continue;
    }

    const auto actual_hitbox_index =
        pen.hitbox >= 0 ? pen.hitbox : tp.hitbox_index;
    const systems::Hitboxes::Entry* actual_hitbox{nullptr};

    for (const auto& entry : hitbox_set) {
      if (entry.index == actual_hitbox_index) {
        actual_hitbox = &entry;
        break;
      }
    }

    if (!actual_hitbox || actual_hitbox->bone < 0 ||
        actual_hitbox->bone >= record->bone_count) {
      continue;
    }

    // A point belongs to its requested hitbox, including head centers.
    if (actual_hitbox_index != tp.hitbox_index) {
      continue;
    }

    ScanHit h{};
    h.position = tp.position;
    h.aim_angle = aim;
    h.damage = pen.damage;
    h.fov = fov;
    h.hitbox_index = actual_hitbox_index;
    h.hitgroup = pen.hitgroup;
    h.bone_index = actual_hitbox->bone;
    h.hitbox = *actual_hitbox;
    h.is_center = tp.is_center && actual_hitbox_index == tp.hitbox_index;
    h.penetrated = pen.penetrated;
    h.pawn = cand.pawn;
    h.health = cand.health;
    h.record = record;

    results.push_back(h);
  }

  return results;
}

Rage::Target Rage::SelectBest(const AimContext& aim_ctx,
                              const std::vector<ScanHit>& hits,
                              float eval_inaccuracy) const {
  auto hitgroup_priority = [](int hitbox_index) -> int {
    if (hitbox_index == 0) {
      return 4;
    }
    if (hitbox_index >= 1 && hitbox_index <= 6) {
      return 3;
    }
    if (hitbox_index >= 13 && hitbox_index <= 18) {
      return 2;
    }
    if (hitbox_index >= 7 && hitbox_index <= 12) {
      return 1;
    }
    return 0;
  };

  // damage alone cannot tell whether a point passes hitchance. evaluate every
  // traced point so a high damage miss cannot hide an accurate body shot.
  struct EvaluatedHit {
    int hit_index;
    float hitchance;
    float score;
    bool passes_hitchance;
  };

  std::vector<EvaluatedHit> evaluated;
  evaluated.reserve(hits.size());

  const auto& config =
      settings::g_combat.m_ragebot.GetGroup(g_shared.Ctx().weapon_type);
  const auto needed_hc =
      config.hitchance_override.value
          ? static_cast<float>(config.hitchance_override_value) / 100.0f
          : static_cast<float>(config.hitchance) / 100.0f;

  auto max_head_hitchance{0.0f};
  for (auto idx = 0; idx < static_cast<int>(hits.size()); ++idx) {
    const auto& h = hits[idx];

    if (!h.record || !h.record->valid) {
      continue;
    }

    if (h.bone_index < 0 || h.bone_index >= h.record->bone_count) {
      continue;
    }

    const auto& bone = h.record->bones[h.bone_index];
    const auto hc = config.no_spread.value
                        ? 1.0f
                        : g_shared.CalculateHitchance(
                              h.source_eye.position, h.aim_angle, h.hitbox,
                              bone, eval_inaccuracy, aim_ctx.spread);
    if (h.hitbox_index == 0)
      max_head_hitchance = std::max(max_head_hitchance, hc);
    const auto hp = static_cast<float>(h.health);
    const auto can_kill = h.damage >= hp;
    const auto passes_hitchance = config.no_spread.value || hc >= needed_hc;
    auto score = passes_hitchance ? 1000000.0f : 0.0f;

    if (can_kill) {
      score += 100000.0f + hc * 10000.0f;
    } else {
      score += h.damage * hc * 100.0f + h.damage * 5.0f;
    }

    score += h.penetrated ? 0.0f : 250.0f;
    score += h.is_center ? 50.0f : 0.0f;
    score += static_cast<float>(hitgroup_priority(h.hitbox_index)) * 2.0f;
    score -= h.fov * 0.1f;

    evaluated.push_back(EvaluatedHit{idx, hc, score, passes_hitchance});
  }

  Target best{};

  for (const auto& e : evaluated) {
    if (e.hit_index < 0 || e.hit_index >= static_cast<int>(hits.size())) {
      continue;
    }

    const auto& h = hits[e.hit_index];

    if (!h.record || !h.record->valid) {
      continue;
    }

    const auto best_passes_hitchance =
        best.valid && (config.no_spread.value || best.hitchance >= needed_hc);
    auto is_better = !best.valid || (e.passes_hitchance != best_passes_hitchance
                                         ? e.passes_hitchance
                                         : e.score > best.score);
    const auto same_hitchance_tier =
        best.valid && e.passes_hitchance == best_passes_hitchance;
    const auto lethal = h.damage >= static_cast<float>(h.health);
    if (same_hitchance_tier && lethal != best.IsLethal()) {
      is_better = lethal;
    }
    if (same_hitchance_tier && lethal == best.IsLethal() &&
        std::fabsf(e.score - best.score) < 0.01f) {
      if (h.record->tick != best.hit.record->tick) {
        is_better = h.record->tick > best.hit.record->tick;
      } else if (h.is_center != best.hit.is_center) {
        is_better = h.is_center;
      } else {
        is_better = h.fov < best.hit.fov;
      }
    }

    if (is_better) {
      best.hit = h;
      best.hitchance = e.hitchance;
      best.score = e.score;
      best.valid = true;
    }
  }

  best.head_hitchance = max_head_hitchance;
  return best;
}

float Rage::GetStandingInaccuracy(const systems::Local::Snapshot& local,
                                  const AimContext& ctx) const {
  auto velocity = ctx.velocity;
  velocity.z = 0.0f;

  const auto speed = velocity.Length2d();
  if (speed > ctx.accurate_threshold) {
    return g_shared.GetInaccuracyAtVelocity(local.pawn, velocity);
  }

  const auto& shared_ctx = g_shared.Ctx();
  if (!shared_ctx.weapon_vdata) {
    return ctx.predicted_inaccuracy;
  }

  const auto mode_offset = ctx.is_scoped ? sizeof(float) : 0;
  const auto inaccuracy_stand = memory::Read<float>(
      shared_ctx.weapon_vdata +
      SCHEMA("CCSWeaponBaseVData", "m_flInaccuracyStand"_hash) + mode_offset);
  return std::max(inaccuracy_stand,
                  g_shared.GetInaccuracyAtVelocity(local.pawn, velocity));
}

std::vector<Rage::ScanHit> Rage::ScanTaser(
    const math::Vector3& eye, const AimContext& ctx,
    std::vector<Candidate>& candidates,
    const systems::Local::Snapshot& local) const {
  const auto& shared_ctx = g_shared.Ctx();
  std::vector<ScanHit> results;

  for (auto& cand : candidates) {
    for (auto ri = 0; ri < cand.record_count; ++ri) {
      auto* record = cand.records[ri];
      if (!record || !record->valid) {
        continue;
      }

      const auto game_scene_node = memory::Read<std::uintptr_t>(
          cand.pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
      if (!game_scene_node) {
        continue;
      }

      const auto hitbox_set = systems::g_hitboxes.Query(game_scene_node);
      if (hitbox_set.count <= 0) {
        continue;
      }

      record->Apply();

      for (auto i = 0; i < hitbox_set.count; ++i) {
        const auto& hb = hitbox_set.entries[i];

        if (hb.bone < 0 || hb.bone >= record->bone_count) {
          continue;
        }

        const auto& bone = record->bones[hb.bone];
        if (bone.position.LengthSqr() < 1.0f) {
          continue;
        }

        const auto center =
            bone.rotation.RotateVector((hb.mins + hb.maxs) * 0.5f) +
            bone.position;
        const auto aim = math::helpers::CalculateAngle(eye, center);
        const auto fov = math::helpers::AngleDistance(ctx.view_angles, aim);

        if (fov > settings::g_combat.m_zeusbot.max_fov) {
          continue;
        }

        math::Vector3 forward{};
        math::helpers::AngleVectorsLeft(aim, &forward);

        const auto trace = this->TraceTaserHit(
            eye, forward, shared_ctx.range * 0.85f, cand.pawn, local.pawn);
        if (trace.hit_entity != cand.pawn) {
          continue;
        }

        const auto dist = (center - eye).Length();
        const auto range_fraction = dist / shared_ctx.range;

        ScanHit h{};
        h.position = center;
        h.aim_angle = aim;
        h.damage = 500.0f;
        h.score = (10000.0f - dist) * (range_fraction > 0.92f ? 0.8f : 1.0f);
        h.fov = fov;
        h.hitbox_index = hb.index;
        h.hitgroup = systems::g_hitboxes.HitgroupFromHitbox(hb.index);
        h.bone_index = hb.bone;
        h.hitbox = hb;
        h.is_center = true;
        h.pawn = cand.pawn;
        h.health = cand.health;
        h.record = record;

        results.push_back(h);
      }

      record->Restore();
    }
  }

  return results;
}

Rage::KnifeInfo Rage::GetKnifeInfo(
    const systems::Local::Snapshot& local) const {
  const auto& shared_ctx = g_shared.Ctx();
  const auto tick_base = memory::Read<int>(
      local.controller + SCHEMA("CBasePlayerController", "m_nTickBase"_hash));
  const auto next_primary = memory::Read<int>(
      shared_ctx.weapon +
      SCHEMA("C_BasePlayerWeapon", "m_nNextPrimaryAttackTick"_hash));
  const auto next_secondary = memory::Read<int>(
      shared_ctx.weapon +
      SCHEMA("C_BasePlayerWeapon", "m_nNextSecondaryAttackTick"_hash));
  const auto last_shot_time = memory::Read<float>(
      shared_ctx.weapon + SCHEMA("C_CSWeaponBase", "m_fLastShotTime"_hash));
  const auto cur_time = static_cast<float>(tick_base) * cstypes::kTickInterval;

  return KnifeInfo{.can_slash = tick_base >= next_primary,
                   .can_stab = tick_base >= next_secondary,
                   .charged = (cur_time - last_shot_time) > 0.4f,
                   .armor_ratio = memory::Read<float>(
                       shared_ctx.weapon_vdata +
                       SCHEMA("CCSWeaponBaseVData", "m_flArmorRatio"_hash))};
}

std::vector<Rage::ScanHit> Rage::ScanKnife(
    const math::Vector3& eye, const AimContext& ctx, const KnifeInfo& info,
    std::vector<Candidate>& candidates,
    const systems::Local::Snapshot& local) const {
  constexpr auto kStabRange{50.0f};
  constexpr auto kSlashRange{66.0f};

  std::vector<ScanHit> results;

  for (auto& cand : candidates) {
    const auto eye_angles = memory::Read<math::Vector3>(
        cand.pawn + SCHEMA("C_CSPlayerPawn", "m_angEyeAngles"_hash));
    const auto hp = static_cast<float>(cand.health);

    const auto frontal_slash_dmg = this->GetKnifeDamage(
        info.charged ? 40.0f : 25.0f, cand.armor, info.armor_ratio);
    const auto frontal_stab_dmg =
        this->GetKnifeDamage(65.0f, cand.armor, info.armor_ratio);
    const auto frontal_can_kill = (info.can_slash && frontal_slash_dmg >= hp) ||
                                  (info.can_stab && frontal_stab_dmg >= hp);

    for (auto ri = 0; ri < cand.record_count; ++ri) {
      auto* record = cand.records[ri];
      if (!record || !record->valid) {
        continue;
      }

      const auto game_scene_node = memory::Read<std::uintptr_t>(
          cand.pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
      if (!game_scene_node) {
        continue;
      }

      const auto hitbox_set = systems::g_hitboxes.Query(game_scene_node);
      if (hitbox_set.count <= 0) {
        continue;
      }

      record->Apply();

      auto backstab{false};
      {
        const auto delta = record->origin - systems::g_prediction.Pre().origin;
        const auto dist_2d = std::sqrtf(delta.x * delta.x + delta.y * delta.y);

        if (dist_2d > 0.001f) {
          const auto dir_x = delta.x / dist_2d;
          const auto dir_y = delta.y / dist_2d;

          math::Vector3 body_forward{};
          math::helpers::AngleVectorsLeft(record->rotation, &body_forward);

          math::Vector3 eye_forward{};
          math::helpers::AngleVectorsLeft(eye_angles, &eye_forward);

          backstab =
              (dir_x * body_forward.x + dir_y * body_forward.y) > 0.475f ||
              (dir_x * eye_forward.x + dir_y * eye_forward.y) > 0.475f;
        }
      }

      const auto wait_for_backstab = backstab && !frontal_can_kill;

      for (auto i = 0; i < hitbox_set.count; ++i) {
        const auto& hb = hitbox_set.entries[i];

        if (hb.bone < 0 || hb.bone >= record->bone_count) {
          continue;
        }

        const auto& bone = record->bones[hb.bone];
        if (bone.position.LengthSqr() < 1.0f) {
          continue;
        }

        const auto center =
            bone.rotation.RotateVector((hb.mins + hb.maxs) * 0.5f) +
            bone.position;
        const auto dist = (center - eye).Length();
        const auto max_reach = info.can_slash ? kSlashRange : kStabRange;

        if (dist > max_reach) {
          continue;
        }

        const auto aim = math::helpers::CalculateAngle(eye, center);
        const auto fov = math::helpers::AngleDistance(ctx.view_angles, aim);

        if (fov > settings::g_combat.m_knifebot.max_fov) {
          continue;
        }

        math::Vector3 forward{};
        math::helpers::AngleVectorsLeft(aim, &forward);

        for (const auto try_stab : {true, false}) {
          if (try_stab && !info.can_stab) {
            continue;
          }

          if (!try_stab && !info.can_slash) {
            continue;
          }

          const auto reach = try_stab ? kStabRange : kSlashRange;
          if (dist > reach) {
            continue;
          }

          const auto raw_dmg =
              try_stab ? (backstab ? 180.0f : 65.0f)
                       : (backstab ? 90.0f : (info.charged ? 40.0f : 25.0f));
          const auto damage =
              this->GetKnifeDamage(raw_dmg, cand.armor, info.armor_ratio);
          const auto can_kill = damage >= hp;

          if (wait_for_backstab && !can_kill) {
            continue;
          }

          const auto trace =
              this->TraceKnifeHit(eye, forward, reach, cand.pawn, local.pawn);
          if (trace.hit_entity != cand.pawn) {
            continue;
          }

          const auto reach_margin = 1.0f - (dist / reach);

          ScanHit h{};
          h.position = center;
          h.aim_angle = aim;
          h.damage = damage;
          h.score = can_kill ? (10000.0f + damage * reach_margin)
                             : (damage * 100.0f * reach_margin);
          h.fov = fov;
          h.hitbox_index = hb.index;
          h.hitgroup = systems::g_hitboxes.HitgroupFromHitbox(hb.index);
          h.bone_index = hb.bone;
          h.hitbox = hb;
          h.is_center = true;
          h.is_backstab = backstab;
          h.attack_type = try_stab ? 1 : 0;
          h.pawn = cand.pawn;
          h.health = cand.health;
          h.record = record;

          results.push_back(h);
          break;
        }
      }

      record->Restore();
    }
  }

  return results;
}

void Rage::FireGun(systems::Input::Usercmd* cmd, const Target& tgt,
                   bool was_forced, const math::Vector3& shoot_eye,
                   const systems::Local::Snapshot& local, bool subtick_attack) {
  if (!tgt.hit.record || !tgt.hit.record->valid) {
    return;
  }

  this->m_firing_this_tick_ = true;

  const auto base = cmd->csgo_user_cmd.MutableBase();
  const auto tick_base = memory::Read<int>(
      local.controller + SCHEMA("CBasePlayerController", "m_nTickBase"_hash));
  const auto& shared_ctx = g_shared.Ctx();
  const auto& config =
      settings::g_combat.m_ragebot.GetGroup(shared_ctx.weapon_type);
  const auto aim_punch = g_shared.GetAimPunch(local.pawn);
  auto aim_angle = config.no_spread.value ? math::helpers::CalculateAngle(
                                                shoot_eye, tgt.hit.position)
                                          : tgt.hit.aim_angle;

  if (config.no_spread.value) {
    auto stamp_tick = tick_base;
    auto stamp_frac{0.0f};

    if (!tgt.hit.source_eye.is_uninterpolated) {
      auto tick_add = [](int t, float f, int dt, float df) {
        f += df;
        auto carry = static_cast<int>(std::floor(f));
        f -= static_cast<float>(carry);
        return std::pair{t + dt + carry, f};
      };

      std::tie(stamp_tick, stamp_frac) = tick_add(
          tgt.hit.source_eye.player_tick, tgt.hit.source_eye.player_frac,
          tgt.hit.source_eye.lerp_ticks_int,
          tgt.hit.source_eye.lerp_ticks_frac);
    }

    const auto corrected = g_shared.FindSpreadCorrection(aim_angle, stamp_tick);
    if (corrected.x == 0.0f && corrected.y == 0.0f && corrected.z == 0.0f) {
      this->m_firing_this_tick_ = false;
      return;
    }

    aim_angle = corrected;
  }

  g_shared.LastShootTick() = tick_base;

  if (settings::g_misc.m_impacts.console_log.value) {
    const auto hitgroup_name =
        systems::g_hitboxes.HitgroupToName(tgt.hit.hitgroup);
    const auto bt_delta = g_shared.Ctx().current_tick - tgt.hit.record->tick;
    logging::console::Print(
        xs("[rage] shot target hp {} for {:.0f} in {} (hc {:.0f}%, bt {}t{})"),
        tgt.hit.health, tgt.hit.damage, hitgroup_name, tgt.hitchance * 100.0f,
        bt_delta, was_forced ? xs(", forced") : "");
  }

  features::misc::g_impacts.OnBoom(
      tgt.hit.pawn, tgt.hit.hitgroup, tgt.hit.damage, tgt.hitchance,
      shared_ctx.inaccuracy, shared_ctx.spread, aim_angle, shoot_eye,
      tgt.hit.record->tick, g_shared.Lc().GetSkeleton(*tgt.hit.record),
      was_forced);
  features::esp::player::g_chams.Os().Push(tgt.hit.pawn);
  const auto record_time = cstypes::TickFraction::FromValue(
      tgt.hit.record->simulation_time / cstypes::kTickInterval);
  const auto history_size = cmd->csgo_user_cmd.InputHistorySize();
  for (auto i = 0; i < history_size; ++i) {
    const auto entry = cmd->csgo_user_cmd.MutableInputHistory(i);
    if (!entry) {
      continue;
    }

    if (const auto angles = entry->MutableViewAngles()) {
      angles->SetX(aim_angle.x - aim_punch.x);
      angles->SetY(aim_angle.y - aim_punch.y);

      if (config.no_spread.value) {
        angles->SetZ(aim_angle.z);
      }
    }

    entry->SetRenderTickCount(record_time.tick + 1);
    entry->SetRenderTickFraction(0.0f);

    if (!tgt.hit.source_eye.is_uninterpolated) {
      auto tick_add = [](int t, float f, int dt, float df) {
        f += df;
        auto carry = static_cast<int>(std::floor(f));
        f -= static_cast<float>(carry);
        return std::pair{t + dt + carry, f};
      };

      const auto [stamp_tick, stamp_frac] = tick_add(
          tgt.hit.source_eye.player_tick, tgt.hit.source_eye.player_frac,
          tgt.hit.source_eye.lerp_ticks_int,
          tgt.hit.source_eye.lerp_ticks_frac);

      entry->SetPlayerTickCount(stamp_tick);
      entry->SetPlayerTickFraction(stamp_frac);
    }

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

  if (!subtick_attack) {
    cmd->buttons.value |= cstypes::command_buttons::kInAttack;
    cmd->buttons.value_changed |= cstypes::command_buttons::kInAttack;
    cmd->buttons.value_scroll |= cstypes::command_buttons::kInAttack;

    if (history_size > 0) {
      cmd->csgo_user_cmd.SetAttack1StartHistoryIndex(history_size - 1);
    }
  }

  math::Vector3 forward{};
  {
    if (const auto angles = base->Viewangles()) {
      math::helpers::AngleVectorsLeft({angles->X(), angles->Y(), angles->Z()},
                                      &forward);
    }
  }

  const auto punched_aim =
      math::Vector3{aim_angle.x - aim_punch.x, aim_angle.y - aim_punch.y, 0.0f};
  const auto facing_away =
      forward.Dot((tgt.hit.record->origin -
                   systems::g_prediction.Pre().networked_origin)
                      .Normalized()) < 0.707107f;

  auto command_aim = punched_aim;
  if (!subtick_attack && facing_away &&
      settings::g_combat.m_antiaim.hide_shots.value) {
    command_aim.x = 179.9f;
    command_aim.y = std::remainderf(punched_aim.y + 180.0f, 360.0f);
  }

  if (const auto angles = base->MutableViewangles()) {
    angles->SetX(command_aim.x);
    angles->SetY(command_aim.y);
  }

  if (!config.silent.value) {
    systems::g_input.SetViewAngles(punched_aim);
  }
}

void Rage::FireMelee(systems::Input::Usercmd* cmd, const Target& tgt,
                     const systems::Local::Snapshot& local) {
  if (!tgt.hit.record || !tgt.hit.record->valid) {
    return;
  }

  this->m_firing_this_tick_ = true;

  const auto base = cmd->csgo_user_cmd.MutableBase();
  const auto tick_base = memory::Read<int>(
      local.controller + SCHEMA("CBasePlayerController", "m_nTickBase"_hash));

  g_shared.LastShootTick() = tick_base;

  const auto record_time = cstypes::TickFraction::FromValue(
      tgt.hit.record->simulation_time / cstypes::kTickInterval);
  const auto history_index = cmd->csgo_user_cmd.InputHistorySize() - 1;
  const auto entry = history_index >= 0
                         ? cmd->csgo_user_cmd.MutableInputHistory(history_index)
                         : nullptr;

  if (entry) {
    if (const auto angles = entry->MutableViewAngles()) {
      angles->SetX(tgt.hit.aim_angle.x);
      angles->SetY(tgt.hit.aim_angle.y);
    }

    entry->SetRenderTickCount(record_time.tick + 1);
    entry->SetRenderTickFraction(0.0f);

    if (!tgt.hit.source_eye.is_uninterpolated) {
      auto tick_add = [](int t, float f, int dt, float df) {
        f += df;
        auto carry = static_cast<int>(std::floor(f));
        f -= static_cast<float>(carry);
        return std::pair{t + dt + carry, f};
      };

      const auto [stamp_tick, stamp_frac] = tick_add(
          tgt.hit.source_eye.player_tick, tgt.hit.source_eye.player_frac,
          tgt.hit.source_eye.lerp_ticks_int,
          tgt.hit.source_eye.lerp_ticks_frac);

      entry->SetPlayerTickCount(stamp_tick);
      entry->SetPlayerTickFraction(stamp_frac);
    }

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

  const auto is_secondary = tgt.hit.attack_type == 1;
  const auto attack_button = is_secondary
                                 ? cstypes::command_buttons::kInSecondAttack
                                 : cstypes::command_buttons::kInAttack;

  cmd->buttons.value |= attack_button;
  cmd->buttons.value_changed |= attack_button;
  cmd->buttons.value_scroll |= attack_button;

  if (history_index >= 0) {
    if (is_secondary) {
      cmd->csgo_user_cmd.SetAttack2StartHistoryIndex(history_index);
    } else {
      cmd->csgo_user_cmd.SetAttack1StartHistoryIndex(history_index);
    }
  }

  if (const auto angles = base->MutableViewangles()) {
    angles->SetX(tgt.hit.aim_angle.x);
    angles->SetY(tgt.hit.aim_angle.y);
  }
}

std::vector<math::Vector3> Rage::GenerateMultipoints(
    const systems::Hitboxes::Entry& hitbox, const math::Vector3& center,
    const math::Quaternion& bone_rot, float pointscale,
    const math::Vector3& shoot_pos, float inaccuracy) const {
  std::vector<math::Vector3> out;

  auto scale = std::clamp(pointscale / 100.0f, 0.0f, 1.0f);
  if (scale <= 0.01f) {
    return out;
  }

  const auto hb_mid = (hitbox.mins + hitbox.maxs) * 0.5f;
  const auto capsule_a = center + bone_rot.RotateVector(hitbox.mins - hb_mid);
  const auto capsule_b = center + bone_rot.RotateVector(hitbox.maxs - hb_mid);

  // Keep points inside the part of the hitbox reachable by the full spread
  // cone.
  const auto& config =
      settings::g_combat.m_ragebot.GetGroup(g_shared.Ctx().weapon_type);
  if (config.dynamic_pointscale.value && hitbox.radius > 0.001f) {
    const auto cone = std::max(inaccuracy + g_shared.Ctx().spread, 0.0f);
    const auto cone_radius = std::tanf(cone) * (center - shoot_pos).Length();
    const auto automatic_scale =
        std::clamp(0.9f - cone_radius / hitbox.radius, 0.0f, 1.0f);
    scale = std::min(scale, automatic_scale);

    if (scale <= 0.01f) {
      return out;
    }
  }

  // Build a view-relative frame so points rotate correctly above and below us.
  const auto shoot_dir = (center - shoot_pos).Normalized();
  const auto ang = math::helpers::VectorToAngle(shoot_dir);

  math::Vector3 left{}, up{};
  math::helpers::AngleVectorsLeft(ang, nullptr, &left, &up);

  // angle_vectors_left returns left, so negate it for right.
  const auto right = math::Vector3{-left.x, -left.y, -left.z};

  // Trace from outside through the center to find the real capsule surface.
  // This is accurate around rounded end caps, where radius offsets are not.
  const auto surface_point =
      [&](const math::Vector3& direction) -> math::Vector3 {
    const auto dir = direction.Normalized();

    if (hitbox.radius > 0.001f) {
      const auto reach =
          (capsule_b - capsule_a).Length() + hitbox.radius * 2.0f + 1.0f;
      const auto origin = center + dir * reach;
      const auto delta = dir * (reach * -2.0f);
      auto fraction{1.0f};

      if (g_shared.RayVsCapsule(origin, delta, capsule_a, capsule_b,
                                hitbox.radius, fraction)) {
        return origin + delta * fraction;
      }
    } else {
      // Intersect box hitboxes in bone space using their directional support.
      auto inverse = bone_rot;
      inverse.x = -inverse.x;
      inverse.y = -inverse.y;
      inverse.z = -inverse.z;
      const auto local_dir = inverse.RotateVector(dir);
      const auto extents = (hitbox.maxs - hitbox.mins) * 0.5f;
      auto distance = 8192.0f;

      if (std::fabs(local_dir.x) > 1.0e-6f)
        distance = std::min(distance, std::fabs(extents.x / local_dir.x));
      if (std::fabs(local_dir.y) > 1.0e-6f)
        distance = std::min(distance, std::fabs(extents.y / local_dir.y));
      if (std::fabs(local_dir.z) > 1.0e-6f)
        distance = std::min(distance, std::fabs(extents.z / local_dir.z));

      if (distance < 8192.0f) {
        return center + dir * distance;
      }
    }

    return center;
  };

  const auto scaled_surface = [&](const math::Vector3& direction) {
    const auto surface = surface_point(direction);
    return center + (surface - center) * scale;
  };

  switch (hitbox.index) {
    case 0:  // head
    {
      out.reserve(4);
      out.push_back(scaled_surface(right));
      out.push_back(scaled_surface(-right));
      out.push_back(scaled_surface(up));
      out.push_back(scaled_surface(-up));
      break;
    }

    case 2:
    case 3:  // stomach / pelvis
    {
      out.reserve(2);
      out.push_back(scaled_surface(right));
      out.push_back(scaled_surface(-right));
      break;
    }

    case 4:
    case 5:
    case 6:  // chest
    {
      out.reserve(3);
      out.push_back(scaled_surface(right));
      out.push_back(scaled_surface(-right));
      if (hitbox.index == 6) {
        out.push_back(scaled_surface(up));
      }
      break;
    }

    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:  // legs / feet
    {
      out.reserve(2);
      out.push_back(capsule_a);
      out.push_back(capsule_b);
      break;
    }

    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
    case 18:  // arms
    {
      out.reserve(1);
      out.push_back(capsule_b);
      break;
    }

    default: {
      out.reserve(2);
      out.push_back(scaled_surface(right));
      out.push_back(scaled_surface(-right));
      break;
    }
  }

  return out;
}

bool Rage::ShouldStopMovement(const AimContext& ctx) const {
  const auto& shared_ctx = g_shared.Ctx();
  const auto velocity = ctx.velocity;

  if (shared_ctx.weapon_type == cstypes::weapon_type::kSniper &&
      !ctx.is_scoped) {
    return false;
  }

  if (ctx.on_ground) {
    const auto speed_2d = velocity.Length2d();
    if (speed_2d <= 0.1f) {
      return false;
    }

    const auto inaccuracy_move = memory::Read<float>(
        shared_ctx.weapon_vdata +
        SCHEMA("CCSWeaponBaseVData", "m_flInaccuracyMove"_hash));
    const auto inaccuracy_stand = memory::Read<float>(
        shared_ctx.weapon_vdata +
        SCHEMA("CCSWeaponBaseVData", "m_flInaccuracyStand"_hash));

    return speed_2d * inaccuracy_move > inaccuracy_stand;
  }

  if (shared_ctx.weapon_type != cstypes::weapon_type::kSniper) {
    return false;
  }

  if (velocity.z > 140.0f) {
    return false;
  }

  const auto sv_gravity = CONVAR("sv_gravity")->Get<float>();
  const auto sv_friction = CONVAR("sv_friction")->Get<float>();
  const auto sv_stopspeed = CONVAR("sv_stopspeed")->Get<float>();

  const auto inac_jump_initial = memory::Read<float>(
      shared_ctx.weapon_vdata +
      SCHEMA("CCSWeaponBaseVData", "m_flInaccuracyJumpInitial"_hash));
  const auto inac_jump_apex = memory::Read<float>(
      shared_ctx.weapon_vdata +
      SCHEMA("CCSWeaponBaseVData", "m_flInaccuracyJumpApex"_hash));
  const auto shootable_threshold = inac_jump_apex + 0.001f;
  const auto early_threshold =
      inac_jump_initial * 0.55f + inac_jump_apex * 0.45f;
  const auto air_inaccuracy =
      g_shared.GetAirInaccuracy(velocity.z, inac_jump_initial, inac_jump_apex);

  if (air_inaccuracy <= shootable_threshold ||
      air_inaccuracy <= early_threshold) {
    return true;
  }

  auto sim_vz = velocity.z;
  auto ticks_to_shootable{0};

  for (auto i = 1; i <= 32; ++i) {
    sim_vz -= sv_gravity * cstypes::kTickInterval;

    if (g_shared.GetAirInaccuracy(sim_vz, inac_jump_initial, inac_jump_apex) <=
        shootable_threshold) {
      ticks_to_shootable = i;
      break;
    }
  }

  if (ticks_to_shootable == 0) {
    return false;
  }

  const auto speed_2d = velocity.Length2d();
  const auto max_speed =
      memory::Read<float>(shared_ctx.weapon_vdata +
                          SCHEMA("CCSWeaponBaseVData", "m_flMaxSpeed"_hash));
  const auto accurate_threshold = max_speed * 0.34f;

  if (speed_2d <= accurate_threshold) {
    return true;
  }

  auto sim_speed = speed_2d;
  auto ticks_to_stop{32};

  for (auto i = 1; i <= 32; ++i) {
    const auto drop = std::fmaxf(sim_speed, sv_stopspeed) * sv_friction *
                      cstypes::kTickInterval;
    sim_speed -= drop;

    if (sim_speed <= accurate_threshold) {
      ticks_to_stop = i;
      break;
    }
  }

  return ticks_to_shootable <= ticks_to_stop + 2;
}

float Rage::GetMinDamage(const settings::Combat::Ragebot::WeaponGroup& config,
                         int target_health, bool override_active) const {
  const auto configured = static_cast<float>(
      override_active ? config.min_damage_override_value : config.min_damage);
  return std::min(configured, static_cast<float>(target_health));
}

float Rage::GetKnifeDamage(float raw, int armor, float armor_ratio) const {
  if (armor <= 0) {
    return raw;
  }

  const auto ratio = armor_ratio * 0.5f;
  auto damage_to_health = raw * ratio;
  const auto damage_to_armor = (raw - damage_to_health) * 0.5f;

  if (damage_to_armor > static_cast<float>(armor)) {
    damage_to_health = raw - static_cast<float>(armor) * 2.0f;
  }

  return std::max(0.0f, std::floorf(damage_to_health));
}

systems::Tracing::Result Rage::TraceTaserHit(const math::Vector3& origin,
                                             const math::Vector3& forward,
                                             float range,
                                             std::uintptr_t target_pawn,
                                             std::uintptr_t local_pawn) const {
  const auto end = origin + forward * range;
  const int filter_extras[]{0, 15};

  for (const auto extra : filter_extras) {
    const auto filter =
        extra == 0
            ? systems::g_tracing.MakeFilter(local_pawn, 0x001c1003, 4)
            : systems::g_tracing.MakeFilter(local_pawn, 0x001c1003, 4, 15);
    auto result = systems::g_tracing.Trace(origin, end, filter);

    if ((result.fraction < 1.0f || result.all_solid) &&
        result.hit_entity == target_pawn) {
      return result;
    }

    for (auto radius = 2.0f; radius <= 4.0f; radius += 2.0f) {
      const auto sweep_end = end - forward * radius;
      result =
          systems::g_tracing.TraceSphere(origin, sweep_end, radius, filter);

      if ((result.fraction < 1.0f || result.all_solid) &&
          result.hit_entity == target_pawn) {
        return result;
      }
    }
  }

  systems::Tracing::Result miss{};
  miss.fraction = 1.0f;
  miss.hit_entity = 0;
  return miss;
}

systems::Tracing::Result Rage::TraceKnifeHit(const math::Vector3& origin,
                                             const math::Vector3& forward,
                                             float reach,
                                             std::uintptr_t target_pawn,
                                             std::uintptr_t local_pawn) const {
  const auto end = origin + forward * reach;
  const auto knife_filter =
      systems::g_tracing.MakeFilter(local_pawn, 0x0c3001, 4);
  auto result = systems::g_tracing.Trace(origin, end, knife_filter);

  if ((result.fraction < 1.0f || result.all_solid) &&
      result.hit_entity == target_pawn) {
    return result;
  }

  const auto weapon_filter =
      systems::g_tracing.MakeFilter(local_pawn, 0x0c3001, 4, 15);
  result = systems::g_tracing.Trace(origin, end, weapon_filter);

  if ((result.fraction < 1.0f || result.all_solid) &&
      result.hit_entity == target_pawn) {
    return result;
  }

  for (auto radius = 14.0f; radius > 0.0f; radius -= 3.0f) {
    const auto sweep_end = end - forward * radius;
    result = systems::g_tracing.TraceSphere(origin, sweep_end, radius,
                                            weapon_filter);

    if ((result.fraction < 1.0f || result.all_solid) &&
        result.hit_entity == target_pawn) {
      return result;
    }
  }

  result.fraction = 1.0f;
  result.hit_entity = 0;
  return result;
}

void Rage::UpdatePenetrationCrosshair(const systems::Local::Snapshot& local) {
  const auto& cfg = settings::g_combat.m_penetration_crosshair;
  const auto& ctx = g_shared.Ctx();

  if (!cfg.enabled.value || !ctx.valid || !local.is_alive || local.team < 2 ||
      !local.pawn || !ctx.weapon ||
      ctx.weapon_type < cstypes::weapon_type::kPistol ||
      ctx.weapon_type > cstypes::weapon_type::kLmg) {
    this->m_penetration_crosshair_state_.store(
        PenetrationCrosshairState::kUnavailable, std::memory_order_relaxed);
    return;
  }

  // get_shoot_position() can be zero before prediction has populated the
  // weapon-services shoot history. The crosshair needs the current eye now.
  const auto eye_pos = g_shared.GetEyePosition(local.pawn);
  auto view_angles = systems::g_input.GetViewAngles();
  const auto aim_punch = g_shared.GetAimPunch(local.pawn);
  view_angles.x += aim_punch.x;
  view_angles.y += aim_punch.y;

  math::Vector3 forward{};
  math::helpers::AngleVectorsLeft(view_angles, &forward);

  auto pen_damage{0.0f};
  const auto can_pen = g_shared.Pen().Can(eye_pos, forward, pen_damage, local);
  this->m_penetration_crosshair_state_.store(
      can_pen ? PenetrationCrosshairState::kPenetrable
              : PenetrationCrosshairState::kBlocked,
      std::memory_order_relaxed);
}

void Rage::DrawPenetrationCrosshair(xdraw::draw_list& draw_list) const {
  const auto& cfg = settings::g_combat.m_penetration_crosshair;
  if (!cfg.enabled.value) {
    return;
  }

  const auto state =
      this->m_penetration_crosshair_state_.load(std::memory_order_relaxed);
  const auto local = systems::g_local.Get();
  if (state == PenetrationCrosshairState::kUnavailable || !local.is_alive ||
      systems::g_local.IsInCinematic()) {
    return;
  }

  const auto can_pen = state == PenetrationCrosshairState::kPenetrable;

  const auto& fill = can_pen ? cfg.can_penetrate_fill : cfg.blocked_fill;
  const auto& outline =
      can_pen ? cfg.can_penetrate_outline : cfg.blocked_outline;
  const auto [screen_w, screen_h] = xdraw::viewport_size();
  const auto cx = std::floorf(static_cast<float>(screen_w) * 0.5f);
  const auto cy = std::floorf(static_cast<float>(screen_h) * 0.5f);
  constexpr auto kHalfSize{3.0f};
  constexpr auto kOutlineSize{1.0f};

  if (cfg.glow) {
    auto& glow = xdraw::get_glow();
    const auto glow_a = static_cast<std::uint8_t>(
        static_cast<float>(outline.value.a) * cfg.glow_strength);
    const auto glow_col =
        xdraw::color{outline.value.r, outline.value.g, outline.value.b, glow_a};

    glow.rect_filled(cx - kHalfSize - kOutlineSize,
                     cy - kHalfSize - kOutlineSize,
                     (kHalfSize + kOutlineSize) * 2.0f,
                     (kHalfSize + kOutlineSize) * 2.0f, glow_col);
  }

  draw_list.rect_filled(cx - kHalfSize - kOutlineSize,
                        cy - kHalfSize - kOutlineSize,
                        (kHalfSize + kOutlineSize) * 2.0f,
                        (kHalfSize + kOutlineSize) * 2.0f, outline);
  draw_list.rect_filled(cx - kHalfSize, cy - kHalfSize, kHalfSize * 2.0f,
                        kHalfSize * 2.0f, fill);
}

}  // namespace features::combat
