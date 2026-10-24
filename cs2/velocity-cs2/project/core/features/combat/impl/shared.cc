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

namespace detail {

struct BulletTraceRecord {
  float enter_fraction;
  float exit_fraction;
  float damage_applied;
  int team_at_contact;
  std::uint16_t enter_contact_ix;
  std::uint16_t exit_contact_ix;
  std::uint8_t can_penetrate;
  std::uint8_t pad[3];
};

static_assert(sizeof(BulletTraceRecord) == 0x18);

}  // namespace detail

void Shared::Penetration::Prepare(std::uintptr_t weapon_vdata,
                                  std::uintptr_t weapon) {
  if (!weapon_vdata || !weapon) {
    return;
  }

  this->m_weapon_data_ = WeaponData{
      .damage = static_cast<float>(memory::Read<int>(
          weapon_vdata + SCHEMA("CCSWeaponBaseVData", "m_nDamage"_hash))),
      .penetration = memory::Read<float>(
          weapon_vdata + SCHEMA("CCSWeaponBaseVData", "m_flPenetration"_hash)),
      .range_modifier =
          memory::Read<float>(weapon_vdata + SCHEMA("CCSWeaponBaseVData",
                                                    "m_flRangeModifier"_hash)),
      .range = memory::Read<float>(
          weapon_vdata + SCHEMA("CCSWeaponBaseVData", "m_flRange"_hash)),
      .armor_ratio = memory::Read<float>(
          weapon_vdata + SCHEMA("CCSWeaponBaseVData", "m_flArmorRatio"_hash)),
      .headshot_multiplier = memory::Read<float>(
          weapon_vdata +
          SCHEMA("CCSWeaponBaseVData", "m_flHeadshotMultiplier"_hash))};
}

Shared::Penetration::RunContext Shared::Penetration::PrepareTarget(
    std::uintptr_t target_pawn, Lagcomp::Record* record) const {
  RunContext ctx{};
  ctx.target_pawn = target_pawn;
  ctx.record = record;
  if (record && record->game_scene_node) {
    ctx.hitboxes = systems::g_hitboxes.Query(record->game_scene_node);
  }

  ctx.target_armor = memory::Read<int>(
      target_pawn + SCHEMA("C_CSPlayerPawn", "m_ArmorValue"_hash));
  ctx.target_team = memory::Read<int>(
      target_pawn + SCHEMA("C_BaseEntity", "m_iTeamNum"_hash));

  if (ctx.target_armor > 0) {
    const auto services = memory::Read<std::uintptr_t>(
        target_pawn + SCHEMA("C_BasePlayerPawn", "m_pItemServices"_hash));
    if (services) {
      ctx.has_helmet = memory::Read<bool>(
          services + SCHEMA("CCSPlayer_ItemServices", "m_bHasHelmet"_hash));
    }
  }

  ctx.scales = {.ct_head = CONVAR("mp_damage_scale_ct_head")->Get<float>(),
                .t_head = CONVAR("mp_damage_scale_t_head")->Get<float>(),
                .ct_body = CONVAR("mp_damage_scale_ct_body")->Get<float>(),
                .t_body = CONVAR("mp_damage_scale_t_body")->Get<float>()};

  ctx.armor_ratio = this->m_weapon_data_.armor_ratio;
  ctx.headshot_multiplier = this->m_weapon_data_.headshot_multiplier;

  return ctx;
}

bool Shared::Penetration::Run(const math::Vector3& start,
                              const math::Vector3& end, const RunContext& ctx,
                              std::uintptr_t local_pawn, int local_team,
                              Result& out) const {
  if (this->m_weapon_data_.damage <= 0.0f) {
    return false;
  }

  const auto direction = (end - start).Normalized();
  const auto trace_delta = direction * this->m_weapon_data_.range;

  auto filter = systems::g_tracing.MakeFilter(local_pawn, 0x1c300b, 3, 15);
  // Rage scanning calls this hundreds of times in a frame. Reuse the large
  // trace buffer per worker instead of allocating and freeing 7 KB per point.
  thread_local systems::Tracing::TraceData trace_storage{};
  trace_storage = {};
  auto* trace = &trace_storage;
  trace->array_pointer = &trace->elements;
  trace->hit_array_pointer = &trace->hit_elements;

  g_shared.m_current_autowall_record_ = ctx.record;
  g_shared.m_autowalling_ = true;

  // The hitbox-transform hook supplies this thread's record directly.
  // Do not swap the live entity pose: Present may read it concurrently.
  systems::g_tracing.SetupTrace(trace, start, trace_delta, filter, 4, true);

  g_shared.m_autowalling_ = false;
  g_shared.m_current_autowall_record_ = nullptr;

  const auto num_hits = trace->num_hits;
  const auto hit_array =
      reinterpret_cast<std::uintptr_t>(trace->hit_array_pointer);

  if (num_hits <= 0) {
    out = {};
    return false;
  }

  const auto surface_array =
      reinterpret_cast<std::uintptr_t>(trace->array_pointer);

  memory::Call<void>(
      PATTERN(patterns::trace_bullet), trace, this->m_weapon_data_.damage,
      this->m_weapon_data_.penetration, this->m_weapon_data_.range_modifier, 4,
      local_team, static_cast<std::uintptr_t>(0));

  auto actual_hitbox{-1};
  auto closest_hitbox_fraction{1.0f};
  if (ctx.record) {
    for (const auto& hitbox : ctx.hitboxes) {
      if (hitbox.bone < 0 || hitbox.bone >= ctx.record->bone_count) {
        continue;
      }

      const auto& bone = ctx.record->bones[hitbox.bone];
      auto fraction{1.0f};
      auto intersects{false};

      if (hitbox.radius > 0.001f) {
        const auto capsule_start =
            bone.rotation.RotateVector(hitbox.mins) + bone.position;
        const auto capsule_end =
            bone.rotation.RotateVector(hitbox.maxs) + bone.position;
        intersects =
            g_shared.RayVsCapsule(start, trace_delta, capsule_start,
                                  capsule_end, hitbox.radius, fraction);
      } else {
        auto inverse = bone.rotation;
        inverse.x = -inverse.x;
        inverse.y = -inverse.y;
        inverse.z = -inverse.z;

        const auto local_origin = inverse.RotateVector(start - bone.position);
        const auto local_delta = inverse.RotateVector(trace_delta);
        auto entry{0.0f};
        auto exit{1.0f};

        const auto intersect_axis = [&](float origin, float delta,
                                        float minimum, float maximum) {
          if (std::fabsf(delta) < 1.0e-8f) {
            return origin >= minimum && origin <= maximum;
          }

          auto first = (minimum - origin) / delta;
          auto second = (maximum - origin) / delta;
          if (first > second) std::swap(first, second);
          entry = std::max(entry, first);
          exit = std::min(exit, second);
          return entry <= exit;
        };

        intersects = intersect_axis(local_origin.x, local_delta.x,
                                    hitbox.mins.x, hitbox.maxs.x) &&
                     intersect_axis(local_origin.y, local_delta.y,
                                    hitbox.mins.y, hitbox.maxs.y) &&
                     intersect_axis(local_origin.z, local_delta.z,
                                    hitbox.mins.z, hitbox.maxs.z);
        fraction = entry;
      }

      if (intersects && fraction < closest_hitbox_fraction) {
        closest_hitbox_fraction = fraction;
        actual_hitbox = hitbox.index;
      }
    }
  }

  auto penetrated{false};

  for (auto i = 0; i < num_hits; ++i) {
    auto hit = reinterpret_cast<detail::BulletTraceRecord*>(
        hit_array + i * sizeof(detail::BulletTraceRecord));
    const auto damage =
        *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(hit) + 8);

    if (damage <= 0.0f) {
      break;
    }

    if ((hit->can_penetrate & 1) != 0) {
      penetrated = true;

      if (*reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(hit) +
                                    4) == 1.0f) {
        break;
      }

      continue;
    }

    const auto trace_holder =
        surface_array + sizeof(systems::Tracing::TraceArrayElement) *
                            (hit->enter_contact_ix & 0x7fff);
    const auto hit_handle = memory::Read<std::uint32_t>(trace_holder + 0x2c);
    const auto hit_entity = systems::g_entities.Lookup(hit_handle);

    if (!hit_entity || hit_entity != ctx.target_pawn) {
      continue;
    }

    if (actual_hitbox < 0) {
      continue;
    }

    out.hitbox = actual_hitbox;
    out.hitgroup = systems::g_hitboxes.HitgroupFromHitbox(actual_hitbox);
    out.penetrated = penetrated;
    out.damage = damage;

    this->ScaleDamage(out.hitgroup, ctx.target_armor, ctx.has_helmet,
                      ctx.target_team, ctx.armor_ratio, ctx.headshot_multiplier,
                      ctx.scales, out.damage);

    return true;
  }

  out = {};
  return false;
}

bool Shared::Penetration::Can(const math::Vector3& start,
                              const math::Vector3& direction, float& out_damage,
                              const systems::Local::Snapshot& local) const {
  out_damage = 0.0f;

  if (this->m_weapon_data_.damage <= 0.0f ||
      this->m_weapon_data_.penetration <= 0.0f) {
    return false;
  }

  const auto local_team =
      memory::Read<int>(local.pawn + SCHEMA("C_BaseEntity", "m_iTeamNum"_hash));
  const auto trace_delta = direction * this->m_weapon_data_.range;

  auto filter = systems::g_tracing.MakeFilter(local.pawn, 0x1c300b, 3, 15);
  thread_local systems::Tracing::TraceData trace_storage{};
  trace_storage = {};
  auto* trace = &trace_storage;
  trace->array_pointer = &trace->elements;
  trace->hit_array_pointer = &trace->hit_elements;

  systems::g_tracing.SetupTrace(trace, start, trace_delta, filter, 4, true);

  const auto num_hits = trace->num_hits;

  if (num_hits <= 0) {
    return false;
  }

  const auto hit_array =
      reinterpret_cast<std::uintptr_t>(trace->hit_array_pointer);

  memory::Call<void>(
      PATTERN(patterns::trace_bullet), trace, this->m_weapon_data_.damage,
      this->m_weapon_data_.penetration, this->m_weapon_data_.range_modifier, 4,
      local_team, static_cast<std::uintptr_t>(0));

  for (auto i = 0; i < num_hits; ++i) {
    auto hit = reinterpret_cast<detail::BulletTraceRecord*>(
        hit_array + i * sizeof(detail::BulletTraceRecord));
    const auto damage = hit->damage_applied;

    if (damage <= 0.0f) {
      break;
    }

    if ((hit->can_penetrate & 1) != 0) {
      // Match run(): bit 0 marks a penetration record and an exit
      // fraction of 1 means the bullet did not make it through.
      if (hit->exit_fraction == 1.0f) {
        break;
      }

      out_damage = damage;
      return true;
    }
  }

  return false;
}

float Shared::Penetration::GetMaxDamage(int hitgroup, int target_armor,
                                        bool has_helmet,
                                        int target_team) const {
  if (this->m_weapon_data_.damage <= 0.0f) {
    return 0.0f;
  }

  const DamageScales scales{
      .ct_head = CONVAR("mp_damage_scale_ct_head")->Get<float>(),
      .t_head = CONVAR("mp_damage_scale_t_head")->Get<float>(),
      .ct_body = CONVAR("mp_damage_scale_ct_body")->Get<float>(),
      .t_body = CONVAR("mp_damage_scale_t_body")->Get<float>()};

  auto damage = this->m_weapon_data_.damage;
  this->ScaleDamage(hitgroup, target_armor, has_helmet, target_team,
                    this->m_weapon_data_.armor_ratio,
                    this->m_weapon_data_.headshot_multiplier, scales, damage);
  return damage;
}

float Shared::Penetration::GetMaxDamage(int hitgroup,
                                        const RunContext& ctx) const {
  auto damage = this->m_weapon_data_.damage;
  this->ScaleDamage(hitgroup, ctx.target_armor, ctx.has_helmet, ctx.target_team,
                    ctx.armor_ratio, ctx.headshot_multiplier, ctx.scales,
                    damage);
  return damage;
}

void Shared::Penetration::ScaleDamage(int hitgroup, int armor, bool has_helmet,
                                      int team, float armor_ratio,
                                      float headshot_multiplier,
                                      const DamageScales& scales,
                                      float& damage) const {
  const auto is_ct = (team == 3);
  const auto head_scale = is_ct ? scales.ct_head : scales.t_head;
  const auto body_scale = is_ct ? scales.ct_body : scales.t_body;

  switch (hitgroup) {
    case 1:
      damage *= headshot_multiplier * head_scale;
      break;
    case 2:
    case 4:
    case 5:
    case 8:
      damage *= body_scale;
      break;
    case 3:
      damage *= 1.25f * body_scale;
      break;
    case 6:
    case 7:
      damage *= 0.75f * body_scale;
      break;
    default:
      break;
  }

  const auto is_head = (hitgroup == 1);
  const auto is_armored = (hitgroup >= 1 && hitgroup <= 5) || (hitgroup == 8);

  if (armor <= 0 || !is_armored || (is_head && !has_helmet)) {
    damage = std::floor(damage);
    return;
  }

  constexpr auto kArmorBonus{0.5f};
  const auto armor_ratio_scaled = armor_ratio * 0.5f;

  auto damage_to_health = damage * armor_ratio_scaled;
  auto damage_to_armor = (damage - damage_to_health) * kArmorBonus;

  if (damage_to_armor > static_cast<float>(armor)) {
    damage_to_health = damage - (static_cast<float>(armor) / kArmorBonus);
  }

  damage = std::floor(damage_to_health);
}

bool Shared::Lagcomp::Record::Setup(std::uintptr_t pawn) {
  this->pawn = pawn;
  this->game_scene_node = memory::Read<std::uintptr_t>(
      pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));

  if (!this->game_scene_node) {
    return false;
  }

  this->bone_cache = memory::Read<std::uintptr_t>(
      this->game_scene_node + SCHEMA("CSkeletonInstance", "m_modelState"_hash) +
      0x80);
  if (!this->bone_cache) {
    return false;
  }

  this->bone_count = systems::Bones::GetCount(this->game_scene_node);
  if (this->bone_count <= 0) {
    return false;
  }
  this->bone_count = std::min(this->bone_count, 128);

  const auto abs_origin = memory::Read<math::Vector3>(
      this->game_scene_node + SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_hash));
  const auto abs_rotation = memory::Read<math::Vector3>(
      this->game_scene_node +
      SCHEMA("CGameSceneNode", "m_angAbsRotation"_hash));
  if (!std::isfinite(abs_origin.x) || !std::isfinite(abs_origin.y) ||
      !std::isfinite(abs_origin.z)) {
    return false;
  }

  // Network origin is encoded. Records and bones must stay in the same
  // evaluated world-space coordinate system.
  this->origin = abs_origin;
  this->rotation = abs_rotation;

  this->simulation_time = memory::Read<float>(
      pawn + SCHEMA("C_BaseEntity", "m_flSimulationTime"_hash));

  const auto global_vars =
      memory::Read<std::uintptr_t>(addresses::globals::global_vars);
  if (!global_vars) {
    return false;
  }

  const auto backup_current_time = memory::Read<float>(global_vars + 0x30);
  const auto backup_tick_count = memory::Read<int>(global_vars + 0x44);
  memory::Write<float>(global_vars + 0x30, this->simulation_time);
  memory::Write<int>(global_vars + 0x44,
                     cstypes::TimeToTicks(this->simulation_time));

  memory::Call<void>(PATTERN(patterns::game_scene_node_set_mesh_group),
                     this->game_scene_node, 0xfffff);
  memory::Call<void>(PATTERN(patterns::game_scene_node_set_skeleton),
                     this->game_scene_node, 0x100);

  memory::Write<int>(global_vars + 0x44, backup_tick_count);
  memory::Write<float>(global_vars + 0x30, backup_current_time);

  this->bone_cache = memory::Read<std::uintptr_t>(
      this->game_scene_node + SCHEMA("CSkeletonInstance", "m_modelState"_hash) +
      0x80);
  if (!this->bone_cache) {
    return false;
  }

  std::memcpy(this->bones, reinterpret_cast<void*>(this->bone_cache),
              sizeof(systems::Bones::Data) * this->bone_count);

  this->tick = cstypes::TimeToTicks(this->simulation_time);
  this->valid = true;

  return true;
}

bool Shared::Lagcomp::Record::IsValid() const {
  if (!this->valid) {
    return false;
  }

  const auto local_pawn = systems::g_local.Get().pawn;
  const auto net_channel =
      memory::Call<std::uintptr_t>(PATTERN(patterns::get_net_channel), 0, 0);
  const auto global_vars =
      memory::Read<std::uintptr_t>(addresses::globals::global_vars);

  if (!local_pawn || !net_channel || !global_vars) {
    return false;
  }

  const auto max_unlag = [] {
    const auto server_limit = CONVAR("sv_maxunlag")->Get<float>();
    const auto player_limit = CONVAR("sv_maxunlag_player")->Get<float>();
    return player_limit > 0.0f ? std::min(server_limit, player_limit)
                               : server_limit;
  }();

  const auto current_time = memory::Read<float>(global_vars + 0x30);
  const auto latency = memory::CallVfunc<float>(net_channel, 10, 0);

  if (!std::isfinite(max_unlag) || !std::isfinite(current_time) ||
      !std::isfinite(latency)) {
    return false;
  }

  // This value is the effective ping for the selected flow in this build;
  // combining both flows double-counts latency and can erase the window.
  const auto budget = max_unlag - std::max(latency, 0.0f);

  return budget > 0.0f && this->simulation_time >= current_time - budget;
}

void Shared::Lagcomp::Record::Apply() {
  if (!this->valid || this->is_applied || !this->game_scene_node) {
    return;
  }

  this->bone_cache = memory::Read<std::uintptr_t>(
      this->game_scene_node + SCHEMA("CSkeletonInstance", "m_modelState"_hash) +
      0x80);
  if (!this->bone_cache) {
    return;
  }

  const auto size = sizeof(systems::Bones::Data) * this->bone_count;
  std::memcpy(this->bones_backup, reinterpret_cast<void*>(this->bone_cache),
              size);
  std::memcpy(reinterpret_cast<void*>(this->bone_cache), this->bones, size);

  this->is_applied = true;
}

void Shared::Lagcomp::Record::Restore() {
  if (!this->valid || !this->is_applied || !this->bone_cache) {
    return;
  }

  const auto size = sizeof(systems::Bones::Data) * this->bone_count;
  std::memcpy(reinterpret_cast<void*>(this->bone_cache), this->bones_backup,
              size);

  this->is_applied = false;
}

void Shared::Lagcomp::Run() {
  std::unique_lock records_lock(this->m_records_mtx_);

  const auto local = systems::g_local.Get();
  if (!local.is_alive) {
    this->m_records_.clear();
    return;
  }

  std::unordered_set<std::uintptr_t> active{};

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

    const auto team =
        memory::Read<int>(pawn + SCHEMA("C_BaseEntity", "m_iTeamNum"_hash));
    if (!local.IsThisOtherTeam(team)) {
      continue;
    }

    active.insert(pawn);
  }

  std::erase_if(this->m_records_,
                [&](const auto& pair) { return !active.contains(pair.first); });

  struct PendingRecord {
    std::uintptr_t pawn{};
    int simulation_tick{};
  };

  std::vector<PendingRecord> pending;
  pending.reserve(active.size());

  for (const auto& pawn : active) {
    const auto health =
        memory::Read<int>(pawn + SCHEMA("C_BaseEntity", "m_iHealth"_hash));
    if (health <= 0) {
      this->m_records_.erase(pawn);
      continue;
    }

    auto& records = this->m_records_[pawn];
    const auto simulation_time = memory::Read<float>(
        pawn + SCHEMA("C_BaseEntity", "m_flSimulationTime"_hash));
    const auto simulation_tick = cstypes::TimeToTicks(simulation_time);

    if (records.empty() || simulation_tick > records.front().tick) {
      pending.push_back({pawn, simulation_tick});
    }

    while (!records.empty() && !records.back().IsValid()) {
      records.pop_back();
    }
  }

  if (pending.empty()) {
    return;
  }

  for (auto& p : pending) {
    Record rec{};

    if (rec.Setup(p.pawn)) {
      this->m_records_[p.pawn].emplace_front(std::move(rec));
    }
  }

  for (auto& [pawn, records] : this->m_records_) {
    for (auto& rec : records) {
      rec.was_valid = rec.IsValid();
    }
  }
}

Shared::Lagcomp::Record* Shared::Lagcomp::GetOldestValid(std::uintptr_t pawn) {
  std::shared_lock records_lock(this->m_records_mtx_);

  auto it = this->m_records_.find(pawn);
  if (it == this->m_records_.end() || it->second.empty()) {
    return nullptr;
  }

  for (auto rit = it->second.rbegin(); rit != it->second.rend(); ++rit) {
    if (rit->IsValid()) {
      return &(*rit);
    }
  }

  return nullptr;
}

Shared::Lagcomp::Record* Shared::Lagcomp::GetOldestWasValid(
    std::uintptr_t pawn) {
  std::shared_lock records_lock(this->m_records_mtx_);

  auto it = this->m_records_.find(pawn);
  if (it == this->m_records_.end() || it->second.empty()) {
    return nullptr;
  }

  for (auto rit = it->second.rbegin(); rit != it->second.rend(); ++rit) {
    if (rit->was_valid) {
      return &(*rit);
    }
  }

  return nullptr;
}

std::optional<Shared::Lagcomp::VisualRecord>
Shared::Lagcomp::GetOldestWasValidVisual(std::uintptr_t pawn) const {
  std::shared_lock records_lock(this->m_records_mtx_);

  const auto it = this->m_records_.find(pawn);
  if (it == this->m_records_.end()) {
    return std::nullopt;
  }

  for (auto rit = it->second.rbegin(); rit != it->second.rend(); ++rit) {
    if (!rit->was_valid) {
      continue;
    }

    VisualRecord out{};
    out.origin = rit->origin;
    for (auto i = 0; i < 27; ++i) {
      out.bones[i] = rit->bones[i];
    }

    return out;
  }

  return std::nullopt;
}

std::vector<Shared::Lagcomp::Record*> Shared::Lagcomp::GetValidRecords(
    std::uintptr_t pawn) {
  std::shared_lock records_lock(this->m_records_mtx_);

  std::vector<Record*> result;

  auto it = this->m_records_.find(pawn);
  if (it == this->m_records_.end()) {
    return result;
  }

  result.reserve(it->second.size());

  for (auto& rec : it->second) {
    if (rec.IsValid()) {
      result.push_back(&rec);
    }
  }

  if (result.empty()) {
    return result;
  }

  const auto max_ticks =
      std::clamp(settings::g_combat.m_lagcomp.max_backtrack_ticks.value, 1,
                 static_cast<int>(Rage::kMaxLagcompRecords));
  const auto newest_tick = result.front()->tick;

  result.erase(std::remove_if(result.begin(), result.end(),
                              [newest_tick, max_ticks](const Record* rec) {
                                return (newest_tick - rec->tick) > max_ticks;
                              }),
               result.end());

  return result;
}

std::array<systems::Bones::Data, 27> Shared::Lagcomp::GetSkeleton(
    const Record& record) const {
  std::array<systems::Bones::Data, 27> skeleton;

  if (record.valid) {
    for (auto i = 0; i < 27; ++i) {
      skeleton[i] = record.bones[i];
    }
  }

  return skeleton;
}

void Shared::ShootHistory::Snapshot(std::uintptr_t local_pawn,
                                    std::uintptr_t weapon_services) {
  this->m_count_ = 0;

  if (!weapon_services) {
    return;
  }

  {
    const auto net_client = addresses::globals::network_client_service;
    if (!net_client) {
      return;
    }

    const auto tick_state = memory::CallVfunc<std::uintptr_t>(net_client, 23);
    if (!tick_state) {
      return;
    }

    this->m_server_tick_ = memory::Read<int>(tick_state + 892);
  }

  {
    const auto idx_raw =
        memory::Read<int>(addresses::globals::frame_input_ring_idx);
    const auto idx = static_cast<unsigned>(idx_raw) % 10u;
    const auto slot = addresses::globals::frame_input_ring_base + 40ull * idx;

    this->m_client_tick_ = memory::Read<int>(slot + 0x0c);
    this->m_client_tick_frac_ = memory::Read<float>(slot + 0x10);
  }

  {
    const auto lerp_seconds =
        memory::Call<float>(PATTERN(patterns::get_interp_amount), local_pawn);
    const auto lerp_ticks_f = lerp_seconds * 64.0f;
    const auto rounded = std::round(lerp_ticks_f);

    if (std::fabs(lerp_ticks_f - rounded) < 1e-4f) {
      this->m_lerp_ticks_int_ = static_cast<int>(rounded);
      this->m_lerp_ticks_frac_ = 0.0f;
    } else {
      this->m_lerp_ticks_int_ = static_cast<int>(std::floor(lerp_ticks_f));
      this->m_lerp_ticks_frac_ =
          lerp_ticks_f - static_cast<float>(this->m_lerp_ticks_int_);
    }
  }

  const auto tail = memory::Read<int>(weapon_services + 872);
  const auto count = memory::Read<int>(weapon_services + 876);

  if (count <= 0 || count > 32 || tail < 0 || tail >= 32) {
    return;
  }

  for (auto i = 0; i < count; ++i) {
    const auto idx = (tail + i) % 32;
    const auto off = weapon_services + 232 + 20ull * idx;

    auto& e = this->m_entries_[i];
    e.tick = memory::Read<int>(off + 0x00);
    e.fraction = memory::Read<float>(off + 0x04);
    e.position.x = memory::Read<float>(off + 0x08);
    e.position.y = memory::Read<float>(off + 0x0C);
    e.position.z = memory::Read<float>(off + 0x10);
  }

  this->m_count_ = count;
}

Shared::ShootHistory::EyeCandidates Shared::ShootHistory::GetCandidates()
    const {
  EyeCandidates out{};

  if (this->m_count_ < 1) {
    return out;
  }

  constexpr auto kRingSlot{0.03125f};

  const auto newest_valid_tick = this->m_client_tick_ - this->m_lerp_ticks_int_;
  const auto oldest_valid_tick =
      this->m_client_tick_ - this->m_lerp_ticks_int_ - 1;

  auto first_valid{-1};
  auto last_valid{-1};

  for (auto i = 0; i < this->m_count_; ++i) {
    const auto t = this->m_entries_[i].tick;
    if (t > newest_valid_tick || t < oldest_valid_tick) {
      continue;
    }

    if (first_valid == -1) {
      first_valid = i;
    }

    last_valid = i;
  }

  if (last_valid == -1) {
    return out;
  }

  const auto& newest = this->m_entries_[last_valid];
  out.entries[0].position = newest.position;
  out.entries[0].player_tick = newest.tick;
  out.entries[0].player_frac = newest.fraction + kRingSlot;
  out.entries[0].lerp_ticks_int = this->m_lerp_ticks_int_;
  out.entries[0].lerp_ticks_frac = this->m_lerp_ticks_frac_;
  out.count = 1;

  if (first_valid != last_valid) {
    const auto& oldest = this->m_entries_[first_valid];
    const auto delta = oldest.position - newest.position;

    if (delta.x * delta.x + delta.y * delta.y + delta.z * delta.z >= 4.0f) {
      out.entries[1].position = oldest.position;
      out.entries[1].player_tick = oldest.tick;
      out.entries[1].player_frac = oldest.fraction + kRingSlot;
      out.entries[1].lerp_ticks_int = this->m_lerp_ticks_int_;
      out.entries[1].lerp_ticks_frac = this->m_lerp_ticks_frac_;
      out.count = 2;
    }
  }

  return out;
}

void Shared::Update() {
  this->m_ctx_ = {};

  const auto local = systems::g_local.Get();
  if (!local.pawn) {
    return;
  }

  const auto global_vars =
      memory::Read<std::uintptr_t>(addresses::globals::global_vars);
  const auto movement_services = memory::Read<std::uintptr_t>(
      local.pawn + SCHEMA("C_BasePlayerPawn", "m_pMovementServices"_hash));

  if (!global_vars || !movement_services) {
    return;
  }

  this->m_ctx_.current_tick = memory::Read<int>(global_vars + 0x44);
  this->m_ctx_.current_time = memory::Read<float>(global_vars + 0x30);
  this->m_ctx_.is_scoped = memory::Read<bool>(
      local.pawn + SCHEMA("C_CSPlayerPawn", "m_bIsScoped"_hash));
  this->m_ctx_.ticks_since_land =
      this->m_ctx_.current_tick -
      memory::Read<int>(
          movement_services +
          SCHEMA("CCSPlayer_MovementServices", "m_ModernJump"_hash) +
          SCHEMA("CCSPlayerModernJump", "m_nLastLandedTick"_hash));
  this->m_ctx_.weapon_services = memory::Read<std::uintptr_t>(
      local.pawn + SCHEMA("C_BasePlayerPawn", "m_pWeaponServices"_hash));

  if (!this->m_ctx_.weapon_services) {
    return;
  }

  const auto weapon_handle = memory::Read<std::uint32_t>(
      this->m_ctx_.weapon_services +
      SCHEMA("CPlayer_WeaponServices", "m_hActiveWeapon"_hash));
  if (!weapon_handle) {
    return;
  }

  this->m_ctx_.weapon = systems::g_entities.Lookup(weapon_handle);
  if (!this->m_ctx_.weapon) {
    return;
  }

  this->m_ctx_.weapon_vdata = memory::Read<std::uintptr_t>(
      this->m_ctx_.weapon + SCHEMA("C_BaseEntity", "m_nSubclassID"_hash) + 0x8);
  if (!this->m_ctx_.weapon_vdata) {
    return;
  }

  this->m_ctx_.range =
      memory::Read<float>(this->m_ctx_.weapon_vdata +
                          SCHEMA("CCSWeaponBaseVData", "m_flRange"_hash));
  this->m_ctx_.weapon_type = memory::Read<std::uint32_t>(
      this->m_ctx_.weapon_vdata +
      SCHEMA("CCSWeaponBaseVData", "m_WeaponType"_hash));
  this->m_ctx_.item_def_idx = memory::Read<std::uint16_t>(
      this->m_ctx_.weapon + SCHEMA("C_EconEntity", "m_AttributeManager"_hash) +
      SCHEMA("C_AttributeContainer", "m_Item"_hash) +
      SCHEMA("C_EconItemView", "m_iItemDefinitionIndex"_hash));
  this->m_ctx_.num_bullets =
      memory::Read<int>(this->m_ctx_.weapon_vdata +
                        SCHEMA("CCSWeaponBaseVData", "m_nNumBullets"_hash));
  this->m_ctx_.recoil_index = memory::Read<float>(
      this->m_ctx_.weapon + SCHEMA("C_CSWeaponBase", "m_flRecoilIndex"_hash));
  this->m_ctx_.weapon_max_speed =
      memory::Read<float>(this->m_ctx_.weapon_vdata +
                          SCHEMA("CCSWeaponBaseVData", "m_flMaxSpeed"_hash));
  this->m_ctx_.is_jump_scouting =
      (systems::g_prediction.Pre().flags & cstypes::entity_flags::kOnGround) ==
          0 &&
      this->m_ctx_.item_def_idx ==
          cstypes::item_definition_index::kWeaponSsg08 &&
      this->m_ctx_.is_scoped;
  this->m_ctx_.valid = true;

  this->m_pen_.Prepare(this->m_ctx_.weapon_vdata, this->m_ctx_.weapon);
}

void Shared::InvalidateIfNeeded() {
  const auto local = systems::g_local.Get();
  if (!local.is_alive || !local.pawn || !local.controller) {
    this->m_ctx_ = {};
    this->m_last_shoot_tick_ = 0;
  }
}

std::uint32_t Shared::GetSpreadSeed(const math::Vector3& angles,
                                    int tick) const {
  return memory::Call<std::uint32_t>(PATTERN(patterns::get_tick_view_angles),
                                     nullptr, &angles, tick);
}

math::Vector2 Shared::CalculateSpread(int seed, float accuracy, float spread,
                                      float recoil_index, int item_def_idx,
                                      int num_bullets) const {
  math::Vector2 out{};

  memory::Call<void>(PATTERN(patterns::weapon_calculate_spread),
                     static_cast<std::int16_t>(item_def_idx), num_bullets, 0,
                     static_cast<std::uint32_t>(seed + 1), accuracy, spread,
                     recoil_index, &out.x, &out.y);

  return out;
}

math::Vector3 Shared::GetAimPunch(std::uintptr_t local_pawn) const {
  math::Vector3 out{};

  memory::Call<void>(
      PATTERN(patterns::get_aim_punch),
      memory::Read<std::uintptr_t>(
          local_pawn + SCHEMA("C_CSPlayerPawn", "m_pAimPunchServices"_hash)),
      &out, 0u);

  return out;
}

float Shared::CalculateHitchance(const math::Vector3& shoot_position,
                                 const math::Vector3& aim_angle,
                                 const systems::Hitboxes::Entry& hitbox,
                                 const systems::Bones::Data& bone,
                                 float inaccuracy, float spread,
                                 int samples) const {
  const auto total = spread + inaccuracy;
  if (total < 0.0001f) {
    return 1.0f;
  }

  if (samples <= 0) {
    return 0.0f;
  }

  const auto rotation = bone.rotation;
  const auto capsule_start = rotation.RotateVector(hitbox.mins) + bone.position;
  const auto capsule_end = rotation.RotateVector(hitbox.maxs) + bone.position;
  const auto is_capsule = hitbox.radius > 0.001f;
  auto inverse_rotation = rotation;
  inverse_rotation.x = -inverse_rotation.x;
  inverse_rotation.y = -inverse_rotation.y;
  inverse_rotation.z = -inverse_rotation.z;
  const auto box_ray_origin =
      inverse_rotation.RotateVector(shoot_position - bone.position);

  const auto ray_vs_box = [&](const math::Vector3& ray_direction) {
    const auto direction = inverse_rotation.RotateVector(ray_direction);
    auto entry{0.0f};
    auto exit{1.0f};

    const auto intersect_axis = [&](float origin, float delta, float minimum,
                                    float maximum) {
      if (std::fabs(delta) < 1.0e-8f) {
        return origin >= minimum && origin <= maximum;
      }

      auto first = (minimum - origin) / delta;
      auto second = (maximum - origin) / delta;
      if (first > second) {
        std::swap(first, second);
      }

      entry = std::max(entry, first);
      exit = std::min(exit, second);
      return entry <= exit;
    };

    return intersect_axis(box_ray_origin.x, direction.x, hitbox.mins.x,
                          hitbox.maxs.x) &&
           intersect_axis(box_ray_origin.y, direction.y, hitbox.mins.y,
                          hitbox.maxs.y) &&
           intersect_axis(box_ray_origin.z, direction.z, hitbox.mins.z,
                          hitbox.maxs.z);
  };

  math::Vector3 forward{}, left{}, up{};
  math::helpers::AngleVectorsLeft(aim_angle, &forward, &left, &up);

  // Every candidate in a scan uses the same weapon state. The engine spread
  // function is much more expensive than the capsule test, so calculate each
  // deterministic seed once and reuse it for all candidate points.
  struct SpreadCache {
    float inaccuracy{};
    float spread{};
    float recoil_index{};
    int item_def_idx{};
    int num_bullets{};
    int count{};
    bool initialized{};
    std::array<math::Vector2, 256> values{};
  };

  thread_local SpreadCache cache{};
  if (!cache.initialized || cache.inaccuracy != inaccuracy ||
      cache.spread != spread ||
      cache.recoil_index != this->m_ctx_.recoil_index ||
      cache.item_def_idx != this->m_ctx_.item_def_idx ||
      cache.num_bullets != this->m_ctx_.num_bullets) {
    cache.inaccuracy = inaccuracy;
    cache.spread = spread;
    cache.recoil_index = this->m_ctx_.recoil_index;
    cache.item_def_idx = this->m_ctx_.item_def_idx;
    cache.num_bullets = this->m_ctx_.num_bullets;
    cache.count = 0;
    cache.initialized = true;
  }

  const auto cached_samples =
      std::min(samples, static_cast<int>(cache.values.size()));
  for (auto i = cache.count; i < cached_samples; ++i) {
    cache.values[i] = this->CalculateSpread(
        i, inaccuracy, spread, this->m_ctx_.recoil_index,
        this->m_ctx_.item_def_idx, this->m_ctx_.num_bullets);
  }
  cache.count = std::max(cache.count, cached_samples);

  auto hits{0};

  for (auto i = 0; i < samples; ++i) {
    const auto calculated_spread =
        i < cached_samples
            ? cache.values[i]
            : this->CalculateSpread(
                  i, inaccuracy, spread, this->m_ctx_.recoil_index,
                  this->m_ctx_.item_def_idx, this->m_ctx_.num_bullets);
    const auto direction =
        forward + (left * calculated_spread.x) + (up * calculated_spread.y);
    const auto ray_end = direction.Normalized() * 8192.0f;

    auto hit{false};
    if (is_capsule) {
      auto fraction{1.0f};
      hit = this->RayVsCapsule(shoot_position, ray_end, capsule_start,
                               capsule_end, hitbox.radius, fraction);
    } else {
      hit = ray_vs_box(ray_end);
    }

    if (hit) {
      ++hits;
    }
  }

  return static_cast<float>(hits) / static_cast<float>(samples);
}

math::Vector3 Shared::FindSpreadCorrection(const math::Vector3& aim_angle,
                                           int tick) const {
  for (auto i = 0; i < 720; i++) {
    const auto test_angles =
        math::Vector3{static_cast<float>(i) / 2.0f, aim_angle.y, 0.0f};
    const auto seed = this->GetSpreadSeed(test_angles, tick);
    const auto spread = this->CalculateSpread(
        seed, this->m_ctx_.inaccuracy, this->m_ctx_.spread,
        this->m_ctx_.recoil_index, this->m_ctx_.item_def_idx,
        this->m_ctx_.num_bullets);

    auto adj_angle = aim_angle;
    adj_angle.x += math::helpers::RadToDeg(
        std::atan(std::sqrt(spread.x * spread.x + spread.y * spread.y)));
    adj_angle.z = -math::helpers::RadToDeg(std::atan2(spread.x, spread.y));

    if (this->GetSpreadSeed(adj_angle, tick) == seed) {
      return adj_angle;
    }
  }

  return {};
}

math::Vector3 Shared::GetEyePosition(std::uintptr_t local_pawn) const {
  const auto game_scene_node = memory::Read<std::uintptr_t>(
      local_pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
  const auto origin = memory::Read<math::Vector3>(
      game_scene_node + SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_hash));
  const auto view_offset = memory::Read<math::Vector3>(
      local_pawn + SCHEMA("C_BaseModelEntity", "m_vecViewOffset"_hash));
  return origin + view_offset;
}

math::Vector3 Shared::GetShootPosition() const {
  math::Vector3 out{};
  memory::CallVfunc<void>(this->m_ctx_.weapon_services, 29,
                          reinterpret_cast<std::uintptr_t>(&out));
  return out;
}

math::Vector3 Shared::GetInterpolatedShootPosition(std::uintptr_t local_pawn,
                                                   bool newest) const {
  const auto ws = this->m_ctx_.weapon_services;
  const auto head = memory::Read<int>(ws + 872);
  const auto count = memory::Read<int>(ws + 876);

  if (count < 1) {
    return this->GetShootPosition();
  }

  if (newest) {
    const auto newest_idx = (head + count - 1) % 32;
    const auto newest_off = ws + 232 + 20ull * newest_idx;
    return memory::Read<math::Vector3>(newest_off + 8);
  }

  if (count < 2) {
    return this->GetShootPosition();
  }

  const auto interp =
      memory::Call<float>(PATTERN(patterns::get_interp_amount), local_pawn);
  const auto newest_idx = (head + count - 1) % 32;
  const auto newest_off = ws + 232 + 20ull * newest_idx;
  const auto newest_tick = memory::Read<int>(newest_off);
  const auto newest_frac = memory::Read<float>(newest_off + 4);

  const auto target =
      cstypes::TickFraction{newest_tick, newest_frac}.SubtractValue(interp *
                                                                    64.0f);

  for (auto i = 0; i < count - 1; ++i) {
    const auto idx_a = (head + static_cast<std::size_t>(i)) % 32;
    const auto idx_b = (head + static_cast<std::size_t>(i) + 1) % 32;

    const auto a_off = ws + 232 + 20ull * idx_a;
    const auto b_off = ws + 232 + 20ull * idx_b;

    const auto a_tick = memory::Read<int>(a_off);
    const auto a_frac = memory::Read<float>(a_off + 4);
    const auto b_tick = memory::Read<int>(b_off);
    const auto b_frac = memory::Read<float>(b_off + 4);

    const auto a_before = a_tick < target.tick ||
                          (a_tick == target.tick && a_frac <= target.frac);
    if (!a_before) {
      break;
    }

    const auto b_after = b_tick > target.tick ||
                         (b_tick == target.tick && b_frac >= target.frac);
    if (!b_after) {
      continue;
    }

    const auto a_pos = memory::Read<math::Vector3>(a_off + 8);
    const auto b_pos = memory::Read<math::Vector3>(b_off + 8);

    const auto span =
        cstypes::TickFraction{b_tick, b_frac}.Subtract({a_tick, a_frac});
    const auto partial = target.Subtract({a_tick, a_frac});

    const auto total_f = static_cast<float>(span.tick) + span.frac;
    const auto partial_f = static_cast<float>(partial.tick) + partial.frac;

    const auto t = total_f > 0.0f ? partial_f / total_f : 0.0f;

    return a_pos + (b_pos - a_pos) * t;
  }

  return this->GetShootPosition();
}

int Shared::CalculateStopTicks(const math::Vector3& velocity, float max_speed,
                               std::uintptr_t local_pawn) const {
  auto vel = velocity;
  vel.z = 0.0f;

  auto ticks{0};
  const auto sv_friction = CONVAR("sv_friction")->Get<float>();
  const auto sv_stopspeed = CONVAR("sv_stopspeed")->Get<float>();
  const auto sv_accelerate = CONVAR("sv_accelerate")->Get<float>();
  const auto surface_friction = systems::g_prediction.Pre().surface_friction;
  const auto accurate_threshold = max_speed * 0.34f;

  const auto is_scoped = this->m_ctx_.is_scoped;
  auto max_move_speed{250.0f};

  if (is_scoped && local_pawn) {
    const auto movement_services = memory::Read<std::uintptr_t>(
        local_pawn + SCHEMA("C_BasePlayerPawn", "m_pMovementServices"_hash));
    if (movement_services) {
      max_move_speed = memory::Read<float>(
          movement_services +
          SCHEMA("CPlayer_MovementServices", "m_flMaxspeed"_hash));
    }
  }

  while (vel.Length2d() > accurate_threshold && ticks < 15) {
    const auto speed = vel.Length2d();
    if (speed <= 0.0f) {
      break;
    }

    const auto control = std::fmaxf(speed, sv_stopspeed);
    const auto drop =
        sv_friction * surface_friction * control * cstypes::kTickInterval;
    auto new_speed = std::fmaxf(speed - drop, 0.0f);

    auto accel = sv_accelerate;

    if (is_scoped) {
      const auto weapon_ratio = std::fminf(1.0f, max_speed / 250.0f);
      const auto scoped_max =
          std::fmaxf(250.0f, max_move_speed) * weapon_ratio * 0.52f;

      if (new_speed > scoped_max - 5.0f) {
        const auto t =
            1.0f - std::fmaxf(0.0f, new_speed - (scoped_max - 5.0f)) /
                       std::fmaxf(0.01f, 5.0f);
        accel *= std::clamp(t, 0.0f, 1.0f);
      }
    }

    const auto accel_speed = std::fminf(
        accel * max_speed * surface_friction * cstypes::kTickInterval,
        new_speed);
    new_speed = std::fmaxf(new_speed - accel_speed, 0.0f);

    vel *= (new_speed / speed);
    ticks++;
  }

  return ticks;
}

float Shared::GetSpread() const {
  static const auto get_spread = PATTERN(patterns::get_spread);
  return memory::Call<float>(get_spread, this->m_ctx_.weapon);
}

float Shared::GetInaccuracy(bool update_accuracy_penalty) const {
  const auto accuracy_state_begin =
      SCHEMA("C_CSWeaponBase", "m_flTurningInaccuracyDelta"_hash);
  const auto accuracy_state_end =
      SCHEMA("C_CSWeaponBase", "m_flRecoilIndex"_hash);
  if (!this->m_ctx_.weapon || accuracy_state_begin <= 0 ||
      accuracy_state_end < accuracy_state_begin) {
    return 0.0f;
  }

  const auto accuracy_state_size =
      static_cast<std::size_t>(accuracy_state_end - accuracy_state_begin) +
      sizeof(float);
  if (accuracy_state_size > 0x100) {
    return 0.0f;
  }

  std::vector<std::uint8_t> backup(accuracy_state_size);
  std::memcpy(
      backup.data(),
      reinterpret_cast<const void*>(this->m_ctx_.weapon + accuracy_state_begin),
      accuracy_state_size);

  if (update_accuracy_penalty) {
    memory::Call<void>(PATTERN(patterns::weapon_update_accuracy),
                       this->m_ctx_.weapon);
  }

  static const auto get_inaccuracy = PATTERN(patterns::get_inaccuracy);
  const auto inaccuracy = memory::Call<float>(
      get_inaccuracy, this->m_ctx_.weapon, static_cast<float*>(nullptr),
      static_cast<float*>(nullptr));

  std::memcpy(
      reinterpret_cast<void*>(this->m_ctx_.weapon + accuracy_state_begin),
      backup.data(), accuracy_state_size);

  return inaccuracy;
}

float Shared::GetInaccuracyAtVelocity(std::uintptr_t local_pawn,
                                      const math::Vector3& velocity) const {
  const auto accuracy_state_begin =
      SCHEMA("C_CSWeaponBase", "m_flTurningInaccuracyDelta"_hash);
  const auto accuracy_state_end =
      SCHEMA("C_CSWeaponBase", "m_flRecoilIndex"_hash);
  if (!this->m_ctx_.weapon || !local_pawn || accuracy_state_begin <= 0 ||
      accuracy_state_end < accuracy_state_begin) {
    return 0.0f;
  }

  const auto accuracy_state_size =
      static_cast<std::size_t>(accuracy_state_end - accuracy_state_begin) +
      sizeof(float);
  if (accuracy_state_size > 0x100) {
    return 0.0f;
  }

  std::vector<std::uint8_t> backup(accuracy_state_size);
  std::memcpy(
      backup.data(),
      reinterpret_cast<const void*>(this->m_ctx_.weapon + accuracy_state_begin),
      accuracy_state_size);

  const auto old_velocity = memory::Read<math::Vector3>(
      local_pawn + SCHEMA("C_BaseEntity", "m_vecAbsVelocity"_hash));
  const auto old_eflags = memory::Read<std::uint32_t>(
      local_pawn + SCHEMA("C_BaseEntity", "m_iEFlags"_hash));

  memory::Write(local_pawn + SCHEMA("C_BaseEntity", "m_iEFlags"_hash),
                old_eflags & ~0x1000u);
  memory::Write(local_pawn + SCHEMA("C_BaseEntity", "m_vecAbsVelocity"_hash),
                velocity);

  memory::Call<void>(PATTERN(patterns::weapon_update_accuracy),
                     this->m_ctx_.weapon);

  static const auto get_inaccuracy = PATTERN(patterns::get_inaccuracy);
  const auto inaccuracy = memory::Call<float>(
      get_inaccuracy, this->m_ctx_.weapon, static_cast<float*>(nullptr),
      static_cast<float*>(nullptr));

  memory::Write(local_pawn + SCHEMA("C_BaseEntity", "m_vecAbsVelocity"_hash),
                old_velocity);
  memory::Write(local_pawn + SCHEMA("C_BaseEntity", "m_iEFlags"_hash),
                old_eflags);

  std::memcpy(
      reinterpret_cast<void*>(this->m_ctx_.weapon + accuracy_state_begin),
      backup.data(), accuracy_state_size);

  return inaccuracy;
}

float Shared::GetAirInaccuracy(float vertical_speed, float jump_initial,
                               float jump_apex) const {
  constexpr auto kSqrtThreshold{17.37795666f};
  const auto val =
      ((std::sqrtf(std::fabsf(vertical_speed)) - kSqrtThreshold * 0.25f) *
       (jump_initial - jump_apex)) /
          (kSqrtThreshold * 0.75f) +
      jump_apex;
  return std::clamp(val, 0.0f, jump_initial * 2.0f);
}

bool Shared::CanShoot(systems::Input::Usercmd*,
                      std::uintptr_t local_controller,
                      bool check_next_attack) const {
  if (this->m_ctx_.weapon_type != cstypes::weapon_type::kNife) {
    if (memory::Read<bool>(this->m_ctx_.weapon +
                           SCHEMA("C_CSWeaponBase", "m_bInReload"_hash))) {
      return false;
    }

    if (memory::Read<int>(this->m_ctx_.weapon +
                          SCHEMA("C_BasePlayerWeapon", "m_iClip1"_hash)) <= 0) {
      return false;
    }
  }

  if (!check_next_attack) {
    return true;
  }

  const auto tick_base = memory::Read<int>(
      local_controller + SCHEMA("CBasePlayerController", "m_nTickBase"_hash));
  // Weapon attack ticks use the player's simulation clock.
  const auto next_primary = memory::Read<int>(
      this->m_ctx_.weapon +
      SCHEMA("C_BasePlayerWeapon", "m_nNextPrimaryAttackTick"_hash));

  if (this->m_ctx_.weapon_type == cstypes::weapon_type::kNife) {
    const auto next_secondary = memory::Read<int>(
        this->m_ctx_.weapon +
        SCHEMA("C_BasePlayerWeapon", "m_nNextSecondaryAttackTick"_hash));
    return tick_base >= this->m_last_shoot_tick_ + 2 &&
           (tick_base >= next_primary || tick_base >= next_secondary);
  }

  return tick_base >= this->m_last_shoot_tick_ + 2 &&
         tick_base >= next_primary;
}

bool Shared::IsMaxAccuracy(float inaccuracy) const {
  const auto& prestate = systems::g_prediction.Pre();
  const auto on_ground = (prestate.flags & 1) != 0;
  const auto is_ducking = (prestate.flags & 4) != 0;
  const auto speed = prestate.networked_velocity.Length2d();

  if (on_ground) {
    if (this->m_ctx_.weapon_type == cstypes::weapon_type::kSniper) {
      if (!this->m_ctx_.is_scoped) {
        return false;
      }

      if (speed > 0.1f || !std::isfinite(inaccuracy) || inaccuracy < 0.0f) {
        return false;
      }

      // Firing-mode values contain separate primary and scoped floats.
      const auto accuracy_offset =
          is_ducking ? SCHEMA("CCSWeaponBaseVData", "m_flInaccuracyCrouch"_hash)
                     : SCHEMA("CCSWeaponBaseVData", "m_flInaccuracyStand"_hash);
      const auto minimum_inaccuracy = memory::Read<float>(
          this->m_ctx_.weapon_vdata + accuracy_offset + sizeof(float));
      constexpr auto kAccuracyTolerance{0.0001f};
      return std::isfinite(minimum_inaccuracy) && minimum_inaccuracy > 0.0f &&
             inaccuracy <= minimum_inaccuracy + kAccuracyTolerance;
    }

    return speed <= this->m_ctx_.weapon_max_speed * 0.34f;
  }

  const auto inaccuracy_jump_apex = memory::Read<float>(
      this->m_ctx_.weapon_vdata +
      SCHEMA("CCSWeaponBaseVData", "m_flInaccuracyJumpApex"_hash));
  const auto accuracy_penalty =
      memory::Read<float>(this->m_ctx_.weapon +
                          SCHEMA("C_CSWeaponBase", "m_fAccuracyPenalty"_hash));
  const auto min_air_inaccuracy = accuracy_penalty + inaccuracy_jump_apex;

  constexpr auto kTolerance{0.001f};
  return inaccuracy <= min_air_inaccuracy + kTolerance;
}

math::Vector3 Shared::SimulateAimPunch(int recoil_index) const {
  if (recoil_index <= 0 || !this->m_ctx_.valid) {
    return {};
  }

  const auto weapon_mode = memory::Read<int>(
      this->m_ctx_.weapon + SCHEMA("C_CSWeaponBase", "m_weaponMode"_hash));
  const auto cycle_time =
      memory::Read<float>(this->m_ctx_.weapon_vdata +
                          SCHEMA("CCSWeaponBaseVData", "m_flCycleTime"_hash));

  constexpr auto kDecayRate{4.5f};
  constexpr auto kDecay2Exp{8.0f};
  constexpr auto kDecay2Lin{18.0f};
  constexpr auto kRecoilScale{2.0f};

  math::Vector3 punch{};
  math::Vector3 punch_vel{};

  auto hybrid_decay = [](math::Vector3& v, float exp, float lin, float dt) {
    v *= std::expf(-exp * dt);

    const auto mag = v.Length();
    if (mag > lin * dt) {
      v *= (1.0f - (lin * dt) / mag);
    } else {
      v = {};
    }
  };

  for (auto i = 0; i < recoil_index; ++i) {
    float angle{}, magnitude{};
    memory::Call<void>(PATTERN(patterns::weapon_get_recoil_offset),
                       addresses::globals::weapon_recoil_data,
                       this->m_ctx_.weapon, weapon_mode, i, &angle, &magnitude);

    math::Vector3 offset{};
    offset.x = std::cosf(math::helpers::DegToRad(angle)) * magnitude;
    offset.y = std::sinf(math::helpers::DegToRad(angle)) * magnitude;

    punch_vel -= offset;

    for (auto time = 0.0f; time <= cycle_time; time += cstypes::kTickInterval) {
      hybrid_decay(punch, kDecay2Exp, kDecay2Lin, cstypes::kTickInterval);

      punch += punch_vel * cstypes::kTickInterval * 0.5f;
      punch_vel *= std::expf(-kDecayRate * cstypes::kTickInterval);

      if (punch_vel.Length() < 0.03125f) {
        punch_vel = {};
      }

      punch += punch_vel * cstypes::kTickInterval * 0.5f;
    }
  }

  return punch * kRecoilScale;
}

bool Shared::RayVsCapsule(const math::Vector3& ray_origin,
                          const math::Vector3& ray_dir,
                          const math::Vector3& capsule_a,
                          const math::Vector3& capsule_b, float radius,
                          float& out_fraction) const {
  // Long traces against small capsules lose their discriminant in float.
  // Keep all intermediate geometry in double, including vector products.
  using Vector = std::array<double, 3>;
  const auto subtract = [](const math::Vector3& a, const math::Vector3& b) {
    return Vector{static_cast<double>(a.x) - b.x,
                  static_cast<double>(a.y) - b.y,
                  static_cast<double>(a.z) - b.z};
  };
  const auto dot = [](const Vector& a, const Vector& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  };
  const Vector direction{ray_dir.x, ray_dir.y, ray_dir.z};
  const auto ab = subtract(capsule_b, capsule_a);
  const auto oc = subtract(ray_origin, capsule_a);
  const auto ab_sq = dot(ab, ab);
  const auto dir_sq = dot(direction, direction);
  const auto radius_sq = static_cast<double>(radius) * radius;
  if (!std::isfinite(ab_sq) || !std::isfinite(dot(oc, oc)) ||
      !std::isfinite(dir_sq) || !std::isfinite(radius_sq) || radius < 0.0f ||
      dir_sq < 1.0e-12) {
    return false;
  }

  const auto projection =
      ab_sq > 1.0e-12 ? std::clamp(dot(oc, ab) / ab_sq, 0.0, 1.0) : 0.0;
  Vector distance{};
  for (auto i = 0; i < 3; ++i) {
    distance[i] = oc[i] - ab[i] * projection;
  }
  if (dot(distance, distance) <= radius_sq) {
    out_fraction = 0.0f;
    return true;
  }

  auto best_t{1.0};
  auto hit{false};
  if (ab_sq > 1.0e-12) {
    const auto m = dot(ab, direction) / ab_sq;
    const auto n = dot(ab, oc) / ab_sq;
    Vector d_perp{}, oc_perp{};
    for (auto i = 0; i < 3; ++i) {
      d_perp[i] = direction[i] - ab[i] * m;
      oc_perp[i] = oc[i] - ab[i] * n;
    }
    const auto a = dot(d_perp, d_perp);
    const auto half_b = dot(d_perp, oc_perp);
    const auto c = dot(oc_perp, oc_perp) - radius_sq;
    const auto discriminant = std::fma(half_b, half_b, -a * c);
    if (a > 1.0e-12 && discriminant >= 0.0) {
      const auto root = std::sqrt(discriminant);
      for (const auto sign : {-1.0, 1.0}) {
        const auto t = (-half_b + sign * root) / a;
        const auto along_axis = m * t + n;
        if (t >= 0.0 && t <= best_t && along_axis >= 0.0 && along_axis <= 1.0) {
          best_t = t;
          hit = true;
          break;
        }
      }
    }
  }

  const math::Vector3 caps[]{capsule_a, capsule_b};
  for (auto i = 0; i < 2; ++i) {
    const auto co = subtract(ray_origin, caps[i]);
    const auto half_b = dot(co, direction);
    const auto c = dot(co, co) - radius_sq;
    const auto discriminant = std::fma(half_b, half_b, -dir_sq * c);
    if (discriminant < 0.0) {
      continue;
    }
    const auto root = std::sqrt(discriminant);
    for (const auto sign : {-1.0, 1.0}) {
      const auto t = (-half_b + sign * root) / dir_sq;
      if (t < 0.0 || t > best_t) {
        continue;
      }
      const auto along_axis = dot(ab, co) + dot(ab, direction) * t;
      if (ab_sq > 1.0e-12 && (i == 0 ? along_axis > 0.0 : along_axis < 0.0)) {
        continue;
      }
      best_t = t;
      hit = true;
      break;
    }
  }
  if (hit) {
    out_fraction = static_cast<float>(best_t);
  }
  return hit;
}

}  // namespace features::combat
