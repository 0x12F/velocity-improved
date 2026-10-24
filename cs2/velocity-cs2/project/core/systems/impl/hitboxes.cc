module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>

module velocity;

import <cstddef>;
import <filesystem>;
import <string_view>;
import <type_traits>;

namespace systems {

Hitboxes::Set Hitboxes::Query(std::uintptr_t game_scene_node, bool seh) {
  Set result{};

  // CSkeletonInstance moved its model handle in the current client.
  auto model_handle =
      seh ? memory::SafeRead<std::uintptr_t>(game_scene_node + 0x1E0)
                .value_or(0)
          : memory::Read<std::uintptr_t>(game_scene_node + 0x1E0);
  if (!model_handle) {
    model_handle =
        seh ? memory::SafeRead<std::uintptr_t>(game_scene_node + 0x210)
                  .value_or(0)
            : memory::Read<std::uintptr_t>(game_scene_node + 0x210);
  }

  if (!model_handle) {
    return result;
  }

  const auto cmodel = memory::Read<std::uintptr_t>(model_handle);
  if (!cmodel) {
    return result;
  }

  const auto render_mesh_array = memory::Read<std::uintptr_t>(cmodel + 0x78);
  if (!render_mesh_array) {
    return result;
  }

  const auto render_meshes = memory::Read<std::uintptr_t>(render_mesh_array);
  if (!render_meshes) {
    return result;
  }

  std::uintptr_t hitbox_set{};

  // Hitbox sets now live in the render mesh's set container. Player
  // models use the first set; retain the old direct pointer as fallback.
  const auto set_count = memory::Read<int>(render_meshes + 0x174);
  if (set_count > 0 && set_count <= 32) {
    const auto storage_flags =
        memory::Read<std::uint32_t>(render_meshes + 0x164);
    hitbox_set = (storage_flags & 0x7fffffff)
                     ? memory::Read<std::uintptr_t>(render_meshes + 0x168)
                     : render_meshes + 0x168;
  }

  if (!hitbox_set) {
    hitbox_set = memory::Read<std::uintptr_t>(render_meshes + 0x150);
  }

  if (!hitbox_set) {
    return result;
  }

  const auto count = memory::Read<int>(hitbox_set + 0x28);
  if (count <= 0 || count > 20) {
    return result;
  }

  const auto array_ptr = memory::Read<std::uintptr_t>(hitbox_set + 0x30);
  if (!array_ptr) {
    return result;
  }

  const auto remap_count = memory::Read<int>(cmodel + 0x220);
  const auto remap_table = memory::Read<std::uintptr_t>(cmodel + 0x228);
  const auto mesh_a = memory::Read<std::uintptr_t>(cmodel + 0x240);
  const auto mesh_b = memory::Read<std::uintptr_t>(cmodel + 0x2F0);

  constexpr auto kHitboxStride{0x70};

  for (auto i = 0; i < count; ++i) {
    const auto base = array_ptr + static_cast<std::size_t>(i) * kHitboxStride;

    auto bone = -1;

    if (remap_table && mesh_a && mesh_b && remap_count > 0) {
      const auto hb_idx = memory::Read<std::uint16_t>(base + 0x48);
      const auto ofs_a = memory::Read<std::uint16_t>(mesh_a);
      const auto ofs_b = memory::Read<std::uint16_t>(mesh_b);
      const auto slot = static_cast<std::size_t>(hb_idx + ofs_a + ofs_b);

      if (slot < static_cast<std::size_t>(remap_count)) {
        bone = memory::Read<std::int16_t>(remap_table + 2 * slot);
      }
    }

    if (bone < 0) {
      continue;
    }

    const auto radius = memory::Read<float>(base + 0x30);
    if (radius < 0.0f || radius > 100.0f) {
      continue;
    }

    auto& hb = result.entries[result.count++];
    hb.index = i;
    hb.bone = bone;
    hb.hitgroup = memory::Read<int>(base + 0x38);
    hb.mins = memory::Read<math::Vector3>(base + 0x18);
    hb.maxs = memory::Read<math::Vector3>(base + 0x24);
    hb.radius = radius;
    hb.shape_type = memory::Read<std::uint8_t>(base + 0x3C);
    hb.translation_only = memory::Read<std::uint8_t>(base + 0x3D) != 0;
  }

  return result;
}

int Hitboxes::HitgroupFromHitbox(int hitbox) {
  switch (hitbox) {
    case 0:
      return 1;
    case 1:
      return 8;
    case 2:
      return 3;
    case 3:
      return 3;
    case 4:
      return 2;
    case 5:
      return 2;
    case 6:
      return 2;
    case 7:
      return 7;
    case 8:
      return 6;
    case 9:
      return 7;
    case 10:
      return 6;
    case 11:
      return 7;
    case 12:
      return 6;
    case 13:
      return 5;
    case 14:
      return 4;
    case 15:
      return 5;
    case 16:
      return 5;
    case 17:
      return 4;
    case 18:
      return 4;
    default:
      return 0;
  }
}

const char* Hitboxes::HitgroupToName(int hitgroup) {
  constexpr const char* kNames[]{"body",     "head",      "chest",
                                 "stomach",  "left arm",  "right arm",
                                 "left leg", "right leg", "neck"};
  return hitgroup < 9 ? kNames[hitgroup] : "body";
}

}  // namespace systems
