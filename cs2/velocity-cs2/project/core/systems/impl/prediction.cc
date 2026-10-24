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

void Prediction::CapturePrestate(std::uintptr_t local_pawn,
                                 std::uintptr_t movement_services) {
  this->m_prestate_.flags = memory::Read<std::uint32_t>(
      local_pawn + SCHEMA("C_BaseEntity", "m_fFlags"_hash));
  this->m_prestate_.networked_velocity = memory::Read<math::Vector3>(
      local_pawn + SCHEMA("C_BaseEntity", "m_vecVelocity"_hash));
  this->m_prestate_.velocity = memory::Read<math::Vector3>(
      local_pawn + SCHEMA("C_BaseEntity", "m_vecAbsVelocity"_hash));
  this->m_prestate_.stamina = memory::Read<float>(
      movement_services +
      SCHEMA("CCSPlayer_MovementServices", "m_flStamina"_hash));
  this->m_prestate_.surface_friction = memory::Read<float>(
      movement_services +
      SCHEMA("CPlayer_MovementServices_Humanoid", "m_flSurfaceFriction"_hash));

  this->m_prestate_.last_movement_impulses.x = memory::Read<float>(
      movement_services +
      SCHEMA("CPlayer_MovementServices", "m_flCmdForwardMove"_hash));
  this->m_prestate_.last_movement_impulses.y =
      memory::Read<float>(movement_services + SCHEMA("CPlayer_MovementServices",
                                                     "m_flCmdLeftMove"_hash));
  this->m_prestate_.last_movement_impulses.z =
      memory::Read<float>(movement_services + SCHEMA("CPlayer_MovementServices",
                                                     "m_flCmdUpMove"_hash));

  const auto game_scene_node = memory::Read<std::uintptr_t>(
      local_pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
  if (game_scene_node) {
    this->m_prestate_.origin = memory::Read<math::Vector3>(
        game_scene_node + SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_hash));
    // m_vecOrigin is an encoded network-origin object, not a vector3.
    // Use the evaluated position anywhere a plain world-space vector is needed.
    this->m_prestate_.networked_origin = this->m_prestate_.origin;
  }
}

}  // namespace systems
