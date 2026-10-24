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

namespace features::misc {
namespace {
constexpr std::ptrdiff_t kFovOffset{0x498};
constexpr std::ptrdiff_t kAspectRatioOffset{0x4d4};
constexpr std::ptrdiff_t kViewFlagsOffset{0x551};
constexpr std::uint8_t kExplicitAspectRatioFlag{1u << 1};

[[nodiscard]] float ScaleHorizontalFov(float fov, float aspect_ratio) {
  constexpr auto kDegreesToHalfRadians{std::numbers::pi_v<float> / 360.0f};
  constexpr auto kHalfRadiansToDegrees{360.0f / std::numbers::pi_v<float>};
  constexpr auto kFourByThreeInverse{0.75f};

  return std::atan(std::tan(fov * kDegreesToHalfRadians) * aspect_ratio *
                   kFourByThreeInverse) *
         kHalfRadiansToDegrees;
}
}  // namespace

void Camera::OnOverrideView(std::uintptr_t view_setup) {
  const auto local = systems::g_local.Get();
  if (local.is_alive && !systems::g_local.IsInCinematic() && local.team >= 2 &&
      local.pawn) {
    this->DoThirdperson(view_setup, local.pawn);
    this->DoFovChange(view_setup, local.pawn);
  }

  // Aspect conversion must run after the base FOV has been selected.
  this->DoAspectRatioChange(view_setup);
}

void Camera::UpdateFovSensitivity(std::uintptr_t player_pawn) const {
  if (!settings::g_misc.m_camera.change_fov.value) {
    return;
  }

  const auto local = systems::g_local.Get();
  if (player_pawn != local.pawn) {
    return;
  }

  const auto& cfg = settings::g_misc.m_camera;
  const auto is_scoped = memory::Read<bool>(
      player_pawn + SCHEMA("C_CSPlayerPawn", "m_bIsScoped"_hash));
  const auto target_fov = (is_scoped && cfg.scoped_fov_override.value)
                              ? cfg.scoped_fov.value
                              : cfg.fov.value;

  if (is_scoped == this->m_cached_scoped_ &&
      target_fov == this->m_cached_target_fov_ &&
      this->m_cached_fov_sensitivity_ >= 0.0f) {
    const auto current_adjust = memory::Read<float>(
        player_pawn +
        SCHEMA("C_BasePlayerPawn", "m_flFOVSensitivityAdjust"_hash));
    if (std::fabsf(current_adjust - this->m_cached_fov_sensitivity_) <
        0.0001f) {
      return;
    }
  }

  this->m_cached_scoped_ = is_scoped;
  this->m_cached_target_fov_ = target_fov;

  const auto ratio = CONVAR("zoom_sensitivity_ratio")->Get<float>();
  const auto desired = ratio * (target_fov / 90.0f);

  this->m_cached_fov_sensitivity_ = desired;
  memory::Write<float>(
      player_pawn + SCHEMA("C_BasePlayerPawn", "m_flFOVSensitivityAdjust"_hash),
      desired);
}

void Camera::DoThirdperson(std::uintptr_t view_setup,
                           std::uintptr_t local_pawn) const {
  const auto& cfg = settings::g_misc.m_camera;
  if (!cfg.thirdperson.value) {
    return;
  }

  const auto game_scene_node = memory::Read<std::uintptr_t>(
      local_pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
  const auto origin = memory::Read<math::Vector3>(
      game_scene_node + SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_hash));
  const auto view_offset = memory::Read<math::Vector3>(
      local_pawn + SCHEMA("C_BaseModelEntity", "m_vecViewOffset"_hash));
  const auto eye_position = origin + view_offset;
  const auto view_angles = systems::g_input.GetViewAngles();

  math::Vector3 forward{};
  {
    math::helpers::AngleVectorsLeft(view_angles, &forward);
  }

  auto camera_position = eye_position - forward * cfg.thirdperson_distance;
  const auto hull =
      math::Vector3{-cfg.thirdperson_hull_size, -cfg.thirdperson_hull_size,
                    -cfg.thirdperson_hull_size};
  const auto result = systems::g_tracing.TraceHull(
      eye_position, camera_position, hull, hull, local_pawn);

  if (result.fraction < 1.0f) {
    const auto world = systems::g_entities.GetByIndex(0);
    if (result.hit_entity == world) {
      camera_position =
          eye_position + (camera_position - eye_position) * result.fraction;
    }
  }

  memory::Write<math::Vector3>(view_setup + 0x4a0, camera_position);
}

void Camera::DoFovChange(std::uintptr_t view_setup,
                         std::uintptr_t local_pawn) const {
  const auto& cfg = settings::g_misc.m_camera;
  if (!cfg.change_fov.value) {
    return;
  }

  const auto is_scoped = memory::Read<bool>(
      local_pawn + SCHEMA("C_CSPlayerPawn", "m_bIsScoped"_hash));
  const auto target_fov = (is_scoped && cfg.scoped_fov_override.value)
                              ? cfg.scoped_fov.value
                              : cfg.fov.value;

  memory::Write<float>(view_setup + kFovOffset, target_fov);

  this->UpdateFovSensitivity(local_pawn);
}

void Camera::DoAspectRatioChange(std::uintptr_t view_setup) {
  const auto& cfg = settings::g_misc.m_camera;

  if (cfg.change_aspect_ratio.value) {
    const auto base_fov = memory::Read<float>(view_setup + kFovOffset);
    const auto flags =
        memory::Read<std::uint8_t>(view_setup + kViewFlagsOffset);

    // Explicit aspect bypasses the game's native 4:3-based FOV conversion.
    memory::Write<float>(view_setup + kFovOffset,
                         ScaleHorizontalFov(base_fov, cfg.aspect_ratio));
    memory::Write<float>(view_setup + kAspectRatioOffset, cfg.aspect_ratio);
    memory::Write<std::uint8_t>(view_setup + kViewFlagsOffset,
                                flags | kExplicitAspectRatioFlag);
  } else {
    const auto flags =
        memory::Read<std::uint8_t>(view_setup + kViewFlagsOffset);
    memory::Write<std::uint8_t>(
        view_setup + kViewFlagsOffset,
        flags & static_cast<std::uint8_t>(~kExplicitAspectRatioFlag));
  }
}

}  // namespace features::misc
