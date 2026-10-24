module;

#include "platform/platform.h"

#include <ShlObj.h>

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

namespace features::misc {

namespace detail {

constexpr std::uint32_t kInvalidParticleEffect{static_cast<std::uint32_t>(-1)};
constexpr std::uint8_t kPeriwinkleStartR{130};
constexpr std::uint8_t kPeriwinkleStartG{160};
constexpr std::uint8_t kPeriwinkleStartB{240};
constexpr std::uint8_t kPeriwinkleEndR{200};
constexpr std::uint8_t kPeriwinkleEndG{220};
constexpr std::uint8_t kPeriwinkleEndB{255};

[[nodiscard]] std::string ChatWhite(std::string_view text) {
  return std::format("<font color='#FFFFFF'>{}</font>", text);
}

[[nodiscard]] std::string ChatDim(std::string_view text) {
  return std::format("<font color='#CCCCCC'>{}</font>", text);
}

[[nodiscard]] std::string FormatHitChatMessage(const std::string& name,
                                               int damage,
                                               const std::string& hitgroup,
                                               int health,
                                               const std::string& reason = {}) {
  const auto damage_str = std::to_string(damage);

  if (!reason.empty()) {
    return ChatDim("hit ") + ChatWhite(name) + ChatDim(" for ") +
           ChatWhite(damage_str) + ChatDim(" in ") + ChatWhite(hitgroup) +
           ChatDim(std::format(", {} ({} remaining)", reason, health));
  }

  return ChatDim("hit ") + ChatWhite(name) + ChatDim(" for ") +
         ChatWhite(damage_str) + ChatDim(" in ") + ChatWhite(hitgroup) +
         ChatDim(std::format(" ({} remaining)", health));
}

[[nodiscard]] std::string FormatKnifeChatMessage(const std::string& name,
                                                 int damage, int health) {
  return ChatDim("knifed ") + ChatWhite(name) + ChatDim(" for ") +
         ChatWhite(std::to_string(damage)) +
         ChatDim(std::format(" ({} remaining)", health));
}

[[nodiscard]] std::string FormatTaserChatMessage(const std::string& name) {
  return ChatDim("zapped the fuck out of ") + ChatWhite(name);
}

[[nodiscard]] std::string MakeGradientLabel(const char* text, std::uint8_t sr,
                                            std::uint8_t sg, std::uint8_t sb,
                                            std::uint8_t er, std::uint8_t eg,
                                            std::uint8_t eb) {
  const auto len = std::strlen(text);
  if (len == 0) {
    return {};
  }

  std::string result{};
  result.reserve(len * 40);

  for (auto i = 0ull; i < len; ++i) {
    const auto t =
        len > 1 ? static_cast<float>(i) / static_cast<float>(len - 1) : 0.0f;
    const auto r = static_cast<std::uint8_t>(sr + (er - sr) * t);
    const auto g = static_cast<std::uint8_t>(sg + (eg - sg) * t);
    const auto b = static_cast<std::uint8_t>(sb + (eb - sb) * t);

    char tag[48];
    std::snprintf(tag, sizeof(tag), "<font color='#%02X%02X%02X'>%c</font>", r,
                  g, b, text[i]);
    result += tag;
  }

  return result;
}

void ChatPrint(const char* label_text, std::uint8_t sr, std::uint8_t sg,
               std::uint8_t sb, std::uint8_t er, std::uint8_t eg,
               std::uint8_t eb, const char* msg) {
  const auto local = systems::g_local.Get();
  if (!local.IsValid() || !local.is_alive || systems::g_local.IsInCinematic() ||
      !local.pawn) {
    return;
  }

  const auto hud_element = memory::Call<std::uintptr_t>(
      PATTERN(patterns::find_hud_element), xs("CCSGO_HudVoiceStatus"));
  if (!hud_element) {
    return;
  }

  const auto voice = hud_element - 32;
  const auto label = MakeGradientLabel(label_text, sr, sg, sb, er, eg, eb);

  char buf[1024];
  std::snprintf(buf, sizeof(buf), "%s <font color='#CCCCCC'>- </font>%s",
                label.c_str(), msg);

  std::uint8_t flags[2]{1, 0};
  memory::Call<void>(PATTERN(patterns::set_voice_data), voice, buf, 0xFFFFFFFF,
                     flags);
}

void ChatPrintVelocity(const char* msg) {
  ChatPrint("[velocity]", kPeriwinkleStartR, kPeriwinkleStartG,
            kPeriwinkleStartB, kPeriwinkleEndR, kPeriwinkleEndG,
            kPeriwinkleEndB, msg);
}

}  // namespace detail

void Impacts::OnRenderEarly(xdraw::draw_list& draw_list) {
  const auto global_vars =
      memory::Read<std::uintptr_t>(addresses::globals::global_vars);
  const auto current_time = memory::Read<float>(global_vars + 0x30);

  this->RenderHitEffect(draw_list, current_time);
  this->RenderBulletImpactOverlays(draw_list, current_time);
}

void Impacts::OnFrameStageNotify() {
  // Frame-stage updates can overlap Present, which renders the same impact
  // vectors.
  std::unique_lock lock(this->m_mtx_);

  if (this->m_buffered_impact_time_ > 0.0f) {
    this->FlushBufferedImpacts();
    this->m_buffered_impacts_.clear();
    this->m_buffered_impact_time_ = -1.0f;
  }
}

void Impacts::OnLevelChange() {
  std::unique_lock lock(this->m_mtx_);

  this->m_hitmarkers_.clear();
  this->m_logs_.clear();
  this->m_pending_hits_.clear();
  this->m_pending_shots_.clear();
  this->m_bullet_impacts_.clear();
  this->m_buffered_impacts_.clear();
  this->m_buffered_impact_time_ = -1.0f;
  this->m_hit_effect_time_ = 0.0f;
}

void Impacts::OnRender(xdraw::draw_list& draw_list) {
  const auto global_vars =
      memory::Read<std::uintptr_t>(addresses::globals::global_vars);
  const auto current_time = memory::Read<float>(global_vars + 0x30);

  {
    std::unique_lock lock(this->m_mtx_);
    this->CheckMisses();
  }

  this->RenderHitMarkers(draw_list, current_time);
  this->RenderLogs(draw_list, current_time);
}

void Impacts::OnReportHit(std::uintptr_t msg) {
  const auto& cfg = settings::g_misc.m_impacts;

  if (!cfg.hit_marker.value && !cfg.hit_sound.value && !cfg.hit_effect.value) {
    return;
  }

  const auto position = memory::Read<math::Vector3>(msg + 0x18);
  if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
      !std::isfinite(position.z) || position.LengthSqr() < 1.0f) {
    return;
  }

  const auto global_vars =
      memory::Read<std::uintptr_t>(addresses::globals::global_vars);
  const auto current_time = memory::Read<float>(global_vars + 0x30);

  std::unique_lock lock(this->m_mtx_);

  this->m_pending_hits_.push_back({position, current_time});

  if (this->m_pending_hits_.size() > 10) {
    this->m_pending_hits_.erase(this->m_pending_hits_.begin());
  }
}

void Impacts::OnPlayerHurt(std::uintptr_t event) {
  if (!event) {
    return;
  }

  const auto data = this->ParseEvent(event);
  if (!data.victim_pawn) {
    return;
  }

  const auto& cfg = settings::g_misc.m_impacts;
  const auto is_kill = data.health <= 0;

  if (cfg.hit_marker) {
    const auto global_vars =
        memory::Read<std::uintptr_t>(addresses::globals::global_vars);
    const auto current_time = memory::Read<float>(global_vars + 0x30);
    auto position = math::Vector3{};
    auto has_position{false};

    // Report-hit contains the most accurate contact point, but it is not
    // guaranteed to arrive before player_hurt. Use it when available and
    // fall back to the confirmed shot impact or the victim's position.
    const auto game_scene_node = memory::Read<std::uintptr_t>(
        data.victim_pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
    if (game_scene_node) {
      const auto origin = memory::Read<math::Vector3>(
          game_scene_node + SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_hash));
      const auto view_offset = memory::Read<math::Vector3>(
          data.victim_pawn +
          SCHEMA("C_BaseModelEntity", "m_vecViewOffset"_hash));

      position = origin + view_offset * 0.75f;
      has_position = true;
    }

    std::unique_lock lock(this->m_mtx_);

    auto best_idx{SIZE_MAX};
    auto best_diff{0.5f};

    for (auto i = 0ull; i < this->m_pending_hits_.size(); ++i) {
      const auto diff = std::abs(current_time - this->m_pending_hits_[i].time);
      if (diff < best_diff) {
        best_diff = diff;
        best_idx = i;
      }
    }

    if (best_idx != SIZE_MAX) {
      position = this->m_pending_hits_[best_idx].position;
      has_position = true;
      this->m_pending_hits_.erase(this->m_pending_hits_.begin() + best_idx);
    } else {
      for (auto it = this->m_pending_shots_.rbegin();
           it != this->m_pending_shots_.rend(); ++it) {
        if (it->victim_pawn == data.victim_pawn && it->impact_confirmed &&
            current_time - it->impact_time <= 1.0f) {
          position = it->impact_position;
          has_position = true;
          break;
        }
      }
    }

    if (has_position) {
      this->m_hitmarkers_.push_back({position, current_time, data.damage});

      if (this->m_hitmarkers_.size() > 10) {
        this->m_hitmarkers_.erase(this->m_hitmarkers_.begin());
      }
    }

    std::erase_if(this->m_pending_hits_, [&](const auto& entry) {
      return current_time - entry.time > 0.5f;
    });
  }

  if (is_kill && cfg.death_sound.value) {
    this->PlaySound(cfg.death_sound_type, cfg.death_sound_volume,
                    cfg.custom_death_sound.value);
  }

  if (cfg.hit_sound.value) {
    this->PlaySound(cfg.hit_sound_type, cfg.hit_sound_volume,
                    cfg.custom_hit_sound.value);
  }

  if (is_kill && cfg.death_effect.value) {
    this->PlayDeathEffect(data.victim_pawn);
  }

  if (cfg.hit_effect.value) {
    this->PlayHitEffect(data.victim_pawn);
  }

  if (cfg.hit_log.value) {
    this->AddHitLog(data);
  }
}

void Impacts::OnBulletImpact(std::uintptr_t event) {
  if (!event) {
    return;
  }

  const auto userid_key = cstypes::EventHash{0, "userid"};
  const auto controller = memory::Call<std::uintptr_t>(
      PATTERN(patterns::game_event_get_controller), event, &userid_key);

  if (controller != systems::g_local.Get().controller) {
    return;
  }

  const auto x = memory::Call<float>(PATTERN(patterns::game_event_get_float),
                                     event, "x", 0.0f);
  const auto y = memory::Call<float>(PATTERN(patterns::game_event_get_float),
                                     event, "y", 0.0f);
  const auto z = memory::Call<float>(PATTERN(patterns::game_event_get_float),
                                     event, "z", 0.0f);

  {
    std::unique_lock lock(this->m_mtx_);
    const auto pos = math::Vector3{x, y, z};
    const auto global_vars =
        memory::Read<std::uintptr_t>(addresses::globals::global_vars);
    const auto current_time = memory::Read<float>(global_vars + 0x30);
    ShotRecord* matched_shot{};
    auto best_score{-FLT_MAX};

    // Server shot callbacks and bullet-impact events preserve command order.
    // Match the oldest shot that does not have an impact before considering
    // extra impacts from an already matched shot.
    for (auto& shot : this->m_pending_shots_) {
      if (shot.resolved || shot.impact_confirmed) {
        continue;
      }

      const auto age = current_time - shot.time;
      if (age < -0.05f || age > 1.0f) {
        continue;
      }

      const auto origin = shot.server_shoot_position_confirmed
                              ? shot.server_shoot_position
                              : shot.shoot_position;
      const auto impact_delta = pos - origin;
      if (impact_delta.LengthSqr() < 1.0f) {
        continue;
      }

      math::Vector3 expected_direction{};
      math::helpers::AngleVectorsLeft(shot.aim_angle, &expected_direction);
      const auto alignment = expected_direction.Dot(impact_delta.Normalized());
      if (alignment > -0.25f) {
        matched_shot = &shot;
        break;
      }
    }

    if (!matched_shot) {
      for (auto& shot : this->m_pending_shots_) {
        if (shot.resolved || !shot.impact_confirmed) {
          continue;
        }

        const auto age = current_time - shot.time;
        if (age < -0.05f || age > 1.0f) {
          continue;
        }

        const auto origin = shot.server_shoot_position_confirmed
                                ? shot.server_shoot_position
                                : shot.shoot_position;
        const auto impact_delta = pos - origin;
        if (impact_delta.LengthSqr() < 1.0f) {
          continue;
        }

        math::Vector3 expected_direction{};
        math::helpers::AngleVectorsLeft(shot.aim_angle, &expected_direction);
        const auto score =
            expected_direction.Dot(impact_delta.Normalized()) * 4.0f -
            std::fabs(age);

        if (score > best_score) {
          best_score = score;
          matched_shot = &shot;
        }
      }
    }

    if (matched_shot) {
      const auto target_origin = matched_shot->skeleton[0].position;
      const auto dist_sq = (pos - target_origin).LengthSqr();

      if (!matched_shot->impact_confirmed ||
          dist_sq < matched_shot->best_impact_dist_sq) {
        matched_shot->impact_position = pos;
        matched_shot->best_impact_dist_sq = dist_sq;
        matched_shot->impact_time = current_time;
        matched_shot->impact_confirmed = true;
      }
    }

    this->CheckMisses();

    const auto& cfg = settings::g_misc.m_impacts;
    if (cfg.bullet_impact_effect.value || cfg.bullet_tracers.value) {
      const auto type = cfg.bullet_impact_effect_type.value;
      const auto show_overlay =
          type == settings::Misc::Impacts::BulletImpactType::kOverlay ||
          type == settings::Misc::Impacts::BulletImpactType::kBoth;

      if (cfg.bullet_tracers.value || show_overlay) {
        const auto global_vars =
            memory::Read<std::uintptr_t>(addresses::globals::global_vars);
        const auto current_time = memory::Read<float>(global_vars + 0x30);

        if (current_time != this->m_buffered_impact_time_) {
          this->FlushBufferedImpacts();

          this->m_buffered_impact_time_ = current_time;
          this->m_buffered_impacts_.clear();

          const auto local_pawn = systems::g_local.Get().pawn;
          if (local_pawn) {
            const auto game_scene_node = memory::Read<std::uintptr_t>(
                local_pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
            const auto origin = memory::Read<math::Vector3>(
                game_scene_node +
                SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_hash));
            const auto view_offset = memory::Read<math::Vector3>(
                local_pawn +
                SCHEMA("C_BaseModelEntity", "m_vecViewOffset"_hash));
            this->m_buffered_eye_position_ = origin + view_offset;
          }
        }

        this->m_buffered_impacts_.push_back(math::Vector3{x, y, z});
      }
    }
  }

  const auto& cfg = settings::g_misc.m_impacts;
  if (cfg.bullet_impact_effect.value) {
    const auto type = cfg.bullet_impact_effect_type.value;
    const auto show_sparks =
        type == settings::Misc::Impacts::BulletImpactType::kSparks ||
        type == settings::Misc::Impacts::BulletImpactType::kBoth;

    if (show_sparks) {
      this->PlayBulletImpactEffect(math::Vector3{x, y, z});
    }
  }
}

void Impacts::OnBaseFireGunsGetInaccuracy(std::uintptr_t weapon,
                                          float inaccuracy) {
  const auto owner_handle = memory::Read<std::uint32_t>(
      weapon + SCHEMA("C_BaseEntity", "m_hOwnerEntity"_hash));
  const auto owner = systems::g_entities.Lookup(owner_handle);

  if (!owner || owner != systems::g_local.Get().pawn) {
    return;
  }

  std::unique_lock lock(this->m_mtx_);

  for (auto it = this->m_pending_shots_.begin();
       it != this->m_pending_shots_.end(); ++it) {
    if (!it->server_confirmed) {
      it->server_inaccuracy = inaccuracy;
      it->server_confirmed = true;
      break;
    }
  }
}

void Impacts::OnGetInterpolatedShootPosition(std::uintptr_t weapon_services,
                                             float* out) {
  const auto local_pawn = systems::g_local.Get().pawn;
  if (!local_pawn) {
    return;
  }

  const auto local_weapon_services = memory::Read<std::uintptr_t>(
      local_pawn + SCHEMA("C_BasePlayerPawn", "m_pWeaponServices"_hash));
  if (!local_weapon_services || weapon_services != local_weapon_services) {
    return;
  }

  const auto shoot_position = math::Vector3{out[0], out[1], out[2]};

  std::unique_lock lock(this->m_mtx_);

  for (auto& shot : this->m_pending_shots_) {
    if (!shot.resolved && !shot.server_shoot_position_confirmed) {
      shot.server_shoot_position = shoot_position;
      shot.server_shoot_position_confirmed = true;
      break;
    }
  }
}

void Impacts::OnBoom(std::uintptr_t victim_pawn, int hitgroup, float damage,
                     float hitchance, float inaccuracy, float spread,
                     const math::Vector3& aim_angle,
                     const math::Vector3& shoot_position, int tick,
                     const std::array<systems::Bones::Data, 27>& skeleton,
                     bool forced) {
  const auto global_vars =
      memory::Read<std::uintptr_t>(addresses::globals::global_vars);
  const auto current_time = memory::Read<float>(global_vars + 0x30);

  std::unique_lock lock(this->m_mtx_);

  const auto target_velocity = memory::Read<math::Vector3>(
      victim_pawn + SCHEMA("C_BaseEntity", "m_vecVelocity"_hash));

  this->m_pending_shots_.push_back({
      .victim_pawn = victim_pawn,
      .hitgroup = hitgroup,
      .damage = damage,
      .hitchance = hitchance,
      .predicted_inaccuracy = inaccuracy,
      .predicted_spread = spread,
      .server_inaccuracy = 0.0f,
      .aim_angle = aim_angle,
      .shoot_position = shoot_position,
      .tick = tick,
      .time = current_time,
      .skeleton = skeleton,
      .resolved = false,
      .server_confirmed = false,
      .impact_confirmed = false,
      .target_velocity = target_velocity,
      .forced = forced,
      .weapon_type = features::combat::g_shared.Ctx().weapon_type,
  });

  if (this->m_pending_shots_.size() > 10) {
    this->m_pending_shots_.erase(this->m_pending_shots_.begin());
  }
}

const char* Impacts::ClassifyShotDeviation(const ShotRecord& shot) const {
  if (!shot.impact_confirmed) {
    return "death";
  }

  if (shot.server_confirmed &&
      std::fabsf(shot.server_inaccuracy - shot.predicted_inaccuracy) > 0.003f) {
    return "prediction error";
  }

  if (shot.server_shoot_position_confirmed &&
      (shot.server_shoot_position - shot.shoot_position).LengthSqr() > 1.0f) {
    return "shoot position mismatch";
  }

  const auto shoot_position = shot.server_shoot_position_confirmed
                                  ? shot.server_shoot_position
                                  : shot.shoot_position;

  math::Vector3 ideal_forward{};
  math::helpers::AngleVectorsLeft(shot.aim_angle, &ideal_forward);

  const auto to_impact = shot.impact_position - shoot_position;
  const auto impact_dist = to_impact.Length();

  if (impact_dist <= 0.1f) {
    return "impact too close to origin (likely penetration)";
  }

  const auto impact_dir = to_impact * (1.0f / impact_dist);
  const auto dot = ideal_forward.Dot(impact_dir);
  const auto angular_deviation = std::acosf(std::clamp(dot, -1.0f, 1.0f));

  const auto inaccuracy = shot.server_confirmed ? shot.server_inaccuracy
                                                : shot.predicted_inaccuracy;
  const auto max_spread_angle = std::atanf(
      std::max(inaccuracy, 0.0f) + std::max(shot.predicted_spread, 0.0f));
  const auto impact_mismatch_angle =
      std::max(max_spread_angle * 3.0f, math::helpers::DegToRad(2.0f));

  const auto hitbox_dist = this->DistanceToNearestHitbox(shot);
  const auto ray_dist = this->RayDistanceToNearestHitbox(shot, impact_dir);
  const auto ideal_ray_dist =
      this->RayDistanceToNearestHitbox(shot, ideal_forward);
  const auto target_dist =
      (shot.skeleton[0].position - shoot_position).Length();

  // The scalar cone is only an estimate of the server's shot state. Reserve
  // this label for an impact that clearly belongs to another direction;
  // smaller deviations are classified from their hitbox geometry below.
  if (angular_deviation > impact_mismatch_angle) {
    return "impact mismatch";
  }

  if (ideal_ray_dist <= 1.0f && impact_dist < target_dist * 0.85f) {
    return "occlusion";
  }

  // If the server impact ray misses the saved hitboxes, weapon spread is
  // already sufficient to explain the miss. Do not blame lag compensation
  // merely because it passed within several units of the target.
  if (ray_dist > 1.0f) {
    return "spread";
  }

  if (impact_dist > target_dist * 1.05f) {
    if (shot.target_velocity.Length2d() < 5.0f) {
      return "server discrepancy";
    }

    return "lag compensation";
  }

  if (hitbox_dist > 16.0f) {
    return "spread";
  }

  return "server discrepancy";
}

Impacts::HitData Impacts::ParseEvent(std::uintptr_t event) {
  const auto attacker_key = cstypes::EventHash{0, "attacker"};
  const auto userid_key = cstypes::EventHash{0, "userid"};

  const auto attacker = memory::Call<std::uintptr_t>(
      PATTERN(patterns::game_event_get_controller), event, &attacker_key);
  const auto victim = memory::Call<std::uintptr_t>(
      PATTERN(patterns::game_event_get_controller), event, &userid_key);

  const auto local = systems::g_local.Get();

  if (attacker != local.controller || victim == local.controller) {
    return {};
  }

  const auto victim_pawn = memory::Call<std::uintptr_t>(
      PATTERN(patterns::game_event_get_pawn), event, &userid_key);
  if (!victim_pawn) {
    return {};
  }

  const auto victim_team = memory::Read<int>(
      victim_pawn + SCHEMA("C_BaseEntity", "m_iTeamNum"_hash));
  if (!local.IsThisOtherTeam(victim_team)) {
    return {};
  }

  const auto damage = memory::Call<int>(PATTERN(patterns::game_event_get_int),
                                        event, "dmg_health", false);
  const auto hitgroup = memory::Call<int>(PATTERN(patterns::game_event_get_int),
                                          event, "hitgroup", false);

  auto expected_hitgroup{0};
  auto expected_damage{0.0f};
  auto was_aimbot{false};
  auto weapon_type{0u};
  std::string mismatch_reason{};
  math::Vector3 impact_pos{};

  {
    std::unique_lock lock(this->m_mtx_);

    if (!this->m_pending_hits_.empty()) {
      impact_pos = this->m_pending_hits_.back().position;
    }

    auto matched_shot = this->m_pending_shots_.end();
    for (auto it = this->m_pending_shots_.begin();
         it != this->m_pending_shots_.end(); ++it) {
      if (it->victim_pawn != victim_pawn || it->resolved) {
        continue;
      }

      if (matched_shot == this->m_pending_shots_.end() ||
          (it->impact_confirmed &&
           (!matched_shot->impact_confirmed ||
            it->impact_time > matched_shot->impact_time))) {
        matched_shot = it;
      }
    }

    if (matched_shot != this->m_pending_shots_.end()) {
      expected_hitgroup = matched_shot->hitgroup;
      was_aimbot = true;
      weapon_type = matched_shot->weapon_type;
      expected_damage = matched_shot->damage;
      matched_shot->resolved = true;

      if (expected_hitgroup > 0 && hitgroup != expected_hitgroup) {
        mismatch_reason = this->ClassifyShotDeviation(*matched_shot);
      }
    }
  }

  if (!was_aimbot) {
    if (local.pawn) {
      const auto weapon_services = memory::Read<std::uintptr_t>(
          local.pawn + SCHEMA("C_BasePlayerPawn", "m_pWeaponServices"_hash));
      if (weapon_services) {
        const auto weapon_handle = memory::Read<std::uint32_t>(
            weapon_services +
            SCHEMA("CPlayer_WeaponServices", "m_hActiveWeapon"_hash));
        const auto weapon =
            weapon_handle ? systems::g_entities.Lookup(weapon_handle) : 0;

        if (weapon) {
          const auto weapon_vdata = memory::Read<std::uintptr_t>(
              weapon + SCHEMA("C_BaseEntity", "m_nSubclassID"_hash) + 0x8);
          if (weapon_vdata) {
            weapon_type = memory::Read<std::uint32_t>(
                weapon_vdata +
                SCHEMA("CCSWeaponBaseVData", "m_WeaponType"_hash));
          }
        }
      }
    }
  }

  return {.victim = victim,
          .victim_pawn = victim_pawn,
          .damage = damage,
          .health = memory::Call<int>(PATTERN(patterns::game_event_get_int),
                                      event, "health", false),
          .hitgroup = hitgroup,
          .expected_hitgroup = expected_hitgroup,
          .was_aimbot = was_aimbot,
          .mismatch_reason = std::move(mismatch_reason),
          .weapon_type = weapon_type,
          .expected_damage = expected_damage};
}

std::string Impacts::GetPlayerName(std::uintptr_t controller) {
  const auto name_ptr = memory::Read<std::uintptr_t>(
      controller +
      SCHEMA("CCSPlayerController", "m_sSanitizedPlayerName"_hash));
  if (!name_ptr) {
    return "unknown";
  }

  auto name = memory::ReadString(name_ptr, 64);

  std::transform(name.begin(), name.end(), name.begin(), ::tolower);

  return name;
}

std::string Impacts::GetPlayerNameFromPawn(std::uintptr_t pawn) {
  const auto controller_handle = memory::Read<std::uint32_t>(
      pawn + SCHEMA("C_BasePlayerPawn", "m_hController"_hash));
  if (!controller_handle) {
    return "unknown";
  }

  const auto controller = systems::g_entities.Lookup(controller_handle);
  if (!controller) {
    return "unknown";
  }

  return this->GetPlayerName(controller);
}

float Impacts::DistanceToNearestHitbox(const ShotRecord& shot) const {
  if (shot.impact_position.LengthSqr() < 1.0f) {
    return -1.0f;
  }

  const auto game_scene_node = memory::Read<std::uintptr_t>(
      shot.victim_pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
  if (!game_scene_node) {
    return -1.0f;
  }

  const auto hitbox_set = systems::g_hitboxes.Query(game_scene_node);
  if (hitbox_set.count <= 0) {
    return -1.0f;
  }

  auto best_dist{FLT_MAX};

  for (const auto& entry : hitbox_set) {
    if (entry.bone < 0 ||
        entry.bone >= static_cast<int>(shot.skeleton.size())) {
      continue;
    }

    const auto& bone = shot.skeleton[entry.bone];
    if (bone.position.LengthSqr() < 1.0f) {
      continue;
    }

    auto dist{FLT_MAX};
    if (entry.radius > 0.001f) {
      const auto capsule_start =
          bone.rotation.RotateVector(entry.mins) + bone.position;
      const auto capsule_end =
          bone.rotation.RotateVector(entry.maxs) + bone.position;
      const auto segment = capsule_end - capsule_start;
      const auto segment_length_sq = segment.LengthSqr();
      const auto t =
          segment_length_sq > 0.001f
              ? std::clamp((shot.impact_position - capsule_start).Dot(segment) /
                               segment_length_sq,
                           0.0f, 1.0f)
              : 0.0f;
      dist = (shot.impact_position - (capsule_start + segment * t)).Length() -
             entry.radius;
    } else {
      auto inverse = bone.rotation;
      inverse.x = -inverse.x;
      inverse.y = -inverse.y;
      inverse.z = -inverse.z;
      const auto local =
          inverse.RotateVector(shot.impact_position - bone.position);
      const math::Vector3 closest{
          std::clamp(local.x, entry.mins.x, entry.maxs.x),
          std::clamp(local.y, entry.mins.y, entry.maxs.y),
          std::clamp(local.z, entry.mins.z, entry.maxs.z)};
      dist = (local - closest).Length();
    }

    if (dist < best_dist) {
      best_dist = dist;
    }
  }

  return best_dist;
}

float Impacts::RayDistanceToNearestHitbox(
    const ShotRecord& shot, const math::Vector3& direction) const {
  const auto game_scene_node = memory::Read<std::uintptr_t>(
      shot.victim_pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
  if (!game_scene_node) {
    return FLT_MAX;
  }

  const auto hitbox_set = systems::g_hitboxes.Query(game_scene_node);
  if (hitbox_set.count <= 0) {
    return FLT_MAX;
  }

  auto best_dist{FLT_MAX};
  const auto shoot_position = shot.server_shoot_position_confirmed
                                  ? shot.server_shoot_position
                                  : shot.shoot_position;

  for (const auto& entry : hitbox_set) {
    if (entry.bone < 0 ||
        entry.bone >= static_cast<int>(shot.skeleton.size())) {
      continue;
    }

    const auto& bone = shot.skeleton[entry.bone];
    if (bone.position.LengthSqr() < 1.0f) {
      continue;
    }

    auto dist{FLT_MAX};
    if (entry.radius > 0.001f) {
      const auto capsule_start =
          bone.rotation.RotateVector(entry.mins) + bone.position;
      const auto capsule_end =
          bone.rotation.RotateVector(entry.maxs) + bone.position;
      const auto segment = capsule_end - capsule_start;
      const auto to_start = capsule_start - shoot_position;
      const auto perpendicular_segment =
          segment - direction * segment.Dot(direction);
      const auto perpendicular_start =
          to_start - direction * to_start.Dot(direction);
      const auto perpendicular_length_sq = perpendicular_segment.LengthSqr();
      auto segment_t =
          perpendicular_length_sq > 1.0e-6f
              ? std::clamp(-perpendicular_start.Dot(perpendicular_segment) /
                               perpendicular_length_sq,
                           0.0f, 1.0f)
              : 0.0f;
      auto segment_point = capsule_start + segment * segment_t;
      auto ray_t = (segment_point - shoot_position).Dot(direction);

      if (ray_t < 0.0f) {
        const auto segment_length_sq = segment.LengthSqr();
        segment_t =
            segment_length_sq > 1.0e-6f
                ? std::clamp((shoot_position - capsule_start).Dot(segment) /
                                 segment_length_sq,
                             0.0f, 1.0f)
                : 0.0f;
        segment_point = capsule_start + segment * segment_t;
        ray_t = 0.0f;
      }

      const auto ray_point = shoot_position + direction * ray_t;
      dist = (segment_point - ray_point).Length() - entry.radius;
    } else {
      auto inverse = bone.rotation;
      inverse.x = -inverse.x;
      inverse.y = -inverse.y;
      inverse.z = -inverse.z;
      const auto local_origin =
          inverse.RotateVector(shoot_position - bone.position);
      const auto local_direction = inverse.RotateVector(direction);
      auto entry_t{0.0f};
      auto exit_t{FLT_MAX};

      const auto intersect_axis = [&](float origin, float delta, float minimum,
                                      float maximum) {
        if (std::fabs(delta) < 1.0e-8f) {
          return origin >= minimum && origin <= maximum;
        }

        auto first = (minimum - origin) / delta;
        auto second = (maximum - origin) / delta;
        if (first > second) std::swap(first, second);
        entry_t = std::max(entry_t, first);
        exit_t = std::min(exit_t, second);
        return entry_t <= exit_t;
      };

      if (intersect_axis(local_origin.x, local_direction.x, entry.mins.x,
                         entry.maxs.x) &&
          intersect_axis(local_origin.y, local_direction.y, entry.mins.y,
                         entry.maxs.y) &&
          intersect_axis(local_origin.z, local_direction.z, entry.mins.z,
                         entry.maxs.z)) {
        dist = 0.0f;
      } else {
        const auto center = (entry.mins + entry.maxs) * 0.5f;
        const auto extents = (entry.maxs - entry.mins) * 0.5f;
        const auto to_center = center - local_origin;
        const auto projection = std::max(to_center.Dot(local_direction), 0.0f);
        dist = std::max((to_center - local_direction * projection).Length() -
                            extents.Length(),
                        0.0f);
      }
    }

    if (dist < best_dist) {
      best_dist = dist;
    }
  }

  return best_dist;
}

void Impacts::AddHitLog(const HitData& data) {
  const auto global_vars =
      memory::Read<std::uintptr_t>(addresses::globals::global_vars);
  const auto current_time = memory::Read<float>(global_vars + 0x30);
  const auto& cfg = settings::g_misc.m_impacts;

  std::unique_lock lock(this->m_mtx_);

  Log entry{};
  entry.name = this->GetPlayerName(data.victim);
  entry.damage = data.damage;
  entry.health = data.health;
  entry.time = current_time;
  entry.offset.SetStiffness(180.0f);
  entry.offset.SetDamping(16.0f);
  entry.offset.Snap(-150.0f);
  entry.offset.SetTarget(0.0f);
  entry.alpha.FadeIn(0.15f);
  entry.snapped = false;
  entry.duration = cfg.hit_log_duration;
  entry.hitgroup = systems::g_hitboxes.HitgroupToName(data.hitgroup);
  entry.weapon_type = data.weapon_type;

  if (data.was_aimbot && !data.mismatch_reason.empty()) {
    const auto expected_name =
        systems::g_hitboxes.HitgroupToName(data.expected_hitgroup);
    entry.reason = std::format("{} was expected ({})", expected_name,
                               data.mismatch_reason);
  } else if (data.was_aimbot && data.health > 0 &&
             data.expected_damage > 0.0f &&
             static_cast<float>(data.damage) < data.expected_damage) {
    entry.reason = std::format("expected {:.0f} damage, dealt {}",
                               data.expected_damage, data.damage);
  }

  if (cfg.console_log.value || cfg.chat_log.value) {
    std::string plain_msg{};
    std::string chat_msg{};

    if (data.weapon_type == cstypes::weapon_type::kTaser) {
      plain_msg = std::format("zapped the fuck out of {}", entry.name);
      chat_msg = detail::FormatTaserChatMessage(entry.name);
    } else if (data.weapon_type == cstypes::weapon_type::kNife) {
      plain_msg = std::format("knifed {} for {} ({} remaining)", entry.name,
                              entry.damage, entry.health);
      chat_msg = detail::FormatKnifeChatMessage(entry.name, entry.damage,
                                                entry.health);
    } else if (!entry.reason.empty()) {
      plain_msg =
          std::format("hit {} for {} in {}, {} ({} remaining)", entry.name,
                      entry.damage, entry.hitgroup, entry.reason, entry.health);
      chat_msg = detail::FormatHitChatMessage(
          entry.name, entry.damage, entry.hitgroup, entry.health, entry.reason);
    } else {
      plain_msg = std::format("hit {} for {} in {} ({} remaining)", entry.name,
                              entry.damage, entry.hitgroup, entry.health);
      chat_msg = detail::FormatHitChatMessage(entry.name, entry.damage,
                                              entry.hitgroup, entry.health);
    }

    if (cfg.console_log.value) {
      logging::console::Print(xs("{}"), plain_msg);
    }

    if (cfg.chat_log.value) {
      detail::ChatPrintVelocity(chat_msg.c_str());
    }
  }

  if (cfg.hit_log.value) {
    this->m_logs_.insert(this->m_logs_.begin(), std::move(entry));

    if (this->m_logs_.size() > 5) {
      this->m_logs_.pop_back();
    }
  }
}

void Impacts::AddMissLog(const ShotRecord& shot, const char* reason) {
  const auto global_vars =
      memory::Read<std::uintptr_t>(addresses::globals::global_vars);
  const auto current_time = memory::Read<float>(global_vars + 0x30);
  const auto& cfg = settings::g_misc.m_impacts;

  const auto name = this->GetPlayerNameFromPawn(shot.victim_pawn);
  const auto group = systems::g_hitboxes.HitgroupToName(shot.hitgroup);

  if (cfg.console_log.value || cfg.chat_log.value) {
    std::string plain_msg{};
    std::string chat_msg{};

    if (shot.weapon_type == cstypes::weapon_type::kNife ||
        shot.weapon_type == cstypes::weapon_type::kTaser) {
      if (shot.weapon_type == cstypes::weapon_type::kNife) {
        plain_msg = std::format("missed knife on {} due to latency", name);
        chat_msg = detail::ChatDim("missed knife on ") +
                   detail::ChatWhite(name) + detail::ChatDim(" due to latency");
      } else {
        plain_msg = std::format(
            "missed zeus on {} due to idk ill improve the zeusbot later jeez.",
            name);
        chat_msg =
            detail::ChatDim("missed zeus on ") + detail::ChatWhite(name) +
            detail::ChatDim(" due to idk ill improve the zeusbot later jeez.");
      }
    } else if (shot.forced) {
      plain_msg = std::format("missed {} (forced shot, {:.0f}% hitchance)",
                              name, shot.hitchance * 100.0f);
      chat_msg =
          detail::ChatDim("missed ") + detail::ChatWhite(name) +
          detail::ChatDim(std::format(" (forced shot, {:.0f}% hitchance)",
                                      shot.hitchance * 100.0f));
    } else {
      plain_msg = std::format(
          "missed {}, targeted {} (hc={:.0f}%, dmg={:.0f}, reason={})", name,
          group, shot.hitchance * 100.0f, shot.damage, reason);
      chat_msg = detail::ChatDim("missed ") + detail::ChatWhite(name) +
                 detail::ChatDim(", targeted ") + detail::ChatWhite(group) +
                 detail::ChatDim(
                     std::format(" (hc={:.0f}%, dmg={:.0f}, reason={})",
                                 shot.hitchance * 100.0f, shot.damage, reason));
    }

    if (cfg.console_log.value) {
      logging::console::Print(xs("{}"), plain_msg);
    }

    if (cfg.chat_log.value) {
      detail::ChatPrintVelocity(chat_msg.c_str());
    }
  }

  if (!cfg.miss_log.value) {
    return;
  }

  Log entry{};
  entry.name = name;

  if (shot.forced) {
    entry.reason = "forced shot";
    entry.hitgroup =
        std::format("({:.0f}% hitchance)", shot.hitchance * 100.0f);
  } else {
    entry.reason = reason;
  }

  entry.damage = 0;
  entry.health = -1;
  entry.time = current_time;
  entry.offset.SetStiffness(180.0f);
  entry.offset.SetDamping(16.0f);
  entry.offset.Snap(-150.0f);
  entry.offset.SetTarget(0.0f);
  entry.alpha.FadeIn(0.15f);
  entry.snapped = false;
  entry.is_miss = true;
  entry.duration = settings::g_misc.m_impacts.miss_log_duration;
  entry.weapon_type = shot.weapon_type;

  this->m_logs_.insert(this->m_logs_.begin(), std::move(entry));

  if (this->m_logs_.size() > 5) {
    this->m_logs_.pop_back();
  }
}

void Impacts::CheckMisses() {
  const auto global_vars =
      memory::Read<std::uintptr_t>(addresses::globals::global_vars);
  const auto current_time = memory::Read<float>(global_vars + 0x30);

  constexpr auto kHurtGracePeriod{0.35f};
  constexpr auto kAbsoluteTimeout{1.0f};

  const auto& cfg = settings::g_misc.m_impacts;

  for (auto it = this->m_pending_shots_.begin();
       it != this->m_pending_shots_.end();) {
    if (it->resolved) {
      it = this->m_pending_shots_.erase(it);
      continue;
    }

    const auto elapsed = current_time - it->time;
    const auto impact_elapsed =
        it->impact_confirmed ? (current_time - it->impact_time) : 0.0f;
    const auto is_stale =
        it->impact_confirmed && impact_elapsed > kHurtGracePeriod;
    const auto is_expired = elapsed > kAbsoluteTimeout;

    if (is_stale || is_expired) {
      // A predicted trigger command is not necessarily a shot (notably
      // while cocking the R8). Discard it unless the server fire path or
      // a bullet impact confirms that a round was emitted.
      if (!it->server_confirmed && !it->impact_confirmed) {
        it = this->m_pending_shots_.erase(it);
        continue;
      }

      it->resolved = true;

      if (cfg.miss_log.value) {
        const char* reason;

        if (it->forced) {
          reason = "forced";
        } else if (!it->impact_confirmed) {
          reason = "death";
        } else {
          reason = this->ClassifyShotDeviation(*it);
        }

        this->AddMissLog(*it, reason);
      }

      it = this->m_pending_shots_.erase(it);
      continue;
    }

    ++it;
  }
}

void Impacts::RenderHitMarkers(xdraw::draw_list& draw_list, float time) {
  const auto& cfg = settings::g_misc.m_impacts;

  std::unique_lock lock(this->m_mtx_);

  for (auto it = this->m_hitmarkers_.begin();
       it != this->m_hitmarkers_.end();) {
    const auto duration = cfg.hit_marker_duration;
    const auto elapsed = time - it->time;

    if (elapsed > duration) {
      it = this->m_hitmarkers_.erase(it);
      continue;
    }

    const auto screen = systems::g_view.Project(it->position);
    const auto progress = elapsed / duration;

    const auto ease_out = 1.0f - (progress * progress);
    const auto alpha = static_cast<std::uint8_t>(ease_out * 255.0f);
    const auto color =
        xdraw::color(cfg.hit_marker_color.value.r, cfg.hit_marker_color.value.g,
                     cfg.hit_marker_color.value.b, alpha);

    const auto x = screen.x, y = screen.y;

    const auto show_classic =
        cfg.hit_marker_type == settings::Misc::Impacts::MarkerType::kClassic ||
        cfg.hit_marker_type == settings::Misc::Impacts::MarkerType::kBoth;
    const auto show_damage =
        cfg.hit_marker_type == settings::Misc::Impacts::MarkerType::kDamage ||
        cfg.hit_marker_type == settings::Misc::Impacts::MarkerType::kBoth;

    auto size{0.0f};
    auto gap{0.0f};

    if (show_classic) {
      const auto expand_progress = std::min(elapsed * 20.0f, 1.0f);
      const auto ease_expand = 1.0f - std::pow(1.0f - expand_progress, 4.0f);

      const auto base_size{10.0f};
      const auto base_gap{3.0f};
      const auto expand_amount = 6.0f * (1.0f - ease_expand);

      size = base_size + expand_amount;
      gap = base_gap + expand_amount * 0.3f;
    }

    constexpr auto kThickness{1.25f};

    auto draw_arm = [&](xdraw::draw_list& target, float x1, float y1, float x2,
                        float y2, xdraw::color col, float thick) {
      const auto dx = x2 - x1;
      const auto dy = y2 - y1;
      const auto len = std::sqrt(dx * dx + dy * dy);

      if (len < 0.001f) {
        return;
      }

      const auto col_clear = xdraw::color{col.r, col.g, col.b, 0};
      const auto mx = (x1 + x2) * 0.5f;
      const auto my = (y1 + y2) * 0.5f;

      const float pts[]{x1, y1, mx, my, x2, y2};
      const xdraw::color cols[]{col_clear, col, col};

      target.polyline_gradient(pts, cols, false, thick);
    };

    auto damage_text = std::string{};
    auto draw_x{0.0f};
    auto draw_y{0.0f};

    if (show_damage) {
      const auto base_offset = show_classic ? 20.0f : 0.0f;

      damage_text = std::to_string(it->damage);
      const auto [text_w, text_h] = xdraw::measure_text(damage_text);

      const auto text_x = x - text_w * 0.5f;
      const auto text_y = y - base_offset - text_h * 0.5f;

      const auto shake_progress = std::max(0.0f, 1.0f - elapsed * 8.0f);
      const auto shake_x = std::sin(elapsed * 50.0f) * shake_progress * 3.0f;
      const auto shake_y = std::cos(elapsed * 45.0f) * shake_progress * 2.0f;

      draw_x = text_x + shake_x;
      draw_y = text_y + shake_y;
    }

    if (cfg.hit_marker_glow && alpha > 0) {
      auto& glow = xdraw::get_glow();
      const auto glow_a = static_cast<std::uint8_t>(
          static_cast<float>(alpha) * cfg.hit_marker_glow_strength);
      const auto glow_col = xdraw::color{cfg.hit_marker_color.value.r,
                                         cfg.hit_marker_color.value.g,
                                         cfg.hit_marker_color.value.b, glow_a};

      if (show_classic) {
        for (auto t = 0; t < 3; ++t) {
          const auto glow_thick =
              kThickness + 1.0f + static_cast<float>(t) * 2.0f;

          draw_arm(glow, x - size, y - size, x - gap, y - gap, glow_col,
                   glow_thick);
          draw_arm(glow, x + size, y - size, x + gap, y - gap, glow_col,
                   glow_thick);
          draw_arm(glow, x - size, y + size, x - gap, y + gap, glow_col,
                   glow_thick);
          draw_arm(glow, x + size, y + size, x + gap, y + gap, glow_col,
                   glow_thick);
        }
      }

      if (show_damage) {
        glow.text(draw_x, draw_y, damage_text, glow_col);
      }
    }

    if (show_classic) {
      draw_arm(draw_list, x - size, y - size, x - gap, y - gap, color,
               kThickness);
      draw_arm(draw_list, x + size, y - size, x + gap, y - gap, color,
               kThickness);
      draw_arm(draw_list, x - size, y + size, x - gap, y + gap, color,
               kThickness);
      draw_arm(draw_list, x + size, y + size, x + gap, y + gap, color,
               kThickness);
    }

    if (show_damage) {
      draw_list.text(draw_x, draw_y, damage_text, color);
    }

    ++it;
  }
}

void Impacts::RenderLogs(xdraw::draw_list& draw_list, float time) {
  std::unique_lock lock(this->m_mtx_);

  const auto& cfg = settings::g_misc.m_impacts;
  const auto& s = xui::ctx().style;

  constexpr auto kFadeRatio{0.8f};
  constexpr auto kEntrySpacing{3.0f};
  constexpr auto kBaseX{15.0f};
  constexpr auto kBaseY{15.0f};

  constexpr auto kH{24.0f};
  constexpr auto kR{8.0f};
  constexpr auto kInnerR{6.0f};
  constexpr auto kInnerPad{2.0f};
  constexpr auto kTextPadX{8.0f};
  constexpr auto kTextNudge{0.5f};
  constexpr auto kIconSize{20.0f};
  constexpr auto kIconInnerPad{4.0f};

  static const auto miss_accent = xdraw::color{255, 100, 100, 255};
  static const auto miss_dim = xdraw::color{255, 100, 100, 82};

  static auto hit_icon_w = 0, hit_icon_h = 0;
  static const auto hit_icon = xdraw::load_svg(
      R"(<svg width="12" height="12" viewBox="0 0 12 12" fill="none" xmlns="http://www.w3.org/2000/svg"><path d="M1.5 6C1.5 6.59095 1.6164 7.17611 1.84254 7.72208C2.06869 8.26804 2.40016 8.76412 2.81802 9.18198C3.23588 9.59984 3.73196 9.93131 4.27792 10.1575C4.82389 10.3836 5.40905 10.5 6 10.5C6.59095 10.5 7.17611 10.3836 7.72208 10.1575C8.26804 9.93131 8.76412 9.59984 9.18198 9.18198C9.59984 8.76412 9.93131 8.26804 10.1575 7.72208C10.3836 7.17611 10.5 6.59095 10.5 6C10.5 4.80653 10.0259 3.66193 9.18198 2.81802C8.33807 1.97411 7.19347 1.5 6 1.5C4.80653 1.5 3.66193 1.97411 2.81802 2.81802C1.97411 3.66193 1.5 4.80653 1.5 6Z" stroke="#111111" stroke-linecap="round" stroke-linejoin="round"/><path d="M4.5 7H7.5C7.5 7.39782 7.34196 7.77936 7.06066 8.06066C6.77936 8.34196 6.39782 8.5 6 8.5C5.60218 8.5 5.22064 8.34196 4.93934 8.06066C4.65804 7.77936 4.5 7.39782 4.5 7Z" stroke="#111111" stroke-linecap="round" stroke-linejoin="round"/><path d="M4.5 4L7.5 5.5" stroke="#111111" stroke-linecap="round" stroke-linejoin="round"/><path d="M4.5 5.5L7.5 4" stroke="#111111" stroke-linecap="round" stroke-linejoin="round"/></svg>)",
      1.0f, &hit_icon_w, &hit_icon_h);

  static auto miss_icon_w = 0, miss_icon_h = 0;
  static const auto miss_icon = xdraw::load_svg(
      R"(<svg width="12" height="12" viewBox="0 0 12 12" fill="none" xmlns="http://www.w3.org/2000/svg"><path d="M1.5 6C1.5 6.59095 1.6164 7.17611 1.84254 7.72208C2.06869 8.26804 2.40016 8.76412 2.81802 9.18198C3.23588 9.59984 3.73196 9.93131 4.27792 10.1575C4.82389 10.3836 5.40905 10.5 6 10.5C6.59095 10.5 7.17611 10.3836 7.72208 10.1575C8.26804 9.93131 8.76412 9.59984 9.18198 9.18198C9.59984 8.76412 9.93131 8.26804 10.1575 7.72208C10.3836 7.17611 10.5 6.59095 10.5 6C10.5 5.40905 10.3836 4.82389 10.1575 4.27792C9.93131 3.73196 9.59984 3.23588 9.18198 2.81802C8.76412 2.40016 8.26804 2.06869 7.72208 1.84254C7.17611 1.6164 6.59095 1.5 6 1.5C5.40905 1.5 4.82389 1.6164 4.27792 1.84254C3.73196 2.06869 3.23588 2.40016 2.81802 2.81802C2.40016 3.23588 2.06869 3.73196 1.84254 4.27792C1.6164 4.82389 1.5 5.40905 1.5 6Z" stroke="#111111" stroke-linecap="round" stroke-linejoin="round"/><path d="M7.25 8.02525C7.08706 7.85896 6.89258 7.72684 6.67794 7.63665C6.4633 7.54646 6.23282 7.5 6 7.5C5.76718 7.5 5.5367 7.54646 5.32206 7.63665C5.10742 7.72684 4.91294 7.85896 4.75 8.02525" stroke="#111111" stroke-linecap="round" stroke-linejoin="round"/><path d="M5 4.625C4.75 5.125 3.75 5.125 3.5 4.625" stroke="#111111" stroke-linecap="round" stroke-linejoin="round"/><path d="M8.5 4.625C8.25 5.125 7.25 5.125 7 4.625" stroke="#111111" stroke-linecap="round" stroke-linejoin="round"/></svg>)",
      1.0f, &miss_icon_w, &miss_icon_h);

  const auto inner_h = kH - kInnerPad * 2.0f;
  auto y_offset{0.0f};

  for (auto it = this->m_logs_.begin(); it != this->m_logs_.end();) {
    const auto elapsed = time - it->time;
    const auto duration = it->duration;
    const auto fade_start = duration * kFadeRatio;

    if (elapsed > duration && it->alpha.Finished()) {
      it = this->m_logs_.erase(it);
      continue;
    }

    if (elapsed > fade_start && it->alpha.Alpha() > 0.5f) {
      it->alpha.FadeOut(0.5f);
    }

    it->offset.Update();
    it->alpha.Update();

    if (!it->snapped && it->offset.Settled()) {
      it->offset.Snap(0.0f);
      it->snapped = true;
    }

    const auto alpha = it->alpha.Alpha();
    const auto slide_x = it->snapped ? 0.0f : it->offset.Value();

    if (alpha > 0.01f) {
      const auto scale_alpha = [&](xdraw::color c) -> xdraw::color {
        return c.alpha(
            static_cast<std::uint8_t>((c.a / 255.0f) * alpha * 255.0f));
      };
      const auto& icon_color = it->is_miss ? miss_accent : s.accent;
      const auto& accent_base = it->is_miss ? miss_accent : s.accent;
      const auto& dim_base = it->is_miss ? miss_dim : s.text_dim;
      const auto accent_col = scale_alpha(accent_base);
      const auto dim_col = scale_alpha(dim_base);

      struct TextSpan {
        std::string text{};
        bool accent{};
        float w{};
        float h{};
      };

      std::vector<TextSpan> spans{};

      if (it->is_miss) {
        if (it->weapon_type == cstypes::weapon_type::kNife ||
            it->weapon_type == cstypes::weapon_type::kTaser) {
          const auto weapon_name =
              it->weapon_type == cstypes::weapon_type::kNife ? "knife" : "zeus";
          const auto [a_w, a_h] = xdraw::measure_text("missed ");
          const auto [b_w, b_h] = xdraw::measure_text(weapon_name);
          const auto [c_w, c_h] = xdraw::measure_text(" on ");
          const auto [d_w, d_h] = xdraw::measure_text(it->name);
          const auto [e_w, e_h] = xdraw::measure_text(" due to ");
          const auto [f_w, f_h] = xdraw::measure_text("latency");

          spans.push_back({"missed ", false, a_w, a_h});
          spans.push_back({weapon_name, true, b_w, b_h});
          spans.push_back({" on ", false, c_w, c_h});
          spans.push_back({it->name, true, d_w, d_h});
          spans.push_back({" due to ", false, e_w, e_h});
          spans.push_back({"latency", true, f_w, f_h});
        } else {
          const auto [a_w, a_h] = xdraw::measure_text("missed ");
          const auto [b_w, b_h] = xdraw::measure_text("shot");
          const auto [c_w, c_h] = xdraw::measure_text(" due to ");
          const auto [d_w, d_h] = xdraw::measure_text(it->reason);

          spans.push_back({"missed ", false, a_w, a_h});
          spans.push_back({"shot", true, b_w, b_h});
          spans.push_back({" due to ", false, c_w, c_h});
          spans.push_back({it->reason, true, d_w, d_h});

          if (!it->hitgroup.empty()) {
            const auto [e_w, e_h] = xdraw::measure_text(" ");
            const auto [f_w, f_h] = xdraw::measure_text(it->hitgroup);

            spans.push_back({" ", false, e_w, e_h});
            spans.push_back({it->hitgroup, false, f_w, f_h});
          }
        }
      } else {
        if (it->weapon_type == cstypes::weapon_type::kNife) {
          const auto damage_text = std::to_string(it->damage);

          const auto [a_w, a_h] = xdraw::measure_text("knifed ");
          const auto [b_w, b_h] = xdraw::measure_text(it->name);
          const auto [c_w, c_h] = xdraw::measure_text(" for ");
          const auto [d_w, d_h] = xdraw::measure_text(damage_text);

          spans.push_back({"knifed ", false, a_w, a_h});
          spans.push_back({it->name, true, b_w, b_h});
          spans.push_back({" for ", false, c_w, c_h});
          spans.push_back({damage_text, true, d_w, d_h});

          const auto remaining_text =
              std::format(" ({} remaining)", it->health);
          const auto [e_w, e_h] = xdraw::measure_text(remaining_text);
          spans.push_back({remaining_text, false, e_w, e_h});
        } else if (it->weapon_type == cstypes::weapon_type::kTaser) {
          const auto [a_w, a_h] =
              xdraw::measure_text("zapped the fuck out of ");
          const auto [b_w, b_h] = xdraw::measure_text(it->name);

          spans.push_back({"zapped the fuck out of ", false, a_w, a_h});
          spans.push_back({it->name, true, b_w, b_h});
        } else {
          const auto damage_text = std::to_string(it->damage);

          const auto [a_w, a_h] = xdraw::measure_text("hit ");
          const auto [b_w, b_h] = xdraw::measure_text(it->name);
          const auto [c_w, c_h] = xdraw::measure_text(" for ");
          const auto [d_w, d_h] = xdraw::measure_text(damage_text);
          const auto [e_w, e_h] = xdraw::measure_text(" in ");
          const auto [f_w, f_h] = xdraw::measure_text(it->hitgroup);

          spans.push_back({"hit ", false, a_w, a_h});
          spans.push_back({it->name, true, b_w, b_h});
          spans.push_back({" for ", false, c_w, c_h});
          spans.push_back({damage_text, true, d_w, d_h});
          spans.push_back({" in ", false, e_w, e_h});
          spans.push_back({it->hitgroup, true, f_w, f_h});

          if (!it->reason.empty()) {
            const auto [g_w, g_h] = xdraw::measure_text(", ");
            const auto [h_w, h_h] = xdraw::measure_text(it->reason);

            spans.push_back({", ", false, g_w, g_h});
            spans.push_back({it->reason, false, h_w, h_h});
          }
        }
      }

      auto text_total_w{0.0f};
      auto text_h{0.0f};

      for (const auto& span : spans) {
        text_total_w += span.w;
        text_h = std::max(text_h, span.h);
      }

      const auto text_pill_w = text_total_w + kTextPadX * 2.0f;
      const auto total_w =
          kInnerPad + kIconSize + kInnerPad + text_pill_w + kInnerPad;

      const auto x = kBaseX + slide_x;
      const auto y = kBaseY + y_offset;

      if (elapsed <= fade_start) {
        draw_list.rect_filled_blurred(x, y, total_w, kH,
                                      xdraw::corner_radius{kR});
      }

      draw_list.rect_filled(x, y, total_w, kH, scale_alpha(s.window_bg),
                            xdraw::corner_radius{kR});
      draw_list.rect_filled(x + kInnerPad, y + kInnerPad, kIconSize, inner_h,
                            scale_alpha(icon_color),
                            xdraw::corner_radius{kInnerR});

      const auto& icon = it->is_miss ? miss_icon : hit_icon;
      if (icon) {
        const auto icon_draw = kIconSize - kIconInnerPad * 2.0f;
        const auto ix = std::floor(x + kInnerPad + kIconInnerPad);
        const auto iy =
            std::floor(y + kInnerPad + (inner_h - icon_draw) * 0.5f);
        draw_list.image(ix, iy, icon_draw, icon_draw, icon.Get(),
                        scale_alpha(s.checkbox_mark_icon));
      }

      const auto tp_x = x + kInnerPad + kIconSize + kInnerPad;
      draw_list.rect_filled(tp_x, y + kInnerPad, text_pill_w, inner_h,
                            scale_alpha(s.child_bg),
                            xdraw::corner_radius{kInnerR});

      auto tx = tp_x + kTextPadX;
      const auto ty = y + (kH - text_h) * 0.5f + kTextNudge;

      for (const auto& span : spans) {
        draw_list.text(tx, ty, span.text, span.accent ? accent_col : dim_col);
        tx += span.w;
      }

      y_offset += kH + kEntrySpacing;
    }

    ++it;
  }
}

void Impacts::RenderHitEffect(xdraw::draw_list& draw_list, float time) {
  const auto& cfg = settings::g_misc.m_impacts;

  if (!cfg.hit_effect.value || this->m_hit_effect_time_ <= 0.0f) {
    return;
  }

  const auto elapsed = time - this->m_hit_effect_time_;
  const auto duration = cfg.hit_effect_duration;

  if (elapsed > duration) {
    this->m_hit_effect_time_ = 0.0f;
    return;
  }

  const auto progress = elapsed / duration;
  const auto fade_in = std::min(elapsed * 15.0f, 1.0f);
  const auto fade_in_smooth = 1.0f - std::pow(1.0f - fade_in, 3.0f);
  const auto fade_out = 1.0f - std::pow(progress, 0.6f);
  const auto intensity =
      fade_in_smooth * fade_out * (cfg.hit_effect_strength / 100.0f);

  const auto [screen_w, screen_h] = xdraw::viewport_size();
  const auto sw = static_cast<float>(screen_w);
  const auto sh = static_cast<float>(screen_h);

  const auto r = cfg.hit_effect_color.value.r;
  const auto g = cfg.hit_effect_color.value.g;
  const auto b = cfg.hit_effect_color.value.b;

  constexpr auto kBandCount{32};

  for (auto i = 0; i < kBandCount; ++i) {
    const auto t = static_cast<float>(i) / static_cast<float>(kBandCount - 1);
    const auto size = 0.01f + t * 0.35f;
    const auto falloff = std::pow(1.0f - t, 2.5f);
    const auto a = static_cast<std::uint8_t>(
        std::min(intensity * falloff * 280.0f, 255.0f));

    if (a == 0) {
      continue;
    }

    const auto edge = xdraw::color{r, g, b, a};
    const auto clear = xdraw::color{r, g, b, 0};

    const auto tx = sw * size;
    const auto ty = sh * size;

    draw_list.rect_filled_gradient(0.0f, 0.0f, tx, sh, edge, clear, clear,
                                   edge);
    draw_list.rect_filled_gradient(sw - tx, 0.0f, tx, sh, clear, edge, edge,
                                   clear);
    draw_list.rect_filled_gradient(0.0f, 0.0f, sw, ty, edge, edge, clear,
                                   clear);
    draw_list.rect_filled_gradient(0.0f, sh - ty, sw, ty, clear, clear, edge,
                                   edge);
  }
}

void Impacts::RenderBulletImpactOverlays(xdraw::draw_list& draw_list,
                                         float time) {
  const auto& cfg = settings::g_misc.m_impacts;

  if (!cfg.bullet_impact_effect.value) {
    return;
  }

  const auto type = cfg.bullet_impact_effect_type.value;
  if (type == settings::Misc::Impacts::BulletImpactType::kSparks) {
    return;
  }

  std::unique_lock lock(this->m_mtx_);

  const auto duration = cfg.bullet_impact_effect_duration.value;
  constexpr auto kHalfSize{1.75f};

  for (auto it = this->m_bullet_impacts_.begin();
       it != this->m_bullet_impacts_.end();) {
    const auto elapsed = time - it->time;

    if (elapsed > duration) {
      it = this->m_bullet_impacts_.erase(it);
      continue;
    }

    const auto progress = elapsed / duration;
    const auto fade = 1.0f - (progress * progress);
    const auto alpha = static_cast<std::uint8_t>(fade * 255.0f);

    if (alpha == 0) {
      ++it;
      continue;
    }

    const math::Vector3 corners[8]{
        it->position + math::Vector3{-kHalfSize, -kHalfSize, -kHalfSize},
        it->position + math::Vector3{kHalfSize, -kHalfSize, -kHalfSize},
        it->position + math::Vector3{kHalfSize, kHalfSize, -kHalfSize},
        it->position + math::Vector3{-kHalfSize, kHalfSize, -kHalfSize},
        it->position + math::Vector3{-kHalfSize, -kHalfSize, kHalfSize},
        it->position + math::Vector3{kHalfSize, -kHalfSize, kHalfSize},
        it->position + math::Vector3{kHalfSize, kHalfSize, kHalfSize},
        it->position + math::Vector3{-kHalfSize, kHalfSize, kHalfSize},
    };

    float sx[8]{}, sy[8]{};
    auto all_valid{true};

    for (auto i = 0; i < 8; ++i) {
      const auto proj = systems::g_view.Project(corners[i]);
      if (!systems::g_view.ProjectionValid(proj)) {
        all_valid = false;
        break;
      }

      sx[i] = proj.x;
      sy[i] = proj.y;
    }

    if (!all_valid) {
      ++it;
      continue;
    }

    const auto scale_alpha = [alpha](xdraw::color c) -> xdraw::color {
      return xdraw::color{c.r, c.g, c.b,
                          static_cast<std::uint8_t>((c.a / 255.0f) *
                                                    (alpha / 255.0f) * 255.0f)};
    };
    const auto fill_col =
        scale_alpha(cfg.bullet_impact_effect_fill_color.value);
    const auto edge_col =
        scale_alpha(cfg.bullet_impact_effect_edge_color.value);

    constexpr int kFaces[6][4]{
        {0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4},
        {2, 3, 7, 6}, {0, 4, 7, 3}, {1, 2, 6, 5},
    };

    for (const auto& f : kFaces) {
      float poly[8]{
          sx[f[0]], sy[f[0]], sx[f[1]], sy[f[1]],
          sx[f[2]], sy[f[2]], sx[f[3]], sy[f[3]],
      };

      draw_list.convex_filled({poly, 8}, fill_col);
    }

    constexpr std::pair<int, int> kEdges[12]{
        {0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
        {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7},
    };

    if (cfg.bullet_impact_effect_glow) {
      auto& glow = xdraw::get_glow();

      const auto edge_glow_a =
          static_cast<std::uint8_t>(static_cast<float>(edge_col.a) *
                                    cfg.bullet_impact_effect_glow_strength);
      const auto edge_glow_col =
          xdraw::color{edge_col.r, edge_col.g, edge_col.b, edge_glow_a};

      const auto fill_glow_a =
          static_cast<std::uint8_t>(static_cast<float>(fill_col.a) *
                                    cfg.bullet_impact_effect_glow_strength);
      const auto fill_glow_col =
          xdraw::color{fill_col.r, fill_col.g, fill_col.b, fill_glow_a};

      for (const auto& f : kFaces) {
        float poly[8]{
            sx[f[0]], sy[f[0]], sx[f[1]], sy[f[1]],
            sx[f[2]], sy[f[2]], sx[f[3]], sy[f[3]],
        };

        glow.convex_filled({poly, 8}, fill_glow_col);
      }

      for (const auto& [a, b] : kEdges) {
        const float line[4]{sx[a], sy[a], sx[b], sy[b]};
        glow.polyline({line, 4}, edge_glow_col, false, 1.0f);
      }
    }

    for (const auto& [a, b] : kEdges) {
      const float line[4]{sx[a], sy[a], sx[b], sy[b]};
      draw_list.polyline({line, 4}, edge_col, false, 1.0f);
    }

    ++it;
  }
}

namespace custom_sound_detail {

[[nodiscard]] std::wstring SoundsDirectory() {
  wchar_t app_data[MAX_PATH]{};
  if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr,
                              SHGFP_TYPE_CURRENT, app_data))) {
    return {};
  }

  const auto root = std::wstring(app_data) + L"\\velocity";
  const auto sounds = root + L"\\sounds";

  CreateDirectoryW(root.c_str(), nullptr);
  CreateDirectoryW(sounds.c_str(), nullptr);

  return sounds;
}

[[nodiscard]] std::string SanitizeFilename(std::string_view name) {
  std::string out{};
  out.reserve(name.size());

  for (const auto c : name) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' ||
        c == '.') {
      out.push_back(static_cast<char>(c));
    }
  }

  return out;
}

[[nodiscard]] bool HasExtension(std::string_view name, std::string_view ext) {
  if (name.size() < ext.size()) {
    return false;
  }

  return _strnicmp(name.data() + name.size() - ext.size(), ext.data(),
                   ext.size()) == 0;
}

void PlayEnginePath(const char* sound_path, float volume) {
  struct {
    char padding[1080];
    int argc;
    const char** argv;
  } args{};

  char volume_str[16];
  std::snprintf(volume_str, sizeof(volume_str), "%.2f", volume / 100.0f);

  const char* sound_args[]{"playvol", sound_path, volume_str};
  args.argc = 3;
  args.argv = sound_args;

  memory::Call<void>(PATTERN(patterns::play_sound), 0.0f, &args);
}

void PlayWavDirect(const std::wstring& path, float volume) {
  using PlaySoundWT = BOOL(WINAPI*)(LPCWSTR, HMODULE, DWORD);
  using WaveOutSetVolumeT = UINT(WINAPI*)(UINT_PTR, DWORD);

  static const auto winmm = []() -> HMODULE {
    HMODULE mod = GetModuleHandleW(L"winmm.dll");
    return mod ? mod : LoadLibraryW(L"winmm.dll");
  }();

  if (!winmm) {
    return;
  }

  static const auto play_fn =
      reinterpret_cast<PlaySoundWT>(GetProcAddress(winmm, "PlaySoundW"));
  if (!play_fn) {
    return;
  }

  static const auto set_vol_fn = reinterpret_cast<WaveOutSetVolumeT>(
      GetProcAddress(winmm, "waveOutSetVolume"));
  if (set_vol_fn) {
    const auto level =
        static_cast<WORD>(std::clamp(volume / 100.0f, 0.0f, 1.0f) * 0xFFFFu);
    const DWORD vol =
        static_cast<DWORD>(level) | (static_cast<DWORD>(level) << 16);
    set_vol_fn(static_cast<UINT_PTR>(static_cast<UINT>(-1)),
               vol);  // WAVE_MAPPER
  }

  // SND_FILENAME(0x20000) | SND_ASYNC(0x1) | SND_NODEFAULT(0x2)
  play_fn(path.c_str(), nullptr, 0x00020003u);
}

[[nodiscard]] std::wstring ResolveSoundPath(std::string_view filename) {
  const auto sanitized = SanitizeFilename(filename);
  if (sanitized.empty()) {
    return {};
  }

  const auto directory = SoundsDirectory();
  if (directory.empty()) {
    return {};
  }

  const auto wide_name = std::wstring(sanitized.begin(), sanitized.end());
  const auto full_path = directory + L"\\" + wide_name;

  if (!std::filesystem::exists(full_path)) {
    return {};
  }

  return full_path;
}

}  // namespace custom_sound_detail

std::string Impacts::CustomSoundsDirectoryNarrow() {
  const auto directory = custom_sound_detail::SoundsDirectory();
  if (directory.empty()) {
    return {};
  }

  return std::string(directory.begin(), directory.end());
}

std::vector<std::string> Impacts::ListCustomSounds() {
  std::vector<std::string> files{};

  const auto directory = custom_sound_detail::SoundsDirectory();
  if (directory.empty()) {
    return files;
  }

  std::error_code ec{};
  for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
    if (ec || !entry.is_regular_file()) {
      continue;
    }

    const auto filename = entry.path().filename().string();
    if (filename.empty()) {
      continue;
    }

    if (!custom_sound_detail::HasExtension(filename, ".wav")) {
      continue;
    }

    files.push_back(filename);
  }

  std::sort(files.begin(), files.end());
  return files;
}

void Impacts::PlayCustomSound(std::string_view filename, float volume) const {
  const auto path = custom_sound_detail::ResolveSoundPath(filename);
  if (!path.empty()) {
    custom_sound_detail::PlayWavDirect(path, volume);
  }
}

void Impacts::PlaySound(settings::Misc::Impacts::SoundType type, float volume,
                        std::string_view custom_file) {
  if (type == settings::Misc::Impacts::SoundType::kCustom) {
    this->PlayCustomSound(custom_file, volume);
    return;
  }

  const char* sound_path{nullptr};

  switch (type) {
    case settings::Misc::Impacts::SoundType::kShopClick:
      sound_path = "sounds/ui/panorama/mainmenu_press_shop_01";
      break;
    case settings::Misc::Impacts::SoundType::kHomeClick:
      sound_path = "sounds/ui/panorama/mainmenu_press_home_01";
      break;
    case settings::Misc::Impacts::SoundType::kBell:
      sound_path = "sounds/training/timer_bell";
      break;
    case settings::Misc::Impacts::SoundType::kIllcard:
      sound_path = "sounds/ui/killcard_1";
      break;
    case settings::Misc::Impacts::SoundType::kBulletCasing:
      sound_path = "sounds/weapons/fx/tink/bullet_casing_07";
      break;
    case settings::Misc::Impacts::SoundType::kCoinPickup:
      sound_path = "sounds/ui/coin_pickup_01";
      break;
    case settings::Misc::Impacts::SoundType::kItemDrop:
      sound_path = "sounds/ui/item_drop";
      break;
    case settings::Misc::Impacts::SoundType::kPopcan:
      sound_path = "sounds/physics/metal/metal_popcan_impact_hard3";
      break;
    case settings::Misc::Impacts::SoundType::kEyPress:
      sound_path = "sounds/weapons/c4/key_press7";
      break;
    default:
      return;
  }

  custom_sound_detail::PlayEnginePath(sound_path, volume);
}

void Impacts::PlayHitEffect(std::uintptr_t victim_pawn) {
  const auto global_vars =
      memory::Read<std::uintptr_t>(addresses::globals::global_vars);
  const auto current_time = memory::Read<float>(global_vars + 0x30);

  this->m_hit_effect_time_ = current_time;
}

void Impacts::PlayDeathEffect(std::uintptr_t victim_pawn) {
  const auto particle_manager =
      memory::Read<std::uintptr_t>(addresses::globals::particle_manager);
  if (!particle_manager) {
    return;
  }

  constexpr auto kParticlePath{"particles/embedded/fade.vpcf"};

  if (!this->m_death_effect_loaded_) {
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

    memory::Call<void>(PATTERN(patterns::init_particle_path_buffer), &buffer,
                       kParticlePath);

    buffer.m_unknown4 = 'fcpv';

    memory::Call<void>(PATTERN(patterns::resource_system_precache),
                       addresses::globals::resource_system, &buffer, "");

    this->m_death_effect_loaded_ = true;
  }

  auto effect_index{detail::kInvalidParticleEffect};
  memory::Call<int*>(PATTERN(patterns::particle_create_effect),
                     particle_manager, &effect_index, kParticlePath, 8, 0ll,
                     0ll, 0ll, 0);

  if (effect_index == detail::kInvalidParticleEffect) {
    return;
  }

  const math::Vector3 color{
      static_cast<float>(settings::g_misc.m_impacts.death_effect_color.value.r),
      static_cast<float>(settings::g_misc.m_impacts.death_effect_color.value.g),
      static_cast<float>(
          settings::g_misc.m_impacts.death_effect_color.value.b)};
  memory::Call<bool>(PATTERN(patterns::particle_set_control_point),
                     particle_manager, effect_index, 2, &color, 0);

  struct {
    std::intptr_t xy{0x7f7fffff7f7fffffll};
    int z{0x7f7fffff};
  } default_pos;

  memory::Call<bool>(PATTERN(patterns::particle_set_entity_binding),
                     particle_manager, effect_index, 0, victim_pawn, 1, nullptr,
                     &default_pos, 1, 0ll);
  memory::Call<bool>(PATTERN(patterns::particle_set_entity_binding),
                     particle_manager, effect_index, 1, victim_pawn, 1, nullptr,
                     &default_pos, 1, 0ll);
}

void Impacts::PlayBulletImpactEffect(const math::Vector3& position) {
  const auto particle_manager =
      memory::Read<std::uintptr_t>(addresses::globals::particle_manager);
  if (!particle_manager) {
    return;
  }

  constexpr auto kParticlePath{"particles/embedded/sparks.vpcf"};

  if (!this->m_bullet_impact_effect_loaded_) {
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

    memory::Call<void>(PATTERN(patterns::init_particle_path_buffer), &buffer,
                       kParticlePath);

    buffer.m_unknown4 = 'fcpv';

    memory::Call<void>(PATTERN(patterns::resource_system_precache),
                       addresses::globals::resource_system, &buffer, "");

    this->m_bullet_impact_effect_loaded_ = true;
  }

  auto effect_index{detail::kInvalidParticleEffect};
  memory::Call<int*>(PATTERN(patterns::particle_create_effect),
                     particle_manager, &effect_index, kParticlePath, 8, 0ll,
                     0ll, 0ll, 0);

  if (effect_index == detail::kInvalidParticleEffect) {
    return;
  }

  const auto& cfg = settings::g_misc.m_impacts;

  memory::Call<bool>(PATTERN(patterns::particle_set_control_point),
                     particle_manager, effect_index, 0, &position, 0);

  const math::Vector3 color{
      static_cast<float>(cfg.bullet_impact_effect_color_spark.value.r),
      static_cast<float>(cfg.bullet_impact_effect_color_spark.value.g),
      static_cast<float>(cfg.bullet_impact_effect_color_spark.value.b)};
  memory::Call<bool>(PATTERN(patterns::particle_set_control_point),
                     particle_manager, effect_index, 1, &color, 0);
}

void Impacts::PlayBulletTracer(const math::Vector3& position) {
  const auto particle_manager =
      memory::Read<std::uintptr_t>(addresses::globals::particle_manager);
  if (!particle_manager || !systems::g_local.Get().is_alive) {
    return;
  }

  constexpr auto kParticlePath{"particles/embedded/tracer.vpcf"};

  if (!this->m_bullet_tracers_loaded_) {
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

    memory::Call<void>(PATTERN(patterns::init_particle_path_buffer), &buffer,
                       kParticlePath);

    buffer.m_unknown4 = 'fcpv';

    memory::Call<void>(PATTERN(patterns::resource_system_precache),
                       addresses::globals::resource_system, &buffer, "");

    this->m_bullet_tracers_loaded_ = true;
  }

  auto effect_index{detail::kInvalidParticleEffect};
  memory::Call<int*>(PATTERN(patterns::particle_create_effect),
                     particle_manager, &effect_index, kParticlePath, 8, 0ll,
                     0ll, 0ll, 0);

  if (effect_index == detail::kInvalidParticleEffect) {
    return;
  }

  const auto& cfg = settings::g_misc.m_impacts;

  memory::Call<bool>(PATTERN(patterns::particle_set_control_point),
                     particle_manager, effect_index, 0,
                     &this->m_buffered_eye_position_, 0);
  memory::Call<bool>(PATTERN(patterns::particle_set_control_point),
                     particle_manager, effect_index, 1, &position, 0);

  const math::Vector3 color{
      static_cast<float>(cfg.bullet_tracer_color.value.r),
      static_cast<float>(cfg.bullet_tracer_color.value.g),
      static_cast<float>(cfg.bullet_tracer_color.value.b)};
  memory::Call<bool>(PATTERN(patterns::particle_set_control_point),
                     particle_manager, effect_index, 2, &color, 0);

  const math::Vector3 lifetime{cfg.bullet_tracer_duration, 0.0f, 0.0f};
  memory::Call<bool>(PATTERN(patterns::particle_set_control_point),
                     particle_manager, effect_index, 3, &lifetime, 0);
}

void Impacts::FlushBufferedImpacts() {
  if (this->m_buffered_impacts_.empty()) {
    return;
  }

  const auto& cfg = settings::g_misc.m_impacts;
  const auto& final_pos = this->m_buffered_impacts_.back();

  const auto ray = final_pos - this->m_buffered_eye_position_;
  const auto ray_len = ray.Length();

  if (ray_len < 0.1f) {
    return;
  }

  const auto ray_dir = ray * (1.0f / ray_len);

  for (const auto& impact : this->m_buffered_impacts_) {
    const auto to_impact = impact - this->m_buffered_eye_position_;
    const auto proj = to_impact.Dot(ray_dir);
    const auto corrected =
        this->m_buffered_eye_position_ + ray_dir * std::max(proj, 0.0f);

    const auto type = cfg.bullet_impact_effect_type.value;
    const auto show_overlay =
        type == settings::Misc::Impacts::BulletImpactType::kOverlay ||
        type == settings::Misc::Impacts::BulletImpactType::kBoth;

    if (cfg.bullet_impact_effect.value && show_overlay) {
      this->m_bullet_impacts_.push_back(
          {corrected, this->m_buffered_impact_time_});

      if (this->m_bullet_impacts_.size() > 64) {
        this->m_bullet_impacts_.erase(this->m_bullet_impacts_.begin());
      }
    }
  }

  if (cfg.bullet_tracers.value) {
    this->PlayBulletTracer(final_pos);
  }
}

}  // namespace features::misc
