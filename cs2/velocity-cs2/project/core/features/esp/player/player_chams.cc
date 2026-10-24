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

namespace features::esp::player {

bool Chams::OnGeneratePrimitives(
    std::uintptr_t owner_entity, std::uint32_t owner_hash,
    std::uintptr_t scene_object, std::uintptr_t primitive_buffer,
    void(__fastcall* original_fn)(std::uintptr_t, std::uintptr_t,
                                  std::uintptr_t, std::uintptr_t),
    std::uintptr_t a1, std::uintptr_t scene_view) {
  const auto is_player = owner_hash == "C_CSPlayerPawn"_hash;
  const auto is_arms = owner_hash == "C_CS2HudModelArms"_hash;
  const auto is_weapon = owner_hash == "C_CS2HudModelWeapon"_hash;

  const auto is_local_attachment = [&](std::uintptr_t view_pawn) -> bool {
    const auto game_scene_node = memory::Read<std::uintptr_t>(
        owner_entity + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
    if (!game_scene_node) {
      return false;
    }

    const auto parent_node = memory::Read<std::uintptr_t>(
        game_scene_node + SCHEMA("CGameSceneNode", "m_pParent"_hash));
    if (!parent_node) {
      return false;
    }

    const auto parent_owner = memory::Read<std::uintptr_t>(
        parent_node + SCHEMA("CGameSceneNode", "m_pOwner"_hash));
    return parent_owner == view_pawn;
  };

  const auto apply_config = [&](const settings::Esp::ChamsConfig& cfg,
                                std::uintptr_t target_scene_obj,
                                bool force_original = false) {
    if (cfg.secondary.enabled.value) {
      this->ApplyLayer(primitive_buffer, original_fn, a1, target_scene_obj,
                       scene_view, cfg.secondary.color, cfg.secondary.material);
    }

    if (cfg.primary.enabled.value) {
      this->ApplyLayer(primitive_buffer, original_fn, a1, target_scene_obj,
                       scene_view, cfg.primary.color, cfg.primary.material);
    }

    if (!cfg.primary.enabled.value && !cfg.secondary.enabled.value &&
        (cfg.overlay.enabled.value || force_original)) {
      original_fn(a1, target_scene_obj, scene_view, primitive_buffer);
    }

    if (cfg.overlay.enabled.value) {
      this->ApplyOverlay(primitive_buffer, original_fn, a1, target_scene_obj,
                         scene_view, cfg.overlay.color, cfg.overlay.material);
    }
  };

  if (!is_player && !is_arms && !is_weapon) {
    if (!settings::g_esp.m_viewmodel.weapon.enabled.value) {
      return false;
    }

    if (!settings::g_misc.m_camera.thirdperson.value ||
        !is_local_attachment(systems::g_local.Get().ViewPawn())) {
      return false;
    }

    apply_config(settings::g_esp.m_viewmodel.weapon, scene_object);
    return true;
  }

  if (is_arms || is_weapon) {
    const auto& cfg = is_arms ? settings::g_esp.m_viewmodel.arms
                              : settings::g_esp.m_viewmodel.weapon;
    if (!cfg.enabled.value) {
      return false;
    }

    apply_config(cfg, scene_object);
    return true;
  }

  const auto local = systems::g_local.Get();
  const auto& chams_cfg = settings::g_esp.m_player.m_chams;

  const auto team = memory::Read<int>(
      owner_entity + SCHEMA("C_BaseEntity", "m_iTeamNum"_hash));
  const auto health = memory::Read<int>(
      owner_entity + SCHEMA("C_BaseEntity", "m_iHealth"_hash));

  const auto is_other_team = local.IsThisOtherTeam(team);
  const auto is_local = owner_entity == local.ViewPawn();
  const auto is_dead = health <= 0;

  if (is_player && is_other_team && !is_dead &&
      chams_cfg.backtrack.enabled.value) {
    if (this->m_backtrack_.HasActive(owner_entity)) {
      const auto bt_scene_object =
          this->m_backtrack_.GetSceneObject(owner_entity);
      if (bt_scene_object) {
        const auto before = detail::ReadPrimitiveBuffer(primitive_buffer);
        const auto prev_count = before ? before->Count() : -1;

        apply_config(chams_cfg.backtrack, bt_scene_object, true);

        const auto after = detail::ReadPrimitiveBuffer(primitive_buffer);
        const auto new_count = after ? after->Count() : -1;
        if (after && prev_count >= 0 && new_count > prev_count) {
          for (auto i = prev_count; i < new_count; ++i) {
            detail::MarkPrimitiveLast(after->At(i));
          }
        }
      }
    }
  }
  if (is_player && is_other_team && chams_cfg.onshot.enabled.value) {
    if (this->m_onshot_.HasActive(owner_entity)) {
      const auto os_obj = this->m_onshot_.GetSceneObject(owner_entity);
      if (os_obj) {
        const auto& ocfg = chams_cfg.onshot;

        const auto alpha = this->m_onshot_.GetAlpha(owner_entity);
        const auto fade = [alpha](xdraw::color c) -> xdraw::color {
          c.a = static_cast<std::uint8_t>(c.a * alpha);
          return c;
        };

        auto faded_cfg = ocfg;

        if (faded_cfg.primary.enabled.value)
          faded_cfg.primary.color.value = fade(faded_cfg.primary.color.value);

        if (faded_cfg.secondary.enabled.value)
          faded_cfg.secondary.color.value =
              fade(faded_cfg.secondary.color.value);

        if (faded_cfg.overlay.enabled.value)
          faded_cfg.overlay.color.value = fade(faded_cfg.overlay.color.value);

        const auto before = detail::ReadPrimitiveBuffer(primitive_buffer);
        const auto prev_count = before ? before->Count() : -1;

        apply_config(faded_cfg, os_obj, true);

        const auto after = detail::ReadPrimitiveBuffer(primitive_buffer);
        const auto new_count = after ? after->Count() : -1;
        if (after && prev_count >= 0 && new_count > prev_count) {
          for (auto i = prev_count; i < new_count; ++i)
            detail::MarkPrimitiveLast(after->At(i));
        }
      }
    }
  }

  const settings::Esp::ChamsConfig* target{nullptr};

  if (is_dead) {
    if (is_local) {
      target = &chams_cfg.local_ragdoll;
    } else if (is_other_team) {
      target = &chams_cfg.enemy_ragdoll;
    } else {
      target = &chams_cfg.team_ragdoll;
    }
  } else {
    if (is_local) {
      target = &chams_cfg.local;
    } else if (is_other_team) {
      target = &chams_cfg.enemy;
    } else {
      target = &chams_cfg.team;
    }
  }

  if (!target || !target->enabled.value) {
    return false;
  }

  if (!target->primary.enabled.value && !target->secondary.enabled.value &&
      !target->overlay.enabled.value) {
    return false;
  }

  {
    const auto flags = memory::SafeRead<std::uint8_t>(scene_object + 0x78);
    if (flags) {
      (void)memory::SafeWrite<std::uint8_t>(
          scene_object + 0x78, static_cast<std::uint8_t>(*flags & ~(1u << 3)));
    }
  }

  if (is_local) {
    if (misc::g_other.IsAlphaChanged()) {
      this->ApplyClone(primitive_buffer, original_fn, a1, scene_object,
                       scene_view, systems::Materials::CloneType::kTranslucent);

      if (target->overlay.enabled.value) {
        this->ApplyOverlay(primitive_buffer, original_fn, a1, scene_object,
                           scene_view, target->overlay.color,
                           target->overlay.material);
      }
    } else {
      apply_config(*target, scene_object);
    }

    return true;
  }

  apply_config(*target, scene_object);
  return true;
}

void Chams::OnSortPrimitives(std::uintptr_t entries, std::uint32_t count) {
  if (!count || !entries || count > (1u << 20)) {
    return;
  }

  const auto overlay_mat_count =
      this->m_overlay_material_count_.load(std::memory_order_acquire);
  if (overlay_mat_count <= 0) {
    return;
  }

  const auto total = static_cast<int>(count);
  if (total <= 1) {
    return;
  }

  std::vector<detail::MeshPrimitive> sorted;
  sorted.reserve(total);

  for (auto i = 0; i < total; ++i) {
    const auto primitive = memory::SafeRead<detail::MeshPrimitive>(
        entries + static_cast<std::size_t>(i) * detail::kPrimitiveSize);
    if (!primitive) {
      return;
    }

    sorted.push_back(*primitive);
  }

  const auto overlay_begin = std::stable_partition(
      sorted.begin(), sorted.end(), [this](const auto& primitive) {
        return !this->IsOverlayMaterial(primitive.material);
      });
  const auto overlay_count =
      static_cast<int>(std::distance(overlay_begin, sorted.end()));

  if (overlay_count <= 0 || overlay_count >= total) {
    return;
  }

  for (auto i = 0; i < total; ++i) {
    if (!memory::SafeWrite<detail::MeshPrimitive>(
            entries + static_cast<std::size_t>(i) * detail::kPrimitiveSize,
            sorted[i])) {
      return;
    }
  }
}

void Chams::Backtrack::Update() {
  const auto& cfg = settings::g_esp.m_player.m_chams;
  const auto local = systems::g_local.Get();

  if (!local.is_alive || !cfg.backtrack.enabled.value) {
    for (auto it = this->m_objects_.begin(); it != this->m_objects_.end();) {
      it->second.Destroy();
      it = this->m_objects_.erase(it);
    }

    return;
  }

  const auto players =
      systems::g_entities.GetByType(systems::Entities::Type::kPlayer);

  std::unordered_set<std::uintptr_t> valid_pawns;

  for (const auto& p : players) {
    if (!p.ptr || p.ptr == local.controller) {
      continue;
    }

    const auto pawn_handle = memory::Read<std::uint32_t>(
        p.ptr + SCHEMA("CBasePlayerController", "m_hPawn"_hash));
    const auto pawn = systems::g_entities.Lookup(pawn_handle);

    if (pawn && pawn != local.pawn) {
      valid_pawns.insert(pawn);
    }
  }

  for (auto it = this->m_objects_.begin(); it != this->m_objects_.end();) {
    if (!valid_pawns.contains(it->first)) {
      it->second.Destroy();
      it = this->m_objects_.erase(it);
    } else {
      ++it;
    }
  }

  std::unordered_set<std::uintptr_t> active;

  for (const auto& p : players) {
    if (!p.ptr || p.ptr == local.controller) {
      continue;
    }

    if (!memory::Read<bool>(
            p.ptr + SCHEMA("CCSPlayerController", "m_bPawnIsAlive"_hash))) {
      auto it = this->m_objects_.find(p.ptr);
      if (it != this->m_objects_.end()) {
        it->second.Destroy();
        this->m_objects_.erase(it);
      }

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
      auto it = this->m_objects_.find(pawn);
      if (it != this->m_objects_.end()) {
        it->second.Destroy();
        this->m_objects_.erase(it);
      }

      continue;
    }

    const auto oldest = combat::g_shared.Lc().GetOldestWasValid(pawn);
    if (!oldest) {
      auto it = this->m_objects_.find(pawn);
      if (it != this->m_objects_.end()) {
        it->second.Destroy();
        this->m_objects_.erase(it);
      }

      continue;
    }

    const auto game_scene_node = memory::Read<std::uintptr_t>(
        pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
    if (game_scene_node) {
      if (oldest->origin.Distance(memory::Read<math::Vector3>(
              game_scene_node +
              SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_hash))) < 0.25f) {
        auto it = this->m_objects_.find(pawn);
        if (it != this->m_objects_.end()) {
          it->second.Destroy();
          this->m_objects_.erase(it);
        }

        continue;
      }
    }

    auto& obj = this->m_objects_[pawn];
    if (!obj.scene_object) {
      obj.Create(pawn);
    }

    if (!obj.scene_object) {
      continue;
    }

    active.insert(pawn);
    obj.active = true;
    obj.SetupBones(oldest->bones, oldest->bone_count);
  }

  for (auto it = this->m_objects_.begin(); it != this->m_objects_.end();) {
    if (!active.contains(it->first)) {
      it->second.Destroy();
      it = this->m_objects_.erase(it);
    } else {
      ++it;
    }
  }
}

void Chams::Backtrack::Shutdown() {
  for (auto& [pawn, obj] : this->m_objects_) {
    obj.Destroy();
  }

  this->m_objects_.clear();
}

bool Chams::Backtrack::IsActive(std::uintptr_t scene_object) const {
  for (const auto& [pawn, obj] : this->m_objects_) {
    if (obj.scene_object == scene_object && obj.active) {
      return true;
    }
  }

  return false;
}

bool Chams::Backtrack::HasActive(std::uintptr_t pawn) const {
  auto it = this->m_objects_.find(pawn);
  return it != this->m_objects_.end() && it->second.scene_object &&
         it->second.active;
}

std::uintptr_t Chams::Backtrack::GetSceneObject(std::uintptr_t pawn) const {
  auto it = this->m_objects_.find(pawn);
  if (it != this->m_objects_.end()) {
    return it->second.scene_object;
  }

  return 0;
}

void Chams::Backtrack::SceneObject::Create(std::uintptr_t target_pawn) {
  this->pawn = target_pawn;
  this->scene_object = 0;

  const auto game_scene_node = memory::Read<std::uintptr_t>(
      target_pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
  if (!game_scene_node) {
    return;
  }

  auto temp{0};

  const auto world_group_id = memory::Call<int*>(
      PATTERN(patterns::get_world_group_id), game_scene_node, &temp);
  if (!world_group_id) {
    return;
  }

  const auto render_game_system = memory::Read<std::uintptr_t>(
      addresses::globals::render_game_system_storage);
  if (!render_game_system) {
    return;
  }

  const auto world_group_handle =
      memory::Call<std::uintptr_t>(PATTERN(patterns::get_world_group_handle),
                                   render_game_system, *world_group_id);
  if (!world_group_handle) {
    return;
  }

  const auto flags = (*world_group_id != 0) ? 0x2000000000ll : 0x2000000008ll;
  const auto model_handle = memory::Read<std::uintptr_t>(
      game_scene_node + SCHEMA("CSkeletonInstance", "m_modelState"_hash) +
      SCHEMA("CModelState", "m_hModel"_hash));
  const auto node_to_world =
      game_scene_node + SCHEMA("CGameSceneNode", "m_nodeToWorld"_hash);

  __m128 copy[2]{};
  copy[0] = *reinterpret_cast<__m128*>(node_to_world);
  copy[1] = *reinterpret_cast<__m128*>(node_to_world + 16);

  this->scene_object = memory::CallVfunc<std::uintptr_t>(
      addresses::globals::mesh_system, 20, model_handle, &copy,
      "AnimatableSceneObjectDesc", flags, 0x4100000001ll, world_group_handle);
  if (!this->scene_object) {
    return;
  }

  memory::Write<std::uintptr_t>(this->scene_object + 0x110, game_scene_node);
  memory::Write<int>(this->scene_object + 0xc0, -1);

  const auto model_data = memory::Read<std::uintptr_t>(model_handle);
  if (model_data) {
    const auto has_force_lod =
        (memory::Read<std::uint32_t>(model_data + 16) & 0x400) != 0 ||
        (memory::Read<std::uint32_t>(model_data + 20) & 0x400) != 0;
    auto lod = memory::Read<std::uint8_t>(this->scene_object + 0x9a);
    lod = has_force_lod ? (lod | 0x10) : (lod & 0xef);
    memory::Write(this->scene_object + 0x9a, lod);
  }
}

void Chams::Backtrack::SceneObject::Destroy() {
  if (!this->scene_object) {
    return;
  }

  const auto flags = memory::Read<std::uint64_t>(this->scene_object + 128);
  if (flags & 0x4000000000000000ull) {
    this->scene_object = 0;
    return;
  }

  memory::CallVfunc<void>(addresses::globals::scene_system, 16,
                          this->scene_object);
  this->scene_object = 0;
}

void Chams::Backtrack::SceneObject::SetupBones(systems::Bones::Data* bones,
                                               int count) const {
  if (!this->scene_object) {
    return;
  }

  const auto obj_bone_count = memory::Read<int>(this->scene_object + 0xd0);
  const auto render_bones =
      memory::Read<std::uintptr_t>(this->scene_object + 0xd8);

  if (!render_bones || obj_bone_count <= 0) {
    return;
  }

  const auto write_count = std::min(count, obj_bone_count);

  for (auto i = 0; i < write_count; i++) {
    const auto& b = bones[i];
    const auto dst = render_bones + (static_cast<std::size_t>(i) * 48);

    const auto bxx = b.rotation.x * b.rotation.x;
    const auto byy = b.rotation.y * b.rotation.y;
    const auto bzz = b.rotation.z * b.rotation.z;
    const auto bxy = b.rotation.x * b.rotation.y;
    const auto bxz = b.rotation.x * b.rotation.z;
    const auto byz = b.rotation.y * b.rotation.z;
    const auto bwx = b.rotation.w * b.rotation.x;
    const auto bwy = b.rotation.w * b.rotation.y;
    const auto bwz = b.rotation.w * b.rotation.z;

    memory::Write<float>(dst + 0, 1.0f - 2.0f * (byy + bzz));
    memory::Write<float>(dst + 4, 2.0f * (bxy - bwz));
    memory::Write<float>(dst + 8, 2.0f * (bxz + bwy));
    memory::Write<float>(dst + 12, b.position.x);
    memory::Write<float>(dst + 16, 2.0f * (bxy + bwz));
    memory::Write<float>(dst + 20, 1.0f - 2.0f * (bxx + bzz));
    memory::Write<float>(dst + 24, 2.0f * (byz - bwx));
    memory::Write<float>(dst + 28, b.position.y);
    memory::Write<float>(dst + 32, 2.0f * (bxz - bwy));
    memory::Write<float>(dst + 36, 2.0f * (byz + bwx));
    memory::Write<float>(dst + 40, 1.0f - 2.0f * (bxx + byy));
    memory::Write<float>(dst + 44, b.position.z);
  }
}

void Chams::Onshot::Push(std::uintptr_t pawn) {
  const auto& cfg = settings::g_esp.m_player.m_chams;
  if (!cfg.onshot.enabled.value) return;

  const auto records = combat::g_shared.Lc().GetValidRecords(pawn);
  if (records.empty()) return;

  auto* record = records.front(); /* just the newest for now, kiro make this
                                     customizable or smth */
  const auto bone_count = std::clamp(record->bone_count, 0, 27);
  auto& pending = this->m_pending_[pawn];
  pending.bone_count = bone_count;
  std::copy_n(record->bones, bone_count, pending.bones.begin());
}

void Chams::Onshot::Update() {
  const auto& cfg = settings::g_esp.m_player.m_chams;

  if (!cfg.onshot.enabled.value) {
    for (auto& [pawn, e] : this->m_entries_) e.Destroy();
    this->m_entries_.clear();
    this->m_pending_.clear();
    return;
  }

  const auto global_vars =
      memory::Read<std::uintptr_t>(addresses::globals::global_vars);
  const auto current_time = memory::Read<float>(global_vars + 0x30);

  // Creating mesh scene objects from CreateMove can race the scene graph and
  // fault inside client.dll. Materialize queued shots at frame stage instead.
  for (auto& [pawn, pending] : this->m_pending_) {
    auto& e = this->m_entries_[pawn];
    if (e.scene_object) e.Destroy();

    e.Create(pawn);
    if (!e.scene_object) {
      this->m_entries_.erase(pawn);
      continue;
    }

    e.pawn = pawn;
    e.spawn_time = current_time;
    e.active = true;
    e.SetupBones(pending.bones.data(), pending.bone_count);
  }
  this->m_pending_.clear();

  const auto fade_time = cfg.onshot_fade_time.value;

  for (auto it = this->m_entries_.begin(); it != this->m_entries_.end();) {
    if (current_time - it->second.spawn_time >= fade_time) {
      it->second.Destroy();
      it = this->m_entries_.erase(it);
    } else {
      ++it;
    }
  }
}

void Chams::Onshot::Shutdown() {
  for (auto& [pawn, e] : this->m_entries_) e.Destroy();
  this->m_entries_.clear();
  this->m_pending_.clear();
}

bool Chams::Onshot::HasActive(std::uintptr_t pawn) const {
  auto it = this->m_entries_.find(pawn);
  return it != this->m_entries_.end() && it->second.scene_object;
}

bool Chams::Onshot::IsActive(std::uintptr_t scene_object) const {
  for (const auto& [pawn, e] : this->m_entries_)
    if (e.scene_object == scene_object) return true;
  return false;
}

std::uintptr_t Chams::Onshot::GetSceneObject(std::uintptr_t pawn) const {
  auto it = this->m_entries_.find(pawn);
  if (it != this->m_entries_.end()) return it->second.scene_object;
  return 0;
}

float Chams::Onshot::GetAlpha(std::uintptr_t pawn) const {
  auto it = this->m_entries_.find(pawn);
  if (it == this->m_entries_.end() || !it->second.scene_object) return 0.0f;

  const auto global_vars =
      memory::Read<std::uintptr_t>(addresses::globals::global_vars);
  const auto current_time = memory::Read<float>(global_vars + 0x30);
  const auto fade_time =
      settings::g_esp.m_player.m_chams.onshot_fade_time.value;

  if (fade_time <= 0.0f) return 0.0f;

  const auto elapsed = current_time - it->second.spawn_time;
  return std::clamp(1.0f - (elapsed / fade_time), 0.0f, 1.0f);
}

void Chams::ApplyLayer(
    std::uintptr_t primitive_buffer,
    void(__fastcall* original_fn)(std::uintptr_t, std::uintptr_t,
                                  std::uintptr_t, std::uintptr_t),
    std::uintptr_t a1, std::uintptr_t scene_object, std::uintptr_t scene_view,
    const xdraw::color& color, settings::Esp::ChamIds material_id) {
  const auto before = detail::ReadPrimitiveBuffer(primitive_buffer);
  const auto prev_count = before ? before->Count() : -1;

  original_fn(a1, scene_object, scene_view, primitive_buffer);

  const auto after = detail::ReadPrimitiveBuffer(primitive_buffer);
  const auto new_count = after ? after->Count() : -1;
  if (!after || prev_count < 0 || prev_count >= new_count) {
    return;
  }

  const auto material = systems::Materials::Find(material_id);
  if (!material) {
    return;
  }

  for (auto i = prev_count; i < new_count; ++i) {
    detail::ReplacePrimitive(after->At(i), material, color);
  }
}

void Chams::ApplyOverlay(
    std::uintptr_t primitive_buffer,
    void(__fastcall* original_fn)(std::uintptr_t, std::uintptr_t,
                                  std::uintptr_t, std::uintptr_t),
    std::uintptr_t a1, std::uintptr_t scene_object, std::uintptr_t scene_view,
    const xdraw::color& color, settings::Esp::ChamIds material_id) {
  const auto material = systems::Materials::Find(material_id);
  if (!material) {
    return;
  }

  const auto before = detail::ReadPrimitiveBuffer(primitive_buffer);
  const auto prev_count = before ? before->Count() : -1;

  original_fn(a1, scene_object, scene_view, primitive_buffer);

  const auto after = detail::ReadPrimitiveBuffer(primitive_buffer);
  const auto new_count = after ? after->Count() : -1;
  if (!after || prev_count < 0 || prev_count >= new_count) {
    return;
  }

  for (auto i = prev_count; i < new_count; ++i) {
    detail::ReplacePrimitive(after->At(i), material, color);
  }

  this->AddOverlayMaterial(material);
}

void Chams::ApplyClone(
    std::uintptr_t primitive_buffer,
    void(__fastcall* original_fn)(std::uintptr_t, std::uintptr_t,
                                  std::uintptr_t, std::uintptr_t),
    std::uintptr_t a1, std::uintptr_t scene_object, std::uintptr_t scene_view,
    systems::Materials::CloneType type) {
  const auto before = detail::ReadPrimitiveBuffer(primitive_buffer);
  const auto prev_count = before ? before->Count() : -1;

  original_fn(a1, scene_object, scene_view, primitive_buffer);

  const auto after = detail::ReadPrimitiveBuffer(primitive_buffer);
  const auto new_count = after ? after->Count() : -1;
  if (!after || prev_count < 0 || prev_count >= new_count) {
    return;
  }

  for (auto i = prev_count; i < new_count; ++i) {
    const auto primitive = after->At(i);
    const auto orig_mat = memory::SafeRead<std::uintptr_t>(
        primitive + detail::kPrimitiveMaterialOffset);

    if (!orig_mat || !*orig_mat) {
      continue;
    }

    const auto clone = systems::Materials::GetOrCreateClone(*orig_mat, type);
    if (!clone) {
      continue;
    }

    (void)memory::SafeWrite<std::uintptr_t>(
        primitive + detail::kPrimitiveMaterialOffset, clone);
    (void)memory::SafeWrite<std::uintptr_t>(
        primitive + detail::kPrimitiveMaterialCopyOffset, clone);
  }
}

bool Chams::IsOverlayMaterial(std::uintptr_t mat) const {
  const auto count =
      this->m_overlay_material_count_.load(std::memory_order_acquire);

  for (auto i = 0; i < count; ++i) {
    if (this->m_overlay_materials_[i].load(std::memory_order_relaxed) == mat) {
      return true;
    }
  }

  return false;
}

void Chams::AddOverlayMaterial(std::uintptr_t mat) {
  const auto count =
      this->m_overlay_material_count_.load(std::memory_order_acquire);

  for (auto i = 0; i < count; ++i) {
    if (this->m_overlay_materials_[i].load(std::memory_order_relaxed) == mat) {
      return;
    }
  }

  const auto idx =
      this->m_overlay_material_count_.fetch_add(1, std::memory_order_acq_rel);

  if (idx < kMaxOverlayMaterials) {
    this->m_overlay_materials_[idx].store(mat, std::memory_order_release);
  } else {
    this->m_overlay_material_count_.fetch_sub(1, std::memory_order_release);
  }
}

}  // namespace features::esp::player
