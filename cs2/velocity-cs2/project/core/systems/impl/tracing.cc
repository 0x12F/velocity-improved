module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>

#include "core/systems/schema_macros.h"
#include "protection/address_macros.h"

module velocity;

import <cstddef>;
import <filesystem>;
import <string_view>;
import <type_traits>;

namespace systems {

bool Tracing::IsVisible(const math::Vector3& start, const math::Vector3& end,
                        std::uintptr_t target_entity,
                        std::uintptr_t skip_entity, std::uintptr_t mask) const {
  auto current_start = start;
  auto entity_to_skip = skip_entity;

  constexpr auto kMaxPenetrations{3};

  for (auto i = 0; i < kMaxPenetrations; ++i) {
    const auto result = this->Trace(current_start, end, entity_to_skip, mask);

    if (result.hit_entity == target_entity || result.fraction > 0.97f) {
      return true;
    }

    if (!result.hit_entity) {
      break;
    }

    const auto hit_health = memory::Read<int>(
        result.hit_entity + SCHEMA("C_BaseEntity", "m_iHealth"_hash));
    if (hit_health > 0 && hit_health <= 100) {
      entity_to_skip = result.hit_entity;
      current_start = result.end_pos + (end - current_start).Normalized();
      continue;
    }

    break;
  }

  return false;
}

Tracing::Result Tracing::Trace(const math::Vector3& start,
                               const math::Vector3& end,
                               std::uintptr_t skip_entity, std::uintptr_t mask,
                               std::uint8_t layer) const {
  auto filter = this->MakeFilter(skip_entity, mask, layer);
  return this->Trace(start, end, filter);
}

Tracing::Result Tracing::Trace(const math::Vector3& start,
                               const math::Vector3& end,
                               const Filter& filter) const {
  Ray ray{};
  Result result{};

  memory::Call<bool>(PATTERN(patterns::trace_ray),
                     addresses::globals::game_trace_manager, &ray, &start, &end,
                     &filter, &result);

  return result;
}

Tracing::Result Tracing::TraceHull(
    const math::Vector3& start, const math::Vector3& end,
    const math::Vector3& mins, const math::Vector3& maxs,
    std::uintptr_t skip_entity, std::uintptr_t mask, std::uint8_t layer) const {
  const auto filter = this->MakeFilter(skip_entity, mask, layer);
  return this->TraceHull(start, end, mins, maxs, filter);
}

Tracing::Result Tracing::TraceHull(const math::Vector3& start,
                                   const math::Vector3& end,
                                   const math::Vector3& mins,
                                   const math::Vector3& maxs,
                                   const Filter& filter) const {
  Ray ray{};
  ray.mins = mins;
  ray.maxs = maxs;
  ray.type = 2;

  Result result{};

  memory::Call<bool>(PATTERN(patterns::trace_ray),
                     addresses::globals::game_trace_manager, &ray, &start, &end,
                     &filter, &result);

  return result;
}

Tracing::Result Tracing::TraceSphere(const math::Vector3& start,
                                     const math::Vector3& end, float radius,
                                     const Filter& filter) const {
  Ray ray{};
  ray.mins = {};
  *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(&ray) + 12) =
      radius;
  ray.type = 1;

  Result result{};

  memory::Call<bool>(PATTERN(patterns::trace_ray),
                     addresses::globals::game_trace_manager, &ray, &start, &end,
                     &filter, &result);

  return result;
}

Tracing::Result Tracing::TraceToEntity(const math::Vector3& start,
                                       const math::Vector3& end,
                                       std::uintptr_t target_entity,
                                       std::uintptr_t skip_entity,
                                       std::uintptr_t mask,
                                       std::uint8_t layer) const {
  const auto filter = this->MakeFilter(skip_entity, mask, layer);
  return this->TraceToEntity(start, end, target_entity, filter);
}

Tracing::Result Tracing::TraceToEntity(const math::Vector3& start,
                                       const math::Vector3& end,
                                       std::uintptr_t target_entity,
                                       const Filter& filter) const {
  Ray ray{};
  Result result{};

  memory::Call<bool>(PATTERN(patterns::trace_ray_entity),
                     addresses::globals::game_trace_manager, &ray, &start, &end,
                     target_entity, &filter, &result);

  return result;
}

Tracing::Filter Tracing::MakeFilter(std::uintptr_t skip_entity,
                                    std::uintptr_t mask, std::uint8_t layer,
                                    int type) const {
  Filter filter{};

  memory::Call<void>(PATTERN(patterns::trace_filter_init), &filter, skip_entity,
                     mask, layer, type);

  return filter;
}

Tracing::Filter Tracing::MakeFilter(std::uintptr_t skip_entity,
                                    std::uintptr_t mask,
                                    std::uint8_t layer) const {
  Filter filter{};

  memory::Call<void>(PATTERN(patterns::trace_filter_init), &filter, skip_entity,
                     mask, layer, 7);

  return filter;
}

Tracing::PlayerMovementFilter Tracing::MakePlayerMovementFilter(
    std::uintptr_t entity, std::uint8_t collision_group) const {
  PlayerMovementFilter filter{};
  const auto collision =
      entity + SCHEMA("C_BaseModelEntity", "m_Collision"_hash);
  const auto attributes =
      collision + SCHEMA("CCollisionProperty", "m_collisionAttribute"_hash);
  auto mask = memory::Read<std::uint64_t>(
      attributes +
      SCHEMA("VPhysicsCollisionAttribute_t", "m_nInteractsWith"_hash));
  const auto flags = memory::Read<std::uint32_t>(
      entity + SCHEMA("C_BaseEntity", "m_fFlags"_hash));
  // Match the native movement filter's extra contents for flag bit 4.
  constexpr auto kExtraContentsFlag = 0x10u;
  constexpr auto kExtraContents = 0x20ull;
  if (flags & kExtraContentsFlag) {
    mask |= kExtraContents;
  }

  memory::Call<void>(PATTERN(patterns::trace_filter_set_collision), &filter,
                     entity, mask, static_cast<int>(collision_group));

  return filter;
}

Tracing::Result Tracing::TracePlayerBbox(
    const math::Vector3& start, const math::Vector3& end,
    const BboxCollision& bbox, const PlayerMovementFilter& filter,
    std::uintptr_t movement_services) const {
  Result result{};

  // Native CCSPlayer_MovementServices callers pass their cached trace context.
  constexpr std::uintptr_t kTraceContextOffset = 0x7b0;
  // The native trace temporarily modifies the filter during its cache lookup.
  auto mutable_filter = filter;
  memory::Call<void>(PATTERN(patterns::trace_hull),
                     movement_services + kTraceContextOffset, &result, &start,
                     &end, &bbox, &mutable_filter);

  return result;
}

void Tracing::SetupTrace(TraceData* trace_data, const math::Vector3& start,
                         const math::Vector3& delta, const Filter& filter,
                         int penetration_count, bool trace_world) const {
  memory::Call<void>(PATTERN(patterns::trace_bullet_data_init), trace_data,
                     start, delta, filter, penetration_count, trace_world);
}

void Tracing::InitResult(Result* trace_result) const {
  memory::Call<void>(PATTERN(patterns::trace_bullet_free), trace_result);
}

void Tracing::FinalizeTrace(TraceData* trace_data, Result* hit,
                            float unknown_float, void* unknown) const {
  memory::Call<void>(PATTERN(patterns::trace_bullet_update), trace_data, hit,
                     unknown_float, unknown);
}

}  // namespace systems
