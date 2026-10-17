#include "pch/pch.h"

#include "core/features/features.h"
#include "core/rendering/rendering.h"
#include "core/settings.h"
#include "utilities/addresses/addresses.h"
#include "utilities/memory/memory.h"
#include "utilities/steam/steam.h"

namespace features::esp::other {

namespace detail {

struct AvatarCache {
  struct Entry {
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texture{};
    bool attempted{};
  };

  std::unordered_map<std::uintptr_t, Entry> m_entries{};

  [[nodiscard]] ID3D11ShaderResourceView* Get(std::uintptr_t steam_id) {
    auto it = this->m_entries.find(steam_id);
    if (it != this->m_entries.end()) {
      return it->second.texture.Get();
    }

    auto& e = this->m_entries[steam_id];
    e.attempted = true;

    const auto image_handle = steam::Friends::GetMediumFriendAvatar(steam_id);
    if (image_handle <= 0) {
      return nullptr;
    }

    std::uint32_t w{}, h{};
    if (!steam::Utils::GetImageSize(image_handle, &w, &h) || !w || !h) {
      return nullptr;
    }

    std::vector<std::uint8_t> rgba(w * h * 4);
    if (!steam::Utils::GetImageRgba(image_handle, rgba.data(),
                                    static_cast<int>(rgba.size()))) {
      return nullptr;
    }

    e.texture = xdraw::create_srv_from_rgba(rgba.data(), static_cast<int>(w),
                                            static_cast<int>(h));
    return e.texture.Get();
  }

  void Clear() { this->m_entries.clear(); }
};

constexpr unsigned char kSpectatorIcon[1030]{
    0x3C, 0x73, 0x76, 0x67, 0x20, 0x77, 0x69, 0x64, 0x74, 0x68, 0x3D, 0x22,
    0x31, 0x32, 0x22, 0x20, 0x68, 0x65, 0x69, 0x67, 0x68, 0x74, 0x3D, 0x22,
    0x31, 0x32, 0x22, 0x20, 0x76, 0x69, 0x65, 0x77, 0x42, 0x6F, 0x78, 0x3D,
    0x22, 0x30, 0x20, 0x30, 0x20, 0x31, 0x32, 0x20, 0x31, 0x32, 0x22, 0x20,
    0x66, 0x69, 0x6C, 0x6C, 0x3D, 0x22, 0x6E, 0x6F, 0x6E, 0x65, 0x22, 0x20,
    0x78, 0x6D, 0x6C, 0x6E, 0x73, 0x3D, 0x22, 0x68, 0x74, 0x74, 0x70, 0x3A,
    0x2F, 0x2F, 0x77, 0x77, 0x77, 0x2E, 0x77, 0x33, 0x2E, 0x6F, 0x72, 0x67,
    0x2F, 0x32, 0x30, 0x30, 0x30, 0x2F, 0x73, 0x76, 0x67, 0x22, 0x3E, 0x0D,
    0x0A, 0x3C, 0x67, 0x20, 0x63, 0x6C, 0x69, 0x70, 0x2D, 0x70, 0x61, 0x74,
    0x68, 0x3D, 0x22, 0x75, 0x72, 0x6C, 0x28, 0x23, 0x63, 0x6C, 0x69, 0x70,
    0x30, 0x5F, 0x31, 0x35, 0x35, 0x5F, 0x32, 0x32, 0x33, 0x29, 0x22, 0x3E,
    0x0D, 0x0A, 0x3C, 0x70, 0x61, 0x74, 0x68, 0x20, 0x64, 0x3D, 0x22, 0x4D,
    0x35, 0x20, 0x36, 0x43, 0x35, 0x20, 0x36, 0x2E, 0x32, 0x36, 0x35, 0x32,
    0x32, 0x20, 0x35, 0x2E, 0x31, 0x30, 0x35, 0x33, 0x36, 0x20, 0x36, 0x2E,
    0x35, 0x31, 0x39, 0x35, 0x37, 0x20, 0x35, 0x2E, 0x32, 0x39, 0x32, 0x38,
    0x39, 0x20, 0x36, 0x2E, 0x37, 0x30, 0x37, 0x31, 0x31, 0x43, 0x35, 0x2E,
    0x34, 0x38, 0x30, 0x34, 0x33, 0x20, 0x36, 0x2E, 0x38, 0x39, 0x34, 0x36,
    0x34, 0x20, 0x35, 0x2E, 0x37, 0x33, 0x34, 0x37, 0x38, 0x20, 0x37, 0x20,
    0x36, 0x20, 0x37, 0x43, 0x36, 0x2E, 0x32, 0x36, 0x35, 0x32, 0x32, 0x20,
    0x37, 0x20, 0x36, 0x2E, 0x35, 0x31, 0x39, 0x35, 0x37, 0x20, 0x36, 0x2E,
    0x38, 0x39, 0x34, 0x36, 0x34, 0x20, 0x36, 0x2E, 0x37, 0x30, 0x37, 0x31,
    0x31, 0x20, 0x36, 0x2E, 0x37, 0x30, 0x37, 0x31, 0x31, 0x43, 0x36, 0x2E,
    0x38, 0x39, 0x34, 0x36, 0x34, 0x20, 0x36, 0x2E, 0x35, 0x31, 0x39, 0x35,
    0x37, 0x20, 0x37, 0x20, 0x36, 0x2E, 0x32, 0x36, 0x35, 0x32, 0x32, 0x20,
    0x37, 0x20, 0x36, 0x43, 0x37, 0x20, 0x35, 0x2E, 0x37, 0x33, 0x34, 0x37,
    0x38, 0x20, 0x36, 0x2E, 0x38, 0x39, 0x34, 0x36, 0x34, 0x20, 0x35, 0x2E,
    0x34, 0x38, 0x30, 0x34, 0x33, 0x20, 0x36, 0x2E, 0x37, 0x30, 0x37, 0x31,
    0x31, 0x20, 0x35, 0x2E, 0x32, 0x39, 0x32, 0x38, 0x39, 0x43, 0x36, 0x2E,
    0x35, 0x31, 0x39, 0x35, 0x37, 0x20, 0x35, 0x2E, 0x31, 0x30, 0x35, 0x33,
    0x36, 0x20, 0x36, 0x2E, 0x32, 0x36, 0x35, 0x32, 0x32, 0x20, 0x35, 0x20,
    0x36, 0x20, 0x35, 0x43, 0x35, 0x2E, 0x37, 0x33, 0x34, 0x37, 0x38, 0x20,
    0x35, 0x20, 0x35, 0x2E, 0x34, 0x38, 0x30, 0x34, 0x33, 0x20, 0x35, 0x2E,
    0x31, 0x30, 0x35, 0x33, 0x36, 0x20, 0x35, 0x2E, 0x32, 0x39, 0x32, 0x38,
    0x39, 0x20, 0x35, 0x2E, 0x32, 0x39, 0x32, 0x38, 0x39, 0x43, 0x35, 0x2E,
    0x31, 0x30, 0x35, 0x33, 0x36, 0x20, 0x35, 0x2E, 0x34, 0x38, 0x30, 0x34,
    0x33, 0x20, 0x35, 0x20, 0x35, 0x2E, 0x37, 0x33, 0x34, 0x37, 0x38, 0x20,
    0x35, 0x20, 0x36, 0x5A, 0x22, 0x20, 0x73, 0x74, 0x72, 0x6F, 0x6B, 0x65,
    0x3D, 0x22, 0x23, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x22, 0x20, 0x73,
    0x74, 0x72, 0x6F, 0x6B, 0x65, 0x2D, 0x6C, 0x69, 0x6E, 0x65, 0x63, 0x61,
    0x70, 0x3D, 0x22, 0x72, 0x6F, 0x75, 0x6E, 0x64, 0x22, 0x20, 0x73, 0x74,
    0x72, 0x6F, 0x6B, 0x65, 0x2D, 0x6C, 0x69, 0x6E, 0x65, 0x6A, 0x6F, 0x69,
    0x6E, 0x3D, 0x22, 0x72, 0x6F, 0x75, 0x6E, 0x64, 0x22, 0x2F, 0x3E, 0x0D,
    0x0A, 0x3C, 0x70, 0x61, 0x74, 0x68, 0x20, 0x64, 0x3D, 0x22, 0x4D, 0x37,
    0x2E, 0x35, 0x31, 0x35, 0x20, 0x38, 0x2E, 0x37, 0x33, 0x39, 0x43, 0x37,
    0x2E, 0x30, 0x32, 0x39, 0x32, 0x34, 0x20, 0x38, 0x2E, 0x39, 0x31, 0x34,
    0x32, 0x36, 0x20, 0x36, 0x2E, 0x35, 0x31, 0x36, 0x34, 0x31, 0x20, 0x39,
    0x2E, 0x30, 0x30, 0x32, 0x36, 0x31, 0x20, 0x36, 0x20, 0x39, 0x43, 0x34,
    0x2E, 0x32, 0x20, 0x39, 0x20, 0x32, 0x2E, 0x37, 0x20, 0x38, 0x20, 0x31,
    0x2E, 0x35, 0x20, 0x36, 0x43, 0x32, 0x2E, 0x37, 0x20, 0x34, 0x20, 0x34,
    0x2E, 0x32, 0x20, 0x33, 0x20, 0x36, 0x20, 0x33, 0x43, 0x37, 0x2E, 0x38,
    0x20, 0x33, 0x20, 0x39, 0x2E, 0x33, 0x20, 0x34, 0x20, 0x31, 0x30, 0x2E,
    0x35, 0x20, 0x36, 0x43, 0x31, 0x30, 0x2E, 0x34, 0x35, 0x37, 0x38, 0x20,
    0x36, 0x2E, 0x30, 0x37, 0x30, 0x33, 0x35, 0x20, 0x31, 0x30, 0x2E, 0x34,
    0x31, 0x34, 0x38, 0x20, 0x36, 0x2E, 0x31, 0x34, 0x30, 0x31, 0x39, 0x20,
    0x31, 0x30, 0x2E, 0x33, 0x37, 0x31, 0x20, 0x36, 0x2E, 0x32, 0x30, 0x39,
    0x35, 0x22, 0x20, 0x73, 0x74, 0x72, 0x6F, 0x6B, 0x65, 0x3D, 0x22, 0x23,
    0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x22, 0x20, 0x73, 0x74, 0x72, 0x6F,
    0x6B, 0x65, 0x2D, 0x6C, 0x69, 0x6E, 0x65, 0x63, 0x61, 0x70, 0x3D, 0x22,
    0x72, 0x6F, 0x75, 0x6E, 0x64, 0x22, 0x20, 0x73, 0x74, 0x72, 0x6F, 0x6B,
    0x65, 0x2D, 0x6C, 0x69, 0x6E, 0x65, 0x6A, 0x6F, 0x69, 0x6E, 0x3D, 0x22,
    0x72, 0x6F, 0x75, 0x6E, 0x64, 0x22, 0x2F, 0x3E, 0x0D, 0x0A, 0x3C, 0x70,
    0x61, 0x74, 0x68, 0x20, 0x64, 0x3D, 0x22, 0x4D, 0x39, 0x2E, 0x35, 0x20,
    0x38, 0x56, 0x39, 0x2E, 0x35, 0x22, 0x20, 0x73, 0x74, 0x72, 0x6F, 0x6B,
    0x65, 0x3D, 0x22, 0x23, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x22, 0x20,
    0x73, 0x74, 0x72, 0x6F, 0x6B, 0x65, 0x2D, 0x6C, 0x69, 0x6E, 0x65, 0x63,
    0x61, 0x70, 0x3D, 0x22, 0x72, 0x6F, 0x75, 0x6E, 0x64, 0x22, 0x20, 0x73,
    0x74, 0x72, 0x6F, 0x6B, 0x65, 0x2D, 0x6C, 0x69, 0x6E, 0x65, 0x6A, 0x6F,
    0x69, 0x6E, 0x3D, 0x22, 0x72, 0x6F, 0x75, 0x6E, 0x64, 0x22, 0x2F, 0x3E,
    0x0D, 0x0A, 0x3C, 0x70, 0x61, 0x74, 0x68, 0x20, 0x64, 0x3D, 0x22, 0x4D,
    0x39, 0x2E, 0x35, 0x20, 0x31, 0x31, 0x56, 0x31, 0x31, 0x2E, 0x30, 0x30,
    0x35, 0x22, 0x20, 0x73, 0x74, 0x72, 0x6F, 0x6B, 0x65, 0x3D, 0x22, 0x23,
    0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x22, 0x20, 0x73, 0x74, 0x72, 0x6F,
    0x6B, 0x65, 0x2D, 0x6C, 0x69, 0x6E, 0x65, 0x63, 0x61, 0x70, 0x3D, 0x22,
    0x72, 0x6F, 0x75, 0x6E, 0x64, 0x22, 0x20, 0x73, 0x74, 0x72, 0x6F, 0x6B,
    0x65, 0x2D, 0x6C, 0x69, 0x6E, 0x65, 0x6A, 0x6F, 0x69, 0x6E, 0x3D, 0x22,
    0x72, 0x6F, 0x75, 0x6E, 0x64, 0x22, 0x2F, 0x3E, 0x0D, 0x0A, 0x3C, 0x2F,
    0x67, 0x3E, 0x0D, 0x0A, 0x3C, 0x64, 0x65, 0x66, 0x73, 0x3E, 0x0D, 0x0A,
    0x3C, 0x63, 0x6C, 0x69, 0x70, 0x50, 0x61, 0x74, 0x68, 0x20, 0x69, 0x64,
    0x3D, 0x22, 0x63, 0x6C, 0x69, 0x70, 0x30, 0x5F, 0x31, 0x35, 0x35, 0x5F,
    0x32, 0x32, 0x33, 0x22, 0x3E, 0x0D, 0x0A, 0x3C, 0x72, 0x65, 0x63, 0x74,
    0x20, 0x77, 0x69, 0x64, 0x74, 0x68, 0x3D, 0x22, 0x31, 0x32, 0x22, 0x20,
    0x68, 0x65, 0x69, 0x67, 0x68, 0x74, 0x3D, 0x22, 0x31, 0x32, 0x22, 0x20,
    0x66, 0x69, 0x6C, 0x6C, 0x3D, 0x22, 0x77, 0x68, 0x69, 0x74, 0x65, 0x22,
    0x2F, 0x3E, 0x0D, 0x0A, 0x3C, 0x2F, 0x63, 0x6C, 0x69, 0x70, 0x50, 0x61,
    0x74, 0x68, 0x3E, 0x0D, 0x0A, 0x3C, 0x2F, 0x64, 0x65, 0x66, 0x73, 0x3E,
    0x0D, 0x0A, 0x3C, 0x2F, 0x73, 0x76, 0x67, 0x3E, 0x0D, 0x0A};

}  // namespace detail

void Overlay::OnRender(xdraw::draw_list& draw_list) {
  this->AddSpectators(draw_list);
  this->AddBomb(draw_list);
}

void Overlay::AddBomb(xdraw::draw_list& draw_list) {
  const auto local = systems::g_local.Get();
  if (!local.IsValid() || !systems::g_entities.Exists(local.ViewController())) {
    return;
  }

  const auto planted_c4 =
      memory::Read<std::uintptr_t>(addresses::globals::planted_c4);
  const auto global_vars =
      memory::Read<std::uintptr_t>(addresses::globals::global_vars);

  if (!planted_c4 || !global_vars) {
    return;
  }

  const auto current_time = memory::Read<float>(global_vars + 0x30);
  const auto blow_time = memory::Read<float>(
      planted_c4 + SCHEMA("C_PlantedC4", "m_flC4Blow"_hash));
  const auto has_exploded = memory::Read<bool>(
      planted_c4 + SCHEMA("C_PlantedC4", "m_bHasExploded"_hash));
  const auto bomb_defused = memory::Read<bool>(
      planted_c4 + SCHEMA("C_PlantedC4", "m_bBombDefused"_hash));

  if (bomb_defused) {
    return;
  }

  const auto time_remaining = blow_time - current_time;
  const auto is_exploding = has_exploded || time_remaining <= 0.0f;

  if (is_exploding && time_remaining < -2.0f) {
    return;
  }

  const auto bomb_site =
      memory::Read<int>(planted_c4 + SCHEMA("C_PlantedC4", "m_nBombSite"_hash));
  const auto being_defused = memory::Read<bool>(
      planted_c4 + SCHEMA("C_PlantedC4", "m_bBeingDefused"_hash));
  const auto timer_length = memory::Read<float>(
      planted_c4 + SCHEMA("C_PlantedC4", "m_flTimerLength"_hash));

  const auto calculate_bomb_damage = [&]() -> float {
    const auto view_pawn = local.ViewPawn();
    if (!view_pawn) {
      return 0.0f;
    }

    const auto c4_scene_node = memory::Read<std::uintptr_t>(
        planted_c4 + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
    const auto pawn_scene_node = memory::Read<std::uintptr_t>(
        view_pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));

    if (!c4_scene_node || !pawn_scene_node) {
      return 0.0f;
    }

    const auto c4_origin = memory::Read<math::Vector3>(
        c4_scene_node + SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_hash));
    const auto pawn_origin = memory::Read<math::Vector3>(
        pawn_scene_node + SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_hash));

    const auto distance = (c4_origin - pawn_origin).Length();

    constexpr auto kDefaultDamage{650.0f};
    constexpr auto kDefaultRadius{2275.0f};

    const auto sigma = kDefaultRadius / 3.0f;
    auto damage = kDefaultDamage *
                  std::exp(-(distance * distance) / (2.0f * sigma * sigma));

    const auto armor = memory::Read<int>(
        view_pawn + SCHEMA("C_CSPlayerPawn", "m_ArmorValue"_hash));

    if (armor > 0) {
      constexpr auto kArmorRatio = 0.5f;
      constexpr auto kArmorBonus = 0.5f;

      auto armor_absorbed = damage * kArmorRatio;
      auto armor_cost = (damage - armor_absorbed) * kArmorBonus;

      if (armor_cost > static_cast<float>(armor)) {
        armor_cost = static_cast<float>(armor) * (1.0f / kArmorBonus);
        armor_absorbed = damage - armor_cost;
      }

      damage = armor_absorbed;
    }

    return std::floor(damage);
  }();

  const auto [screen_w, screen_h] = xdraw::viewport_size();
  const auto& s = xui::ctx().style;

  constexpr auto kH{24.0f};
  constexpr auto kTopOffset{175.0f};
  constexpr auto kR{8.0f};
  constexpr auto kInnerR{6.0f};
  constexpr auto kInnerPad{2.0f};
  constexpr auto kTextPadX{8.0f};
  constexpr auto kTextNudge{0.5f};
  constexpr auto kSectionSpacing{2.0f};

  const auto inner_h = kH - kInnerPad * 2.0f;

  auto timer_color = [&]() -> xdraw::color {
    if (is_exploding) {
      return {255, 100, 100, 255};
    }

    const auto frac =
        timer_length > 0.0f ? time_remaining / timer_length : 1.0f;

    if (frac > 0.5f) {
      return s.accent;
    } else if (frac > 0.2f) {
      const auto t = (frac - 0.2f) / 0.3f;

      return {static_cast<std::uint8_t>(255),
              static_cast<std::uint8_t>(
                  200 + static_cast<int>((s.accent.g - 200) * t)),
              static_cast<std::uint8_t>(
                  140 + static_cast<int>((s.accent.b - 140) * t)),
              255};
    } else {
      const auto t = frac / 0.2f;

      return {255, static_cast<std::uint8_t>(120 + static_cast<int>(80 * t)),
              static_cast<std::uint8_t>(100 + static_cast<int>(40 * t)), 255};
    }
  }();

  const auto site_label = bomb_site == 0 ? "A plant" : "B plant";
  const auto [site_tw, site_th] = xdraw::measure_text(site_label);
  const auto site_pill_w = site_tw + kTextPadX * 2.0f;

  const auto damage = static_cast<int>(calculate_bomb_damage);
  const auto view_pawn = local.ViewPawn();
  const auto health =
      view_pawn ? memory::Read<int>(view_pawn +
                                    SCHEMA("C_BaseEntity", "m_iHealth"_hash))
                : 0;
  const auto will_kill = health <= damage;

  char health_buf[16]{};
  std::snprintf(health_buf, sizeof(health_buf), "%+d", -damage);

  const auto [health_vw, health_vh] = xdraw::measure_text(health_buf);
  const auto [health_uw, health_uh] = xdraw::measure_text(" health");
  const auto health_pill_w = health_vw + health_uw + kTextPadX * 2.0f;
  const auto health_col = will_kill ? xdraw::color{255, 120, 120, 255}
                                    : xdraw::color{160, 210, 140, 255};

  char timer_buf[16]{};
  const char* timer_unit{};

  if (is_exploding) {
    strncpy_s(timer_buf, sizeof(timer_buf), "0.0s", _TRUNCATE);
    timer_unit = " exploding";
  } else {
    std::snprintf(timer_buf, sizeof(timer_buf), "%.1fs", time_remaining);
    timer_unit = being_defused ? " defusing" : " explosion";
  }

  const auto [timer_vw, timer_vh] = xdraw::measure_text(timer_buf);
  const auto [timer_uw, timer_uh] = xdraw::measure_text(timer_unit);
  const auto timer_pill_w = timer_vw + timer_uw + kTextPadX * 2.0f;

  const auto total_w = kInnerPad + site_pill_w + kSectionSpacing +
                       health_pill_w + kSectionSpacing + timer_pill_w +
                       kInnerPad;
  const auto x = (static_cast<float>(screen_w) - total_w) * 0.5f;
  const auto y = kTopOffset;

  draw_list.rect_filled_blurred(x, y, total_w, kH, xdraw::corner_radius{kR});
  draw_list.rect_filled(x, y, total_w, kH, s.window_bg,
                        xdraw::corner_radius{kR});

  auto cx = x + kInnerPad;

  draw_list.rect_filled(cx, y + kInnerPad, site_pill_w, inner_h, s.child_bg,
                        xdraw::corner_radius{kInnerR});
  draw_list.text(cx + kTextPadX, y + (kH - site_th) * 0.5f + kTextNudge,
                 site_label, s.accent);
  cx += site_pill_w + kSectionSpacing;

  const auto health_unit_col =
      xdraw::color{health_col.r, health_col.g, health_col.b, 120};
  draw_list.rect_filled(cx, y + kInnerPad, health_pill_w, inner_h, s.child_bg,
                        xdraw::corner_radius{kInnerR});
  draw_list.text(cx + kTextPadX, y + (kH - health_vh) * 0.5f + kTextNudge,
                 health_buf, health_col);
  draw_list.text(cx + kTextPadX + health_vw,
                 y + (kH - health_uh) * 0.5f + kTextNudge, " health",
                 health_unit_col);
  cx += health_pill_w + kSectionSpacing;

  const auto timer_unit_col =
      xdraw::color{timer_color.r, timer_color.g, timer_color.b, 120};
  draw_list.rect_filled(cx, y + kInnerPad, timer_pill_w, inner_h, s.child_bg,
                        xdraw::corner_radius{kInnerR});
  draw_list.text(cx + kTextPadX, y + (kH - timer_vh) * 0.5f + kTextNudge,
                 timer_buf, timer_color);
  draw_list.text(cx + kTextPadX + timer_vw,
                 y + (kH - timer_uh) * 0.5f + kTextNudge, timer_unit,
                 timer_unit_col);
}

void Overlay::AddSpectators(xdraw::draw_list& draw_list) {
  const auto local = systems::g_local.Get();
  if (!local.IsValid() || !systems::g_entities.Exists(local.ViewController())) {
    return;
  }

  const auto game_rules =
      memory::Read<std::uintptr_t>(addresses::globals::game_rules);
  if (!game_rules ||
      memory::Read<int>(game_rules +
                        SCHEMA("C_CSGameRules", "m_gamePhase"_hash)) >= 4) {
    return;
  }

  const auto local_controller = local.controller;
  const auto view_controller = local.ViewController();
  const auto view_pawn = local.ViewPawn();
  if (!view_pawn) {
    return;
  }

  const auto [screen_w, screen_h] = xdraw::viewport_size();
  const auto& s = xui::ctx().style;

  constexpr auto kMargin{10.0f};
  constexpr auto kRowSpacing{3.0f};
  constexpr auto kRowH{24.0f};
  constexpr auto kHeaderH{24.0f};
  constexpr auto kR{8.0f};
  constexpr auto kInnerR{6.0f};
  constexpr auto kInnerPad{2.0f};
  constexpr auto kTextPadX{8.0f};
  constexpr auto kTextNudge{0.5f};
  constexpr auto kIconSize{20.0f};
  constexpr auto kIconInnerPad{4.0f};
  constexpr auto kAvatarPillSize{20.0f};

  struct SpectatorEntry {
    char name[128];
    std::uintptr_t steam_id;
  };

  SpectatorEntry entries[32]{};
  auto count{0};

  for (const auto& player :
       systems::g_entities.GetByType(systems::Entities::Type::kPlayer)) {
    if (player.ptr == view_controller || player.ptr == local_controller ||
        count >= 32) {
      continue;
    }

    if (memory::Read<bool>(player.ptr + SCHEMA("CCSPlayerController",
                                               "m_bPawnIsAlive"_hash))) {
      continue;
    }

    const auto obs_pawn_handle = memory::Read<std::uint32_t>(
        player.ptr + SCHEMA("CCSPlayerController", "m_hObserverPawn"_hash));
    if (!obs_pawn_handle || obs_pawn_handle == 0xffffffff) {
      continue;
    }

    const auto obs_pawn = systems::g_entities.Lookup(obs_pawn_handle);
    if (!obs_pawn) {
      continue;
    }

    const auto observer_services =
        memory::SafeRead<std::uintptr_t>(
            obs_pawn + SCHEMA("C_BasePlayerPawn", "m_pObserverServices"_hash))
            .value_or(0);
    if (!observer_services || (observer_services >> 48) != 0) {
      continue;
    }

    const auto observer_target_handle =
        memory::SafeRead<std::uint32_t>(
            observer_services +
            SCHEMA("CPlayer_ObserverServices", "m_hObserverTarget"_hash))
            .value_or(0);
    if (!observer_target_handle) {
      continue;
    }

    const auto observer_target =
        systems::g_entities.Lookup(observer_target_handle);
    if (observer_target != view_pawn) {
      continue;
    }

    const auto name_ptr = memory::Read<std::uintptr_t>(
        player.ptr +
        SCHEMA("CCSPlayerController", "m_sSanitizedPlayerName"_hash));
    if (!name_ptr) {
      continue;
    }

    auto name = memory::ReadString(name_ptr, 127);
    std::ranges::transform(name, name.begin(),
                           [](unsigned char c) { return std::tolower(c); });

    auto& e = entries[count++];
    strncpy_s(e.name, name.c_str(), sizeof(e.name) - 1);

    e.name[sizeof(e.name) - 1] = '\0';
    e.steam_id = memory::Read<std::uintptr_t>(
        player.ptr + SCHEMA("CBasePlayerController", "m_steamID"_hash));
  }

  if (count <= 0) {
    return;
  }

  static detail::AvatarCache avatars{};

  static auto icon_w_px = 0, icon_h_px = 0;
  static const auto eye_icon = xdraw::load_svg(
      std::span<const std::byte>(
          reinterpret_cast<const std::byte*>(detail::kSpectatorIcon),
          sizeof(detail::kSpectatorIcon)),
      1.0f, &icon_w_px, &icon_h_px);

  const auto [header_tw, header_th] = xdraw::measure_text("spectators");
  const auto inner_h = kRowH - kInnerPad * 2.0f;
  const auto header_inner_h = kHeaderH - kInnerPad * 2.0f;

  const auto header_w = kInnerPad + header_tw + kTextPadX * 2.0f + kInnerPad +
                        kIconSize + kInnerPad;
  const auto x = static_cast<float>(screen_w) - header_w - kMargin;
  auto ry = (static_cast<float>(screen_h) * 0.5f) -
            ((kHeaderH + kRowSpacing +
              static_cast<float>(count) * (kRowH + kRowSpacing)) *
             0.5f);

  draw_list.rect_filled_blurred(x, ry, header_w, kHeaderH,
                                xdraw::corner_radius{kR});
  draw_list.rect_filled(x, ry, header_w, kHeaderH, s.window_bg,
                        xdraw::corner_radius{kR});

  const auto htx = x + kInnerPad;
  const auto htw = header_tw + kTextPadX * 2.0f;
  draw_list.rect_filled(htx, ry + kInnerPad, htw, header_inner_h, s.child_bg,
                        xdraw::corner_radius{kInnerR});
  draw_list.text(htx + kTextPadX,
                 ry + (kHeaderH - header_th) * 0.5f + kTextNudge, "spectators",
                 s.accent);

  const auto icon_x = x + kInnerPad + htw + kInnerPad;
  draw_list.rect_filled(icon_x, ry + kInnerPad, kIconSize, header_inner_h,
                        s.accent, xdraw::corner_radius{kInnerR});

  if (eye_icon) {
    const auto icon_draw = kIconSize - kIconInnerPad * 2.0f;
    const auto ix = std::floor(icon_x + kIconInnerPad);
    const auto iy =
        std::floor(ry + kInnerPad + (header_inner_h - icon_draw) * 0.5f + 1.0f);
    draw_list.image(ix, iy, icon_draw, icon_draw, eye_icon.Get(),
                    s.checkbox_mark_icon);
  }

  ry += kHeaderH + kRowSpacing;

  for (auto i = 0; i < count; ++i) {
    const auto& e = entries[i];
    const auto [nw, nh] = xdraw::measure_text(e.name);
    const auto avatar_tex = avatars.Get(e.steam_id);
    const auto has_avatar = avatar_tex != nullptr;

    const auto name_pill_w = nw + kTextPadX * 2.0f;
    const auto row_w = kInnerPad + name_pill_w +
                       (has_avatar ? kInnerPad + kAvatarPillSize : 0.0f) +
                       kInnerPad;
    const auto rx = static_cast<float>(screen_w) - row_w - kMargin;

    draw_list.rect_filled_blurred(rx, ry, row_w, kRowH,
                                  xdraw::corner_radius{kR});
    draw_list.rect_filled(rx, ry, row_w, kRowH, s.window_bg,
                          xdraw::corner_radius{kR});

    auto cx = rx + kInnerPad;

    draw_list.rect_filled(cx, ry + kInnerPad, name_pill_w, inner_h, s.child_bg,
                          xdraw::corner_radius{kInnerR});
    draw_list.text(cx + kTextPadX, ry + (kRowH - nh) * 0.5f + kTextNudge,
                   e.name, s.accent);
    cx += name_pill_w;

    if (has_avatar) {
      cx += kInnerPad;
      const auto avatar_draw = inner_h;
      const auto ay = ry + kInnerPad;
      draw_list.image(cx, ay, avatar_draw, avatar_draw, avatar_tex,
                      xdraw::corner_radius{kInnerR},
                      xdraw::color{255, 255, 255, 255});
    }

    ry += kRowH + kRowSpacing;
  }
}

}  // namespace features::esp::other
