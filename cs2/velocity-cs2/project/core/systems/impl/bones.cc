module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>

#include "core/systems/schema_macros.h"

module velocity;

import <cstddef>;
import <filesystem>;
import <string_view>;
import <type_traits>;

namespace systems {

int Bones::GetCount(std::uintptr_t game_scene_node) {
  if (!game_scene_node) return 0;
  const auto model_handle =
      memory::Read<std::uintptr_t>(game_scene_node + 0x1e0);
  if (!model_handle) return 0;
  const auto model = memory::Read<std::uintptr_t>(model_handle);
  if (!model) return 0;
  // CModel::m_modelSkeleton starts with CRenderSkeleton::m_boneName.
  const auto count = memory::Read<int>(model + 0x188);
  return count > 0 && count <= 1024 ? count : 0;
}

Bones::Data Bones::Get(std::uintptr_t entity, std::uint32_t bone_id) {
  Data result{};

  auto count{0};
  const auto cache = this->GetBoneCache(entity, &count);

  if (!cache || bone_id >= count) {
    return result;
  }

  const auto& bone = *reinterpret_cast<Data*>(
      cache + static_cast<std::size_t>(bone_id) * sizeof(Data));
  result.position = bone.position;
  result.rotation = bone.rotation;

  return result;
}

std::array<Bones::Data, 27> Bones::GetSkeleton(std::uintptr_t entity) {
  static constexpr std::array<std::uint32_t, 21> kBones{
      cstypes::bone_ids::kPelvis,        cstypes::bone_ids::kSpine1,
      cstypes::bone_ids::kSpine2,        cstypes::bone_ids::kSpine3,
      cstypes::bone_ids::kSpine4,        cstypes::bone_ids::kNeck,
      cstypes::bone_ids::kHead,          cstypes::bone_ids::kLeftClavicle,
      cstypes::bone_ids::kLeftShoulder,  cstypes::bone_ids::kLeftElbow,
      cstypes::bone_ids::kLeftHand,      cstypes::bone_ids::kRightClavicle,
      cstypes::bone_ids::kRightShoulder, cstypes::bone_ids::kRightElbow,
      cstypes::bone_ids::kRightHand,     cstypes::bone_ids::kLeftHip,
      cstypes::bone_ids::kLeftKnee,      cstypes::bone_ids::kLeftFoot,
      cstypes::bone_ids::kRightHip,      cstypes::bone_ids::kRightKnee,
      cstypes::bone_ids::kRightFoot};

  std::array<Data, 27> skeleton{};

  auto count{0};
  const auto cache = this->GetBoneCache(entity, &count);

  if (!cache || count <= 0) {
    return skeleton;
  }

  for (const auto bone_id : kBones) {
    if (bone_id >= count) {
      continue;
    }

    const auto& bone = *reinterpret_cast<Data*>(
        cache + static_cast<std::size_t>(bone_id) * sizeof(Data));
    skeleton[bone_id].position = bone.position;
    skeleton[bone_id].rotation = bone.rotation;
  }

  return skeleton;
}

std::uintptr_t Bones::GetBoneCache(std::uintptr_t entity, int* out_count) {
  const auto game_scene_node = memory::Read<std::uintptr_t>(
      entity + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
  if (!game_scene_node) {
    return 0;
  }

  const auto bone_cache = memory::Read<std::uintptr_t>(
      game_scene_node + SCHEMA("CSkeletonInstance", "m_modelState"_hash) +
      0x80);
  if (!bone_cache) {
    return 0;
  }

  if (out_count) {
    *out_count = GetCount(game_scene_node);
  }

  return bone_cache;
}

}  // namespace systems
