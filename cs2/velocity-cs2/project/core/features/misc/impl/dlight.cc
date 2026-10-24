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

constexpr std::uintptr_t kMinPtr = 0x10000000ull;
constexpr std::uintptr_t kMaxPtr = 0x7FFFFFFFFFFFull;
constexpr std::uint32_t kLightKey = 0x564C4954u;  // "VLIT"
constexpr std::uint32_t kColorExponent = 5u;
constexpr float kRgbChannelScale = 1.0f / 255.0f;
constexpr float kEntryLifetime = 5.0f;

[[nodiscard]] inline bool ValidPtr(std::uintptr_t p) noexcept {
  return p >= kMinPtr && p <= kMaxPtr;
}

}  // namespace

void Dlight::OnPresent() {
  const std::lock_guard lock(this->m_mutex_);

  // Settings are edited on Present. Publish a coherent snapshot for the game
  // thread without touching the engine's light manager from the render thread.
  const auto& cfg = settings::g_misc.m_dlight;
  const auto& c = cfg.color.value;
  this->m_config_.enabled = cfg.enabled.value;
  this->m_config_.packed_color = static_cast<std::uint32_t>(c.r) |
                                 (static_cast<std::uint32_t>(c.g) << 8u) |
                                 (static_cast<std::uint32_t>(c.b) << 16u) |
                                 (kColorExponent << 24u);
  this->m_config_.radius = std::max(cfg.radius.value, 1.0f);
  this->m_config_.z_offset = cfg.z_offset.value;
}

void Dlight::ApplySceneColor(std::uintptr_t object) const {
  if (!object ||
      this->m_scene_object_.load(std::memory_order_acquire) != object) {
    return;
  }

  const auto packed = this->m_packed_color_.load(std::memory_order_relaxed);
  const auto exponent = static_cast<std::int8_t>(packed >> 24u);
  const auto scale =
      std::ldexp(1.0f, exponent) *
      this->m_scene_color_scale_.load(std::memory_order_relaxed) *
      kRgbChannelScale;
  // Source 2 normalizes RGBExp32 byte channels, then scales them by radius
  // when deriving linear attenuation. Preserve both parts for only our light.
  const math::Vector3 color{
      static_cast<float>(packed & 0xFFu) * scale,
      static_cast<float>((packed >> 8u) & 0xFFu) * scale,
      static_cast<float>((packed >> 16u) & 0xFFu) * scale};

  (void)memory::SafeWrite<math::Vector3>(object + 0xE4, color);
}

void Dlight::RetireEntry() {
  if (ValidPtr(this->m_manager_) && ValidPtr(this->m_entry_) &&
      memory::SafeRead<std::uint32_t>(this->m_entry_ + 0x24).value_or(0) ==
          kLightKey) {
    // The allocator owns the manager lock and releases the scene object
    // attached to an existing keyed entry before clearing the slot.
    if (const auto alloc_light = PATTERN(patterns::dynamic_light_alloc)) {
      (void)memory::Call<std::uintptr_t>(alloc_light, this->m_manager_,
                                         kLightKey, 0);
    }
  }

  this->m_manager_ = 0;
  this->m_entry_ = 0;
  this->m_scene_object_.store(0, std::memory_order_release);
  this->m_packed_color_.store(0, std::memory_order_relaxed);
  this->m_scene_color_scale_.store(0.0f, std::memory_order_relaxed);
  this->m_logged_submission_ = false;
  this->m_logged_scene_ = false;
}

void Dlight::OnLevelShutdown() {
  const std::lock_guard lock(this->m_mutex_);

  // Retire while the scene system is still alive. Keeping the engine entry
  // across teardown leaves its queued scene object pointing at a dead parent.
  this->RetireEntry();
}

void Dlight::OnFrameStageNotify() {
  const std::lock_guard lock(this->m_mutex_);

  const auto cfg = this->m_config_;
  const auto manager_slot = PATTERN(patterns::dynamic_light_manager);
  const auto manager =
      manager_slot ? memory::SafeRead<std::uintptr_t>(manager_slot).value_or(0)
                   : 0;
  const auto alloc_light = PATTERN(patterns::dynamic_light_alloc);
  const auto light_time = PATTERN(patterns::dynamic_light_time);

  if (!cfg.enabled) {
    if (manager == this->m_manager_) {
      this->RetireEntry();
    } else {
      this->m_manager_ = 0;
      this->m_entry_ = 0;
      this->m_scene_object_.store(0, std::memory_order_release);
      this->m_packed_color_.store(0, std::memory_order_relaxed);
      this->m_scene_color_scale_.store(0.0f, std::memory_order_relaxed);
    }
    return;
  }

  if (!ValidPtr(manager) || !alloc_light || !light_time) {
    this->m_manager_ = 0;
    this->m_entry_ = 0;
    this->m_scene_object_.store(0, std::memory_order_release);
    this->m_packed_color_.store(0, std::memory_order_relaxed);
    this->m_scene_color_scale_.store(0.0f, std::memory_order_relaxed);
    return;
  }

  const auto local = systems::g_local.Get();
  if (!local.controller || !local.pawn || !local.is_alive) {
    if (manager == this->m_manager_) {
      this->RetireEntry();
    } else {
      this->m_manager_ = 0;
      this->m_entry_ = 0;
      this->m_scene_object_.store(0, std::memory_order_release);
      this->m_packed_color_.store(0, std::memory_order_relaxed);
      this->m_scene_color_scale_.store(0.0f, std::memory_order_relaxed);
    }
    return;
  }

  const auto entry_key =
      ValidPtr(this->m_entry_)
          ? memory::SafeRead<std::uint32_t>(this->m_entry_ + 0x24).value_or(0)
          : 0;

  if (manager != this->m_manager_ || entry_key != kLightKey) {
    this->m_scene_object_.store(0, std::memory_order_release);
    this->m_packed_color_.store(0, std::memory_order_relaxed);
    this->m_scene_color_scale_.store(0.0f, std::memory_order_relaxed);
    this->m_entry_ =
        memory::Call<std::uintptr_t>(alloc_light, manager, kLightKey, 0);
    this->m_manager_ = manager;
  }

  if (!ValidPtr(this->m_entry_)) {
    return;
  }

  const auto game_scene =
      memory::SafeRead<std::uintptr_t>(
          local.pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash))
          .value_or(0);
  const auto feet =
      ValidPtr(game_scene)
          ? memory::SafeRead<math::Vector3>(
                game_scene + SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_hash))
          : std::nullopt;
  if (!feet || !std::isfinite(feet->x) || !std::isfinite(feet->y) ||
      !std::isfinite(feet->z)) {
    return;
  }

  float curtime{};
  memory::Call<float*>(light_time, &curtime, 0);

  const math::Vector3 origin{feet->x, feet->y, feet->z + cfg.z_offset};
  const auto write_ok =
      memory::SafeWrite<math::Vector3>(this->m_entry_ + 0x04, origin) &&
      memory::SafeWrite<float>(this->m_entry_ + 0x10, cfg.radius) &&
      memory::SafeWrite<std::uint32_t>(this->m_entry_ + 0x14, cfg.packed_color)
      // Update before Source 2 copies the entry into its scene-light
      // descriptor.
      && memory::SafeWrite<float>(this->m_entry_ + 0x18,
                                  curtime + kEntryLifetime) &&
      memory::SafeWrite<float>(this->m_entry_ + 0x1C, 0.0f);

  if (!write_ok) {
    this->m_manager_ = 0;
    this->m_entry_ = 0;
    this->m_scene_object_.store(0, std::memory_order_release);
    this->m_packed_color_.store(0, std::memory_order_relaxed);
    this->m_scene_color_scale_.store(0.0f, std::memory_order_relaxed);
    this->m_logged_submission_ = false;
    this->m_logged_scene_ = false;
    return;
  }

  if (!this->m_logged_submission_) {
    logging::console::Print(
        xs("[dlight] submitted; manager={:#x} entry={:#x} origin=({:.1f} "
           "{:.1f} {:.1f}) radius={:.0f} time={:.2f}"),
        manager, this->m_entry_, origin.x, origin.y, origin.z, cfg.radius,
        curtime);
    this->m_logged_submission_ = true;
  }

  this->m_packed_color_.store(cfg.packed_color, std::memory_order_relaxed);
  this->m_scene_color_scale_.store(cfg.radius, std::memory_order_relaxed);
  const auto scene_object =
      memory::SafeRead<std::uintptr_t>(this->m_entry_ + 0x48).value_or(0);
  this->m_scene_object_.store(scene_object, std::memory_order_release);
  if (!this->m_logged_scene_ && scene_object) {
    logging::console::Print(xs("[dlight] scene object active"));
    this->m_logged_scene_ = true;
  }
}

}  // namespace features::misc
