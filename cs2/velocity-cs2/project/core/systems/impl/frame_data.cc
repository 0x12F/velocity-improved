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

void FrameData::Update() {
  const auto local = systems::g_local.Get();
  if (!local.is_alive || !local.pawn) {
    this->Reset();
    return;
  }

  const auto game_scene_node = memory::Read<std::uintptr_t>(
      local.pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
  if (!game_scene_node) {
    this->Reset();
    return;
  }

  this->m_origin_ = memory::Read<math::Vector3>(
      game_scene_node + SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_hash));
  this->m_valid_ = true;
}

void FrameData::Reset() {
  this->m_origin_ = {};
  this->m_valid_ = false;
}

}  // namespace systems
