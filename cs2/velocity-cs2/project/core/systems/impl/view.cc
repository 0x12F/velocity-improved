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

void View::Update(std::uintptr_t view) {
  this->UpdateMatrix();

  if (view) {
    this->m_origin_ = memory::Read<math::Vector3>(view + 0x0);
    this->m_angles_ = memory::Read<math::Vector3>(view + 0xc);
    this->m_fov_ = memory::Read<float>(view + 0x18);
  } else {
    this->m_origin_ = {kInvalid, kInvalid, kInvalid};
    this->m_angles_ = {kInvalid, kInvalid, kInvalid};
    this->m_fov_ = kInvalid;
  }
}

void View::UpdateMatrix() {
  if (!addresses::globals::view_matrix) {
    this->m_matrix_valid_.store(false, std::memory_order_release);
    return;
  }

  const auto matrix =
      memory::Read<math::Matrix4x4>(addresses::globals::view_matrix);
  auto valid = true;
  auto magnitude = 0.0f;

  for (auto row = 0; row < 4; ++row) {
    for (auto column = 0; column < 4; ++column) {
      const auto value = matrix[row][column];
      valid = valid && std::isfinite(value);
      magnitude += std::fabsf(value);
    }
  }

  valid = valid && magnitude > 0.001f;
  if (valid) {
    std::scoped_lock lock(this->m_matrix_mtx_);
    this->m_matrix_ = matrix;
  }

  this->m_matrix_valid_.store(valid, std::memory_order_release);
}

math::Vector2 View::Project(const math::Vector3& world_pos) {
  if (!this->HasCamera()) {
    return {this->kInvalid, this->kInvalid};
  }

  const auto m = this->Matrix();

  const auto w = m[3][0] * world_pos.x + m[3][1] * world_pos.y +
                 m[3][2] * world_pos.z + m[3][3];

  if (w < 0.001f) {
    return {this->kInvalid, this->kInvalid};
  }

  const auto x = m[0][0] * world_pos.x + m[0][1] * world_pos.y +
                 m[0][2] * world_pos.z + m[0][3];
  const auto y = m[1][0] * world_pos.x + m[1][1] * world_pos.y +
                 m[1][2] * world_pos.z + m[1][3];

  const auto display = xdraw::viewport_size();
  const auto inv_w = 1.0f / w;

  return {display.first * 0.5f * (1.0f + x * inv_w),
          display.second * 0.5f * (1.0f - y * inv_w)};
}

View::Projection View::ProjectFull(const math::Vector3& world_pos) const {
  if (!this->HasCamera()) {
    return {};
  }

  const auto m = this->Matrix();

  const auto w = m[3][0] * world_pos.x + m[3][1] * world_pos.y +
                 m[3][2] * world_pos.z + m[3][3];
  if (w < 0.001f) {
    return {};
  }

  const auto x = m[0][0] * world_pos.x + m[0][1] * world_pos.y +
                 m[0][2] * world_pos.z + m[0][3];
  const auto y = m[1][0] * world_pos.x + m[1][1] * world_pos.y +
                 m[1][2] * world_pos.z + m[1][3];

  const auto [sw, sh] = xdraw::viewport_size();
  const auto inv_w = 1.0f / w;

  const math::Vector2 screen{sw * 0.5f * (1.0f + x * inv_w),
                             sh * 0.5f * (1.0f - y * inv_w)};

  Projection r{};
  r.screen = screen;
  r.w = w;
  r.on_screen = w > 0.01f && screen.x >= 0.0f &&
                screen.x < static_cast<float>(sw) && screen.y >= 0.0f &&
                screen.y < static_cast<float>(sh);
  return r;
}

math::Matrix4x4 View::Matrix() const {
  std::scoped_lock lock(this->m_matrix_mtx_);
  return this->m_matrix_;
}

}  // namespace systems
