module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>
#include <external/xdraw/xdraw.hpp>

#include "core/systems/schema_macros.h"

module velocity;

import <cstddef>;
import <filesystem>;
import <limits>;
import <string_view>;
import <type_traits>;

namespace features::esp::player {

void Overlay::OnRender(xdraw::draw_list& draw_list) {
  if (!settings::g_esp.m_player.m_overlay[0].enabled.value &&
      !settings::g_esp.m_player.m_overlay[1].enabled.value) {
    return;
  }

  const auto local = systems::g_local.Get();
  if (!local.IsValid() || !systems::g_entities.Exists(local.ViewController())) {
    return;
  }

  auto players =
      systems::g_entities.GetByType(systems::Entities::Type::kPlayer);
  {
    const auto camera = systems::g_view.Origin();

    std::sort(players.begin(), players.end(),
              [&](const systems::Entities::Cached& a,
                  const systems::Entities::Cached& b) {
                const auto node_a = memory::Read<std::uintptr_t>(
                    a.ptr + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
                const auto node_b = memory::Read<std::uintptr_t>(
                    b.ptr + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));

                if (!node_a || !node_b) {
                  return !node_a && node_b;
                }

                const auto da =
                    node_a ? (memory::Read<math::Vector3>(
                                  node_a + SCHEMA("CGameSceneNode",
                                                  "m_vecAbsOrigin"_hash)) -
                              camera)
                                 .LengthSqr()
                           : 0.0f;
                const auto db =
                    node_b ? (memory::Read<math::Vector3>(
                                  node_b + SCHEMA("CGameSceneNode",
                                                  "m_vecAbsOrigin"_hash)) -
                              camera)
                                 .LengthSqr()
                           : 0.0f;

                return da > db;
              });
  }

  for (const auto& player : players) {
    const auto info = this->GetInfo(player, local);
    if (!info.Valid()) {
      continue;
    }

    const auto& cfg =
        settings::g_esp.m_player.m_overlay[info.is_other_team ? 0 : 1];
    if (!cfg.enabled.value) {
      continue;
    }

    if (cfg.m_oof_arrow.enabled.value) {
      this->AddOofArrow(draw_list, info, cfg.m_oof_arrow);
    }

    const auto bounds = systems::g_bounds.Get(info.pawn);
    if (!bounds.valid) {
      continue;
    }

    Overlay::DrawOffsets offsets{};

    if (cfg.m_box.enabled.value) {
      this->AddBox(draw_list, bounds, cfg.m_box, info.is_visible);
    }

    if (cfg.m_skeleton.enabled.value) {
      this->AddSkeleton(draw_list, info, cfg.m_skeleton, info.is_visible,
                        local);
    }

    if (cfg.m_health_bar.enabled.value) {
      this->AddHealthBar(draw_list, bounds, info, cfg.m_health_bar, offsets);
    }

    if (cfg.m_ammo_bar.enabled.value && info.weapon.Valid()) {
      this->AddAmmoBar(draw_list, bounds, info, cfg.m_ammo_bar, offsets);
    }

    if (cfg.m_name.enabled.value && !info.name.empty()) {
      this->AddName(draw_list, bounds, info, cfg.m_name, offsets);
    }

    if (cfg.m_weapon.enabled.value && !info.weapon.name.empty()) {
      this->AddWeapon(draw_list, bounds, info, cfg.m_weapon, offsets);
    }

    if (cfg.m_info_flags.enabled.value) {
      this->AddFlags(draw_list, bounds, info, cfg.m_info_flags, offsets);
    }
  }
}

void Overlay::AddBox(xdraw::draw_list& draw_list,
                     const systems::Bounds::Data& bounds,
                     const settings::Esp::Player::Overlay::Box& cfg,
                     bool visible) {
  const auto& color = visible ? cfg.visible_color : cfg.occluded_color;

  const auto x = std::floorf(bounds.min.x);
  const auto y = std::floorf(bounds.min.y);
  const auto w = std::floorf(bounds.max.x - bounds.min.x);
  const auto h = std::floorf(bounds.max.y - bounds.min.y);

  if (cfg.fill.value) {
    constexpr auto kEdgeAlpha{0.5f};
    constexpr auto kCenterAlpha{0.08f};
    constexpr auto kCenterBrightness{0.4f};
    constexpr auto kDesaturation{0.7f};

    const auto r = color.value.r / 255.0f;
    const auto g = color.value.g / 255.0f;
    const auto b = color.value.b / 255.0f;
    const auto avg = (r + g + b) / 3.0f;

    const auto edge_r = r * kDesaturation + avg * (1.0f - kDesaturation);
    const auto edge_g = g * kDesaturation + avg * (1.0f - kDesaturation);
    const auto edge_b = b * kDesaturation + avg * (1.0f - kDesaturation);
    const auto edge_color =
        xdraw::color(static_cast<std::uint8_t>(edge_r * 255),
                     static_cast<std::uint8_t>(edge_g * 255),
                     static_cast<std::uint8_t>(edge_b * 255),
                     static_cast<std::uint8_t>(255 * kEdgeAlpha));
    const auto center_color = xdraw::color(
        static_cast<std::uint8_t>(edge_r * 255 * kCenterBrightness),
        static_cast<std::uint8_t>(edge_g * 255 * kCenterBrightness),
        static_cast<std::uint8_t>(edge_b * 255 * kCenterBrightness),
        static_cast<std::uint8_t>(255 * kCenterAlpha));

    const auto mid_y = y + h * 0.5f;

    draw_list.rect_filled_gradient(x + 1, y + 1, w - 2, mid_y - y - 1,
                                   edge_color, edge_color, center_color,
                                   center_color);
    draw_list.rect_filled_gradient(x + 1, mid_y, w - 2, y + h - mid_y - 1,
                                   center_color, center_color, edge_color,
                                   edge_color);
  }

  if (cfg.style == settings::Esp::Player::Overlay::Box::StyleType::kFull) {
    auto draw_full_rect = [&](float rx, float ry, float rw, float rh,
                              const xdraw::color& col, float thickness) {
      const auto t = std::clamp(thickness, 0.0f, std::min(rw, rh) * 0.5f);

      if (t <= 0.0f) {
        return;
      }

      draw_list.ensure_cmd(nullptr);

      auto v = draw_list.emit_vtx(rx, ry, 0, 0, col);
      draw_list.emit_vtx(rx + rw, ry, 0, 0, col);
      draw_list.emit_vtx(rx + rw, ry + t, 0, 0, col);
      draw_list.emit_vtx(rx, ry + t, 0, 0, col);
      draw_list.emit_quad(v, v + 1, v + 2, v + 3);

      v = draw_list.emit_vtx(rx, ry + rh - t, 0, 0, col);
      draw_list.emit_vtx(rx + rw, ry + rh - t, 0, 0, col);
      draw_list.emit_vtx(rx + rw, ry + rh, 0, 0, col);
      draw_list.emit_vtx(rx, ry + rh, 0, 0, col);
      draw_list.emit_quad(v, v + 1, v + 2, v + 3);

      v = draw_list.emit_vtx(rx, ry + t, 0, 0, col);
      draw_list.emit_vtx(rx + t, ry + t, 0, 0, col);
      draw_list.emit_vtx(rx + t, ry + rh - t, 0, 0, col);
      draw_list.emit_vtx(rx, ry + rh - t, 0, 0, col);
      draw_list.emit_quad(v, v + 1, v + 2, v + 3);

      v = draw_list.emit_vtx(rx + rw - t, ry + t, 0, 0, col);
      draw_list.emit_vtx(rx + rw, ry + t, 0, 0, col);
      draw_list.emit_vtx(rx + rw, ry + rh - t, 0, 0, col);
      draw_list.emit_vtx(rx + rw - t, ry + rh - t, 0, 0, col);
      draw_list.emit_quad(v, v + 1, v + 2, v + 3);
    };

    if (cfg.outline.value) {
      draw_full_rect(x - 1, y - 1, w + 2, h + 2, xdraw::color(0, 0, 0, 180),
                     1.0f);
      draw_full_rect(x, y, w, h, xdraw::color(0, 0, 0, 200), 2.0f);
    }

    draw_full_rect(x, y, w, h, color, 1.0f);
  } else {
    const auto corner =
        std::min(cfg.corner_length.value, std::min(w, h) * 0.4f);

    auto draw_cornered_rect = [&](float rx, float ry, float rw, float rh,
                                  const xdraw::color& col, float corner_len,
                                  float thickness) {
      const auto max_corner = std::min(rw, rh) * 0.5f;
      const auto cl = std::min(corner_len, max_corner);
      const auto t = std::clamp(thickness, 0.0f, std::min(rw, rh) * 0.5f);

      if (t <= 0.0f) {
        return;
      }

      draw_list.ensure_cmd(nullptr);

      auto v = draw_list.emit_vtx(rx, ry, 0, 0, col);
      draw_list.emit_vtx(rx + cl, ry, 0, 0, col);
      draw_list.emit_vtx(rx + cl, ry + t, 0, 0, col);
      draw_list.emit_vtx(rx, ry + t, 0, 0, col);
      draw_list.emit_quad(v, v + 1, v + 2, v + 3);

      v = draw_list.emit_vtx(rx, ry + t, 0, 0, col);
      draw_list.emit_vtx(rx + t, ry + t, 0, 0, col);
      draw_list.emit_vtx(rx + t, ry + cl, 0, 0, col);
      draw_list.emit_vtx(rx, ry + cl, 0, 0, col);
      draw_list.emit_quad(v, v + 1, v + 2, v + 3);

      v = draw_list.emit_vtx(rx + rw - cl, ry, 0, 0, col);
      draw_list.emit_vtx(rx + rw, ry, 0, 0, col);
      draw_list.emit_vtx(rx + rw, ry + t, 0, 0, col);
      draw_list.emit_vtx(rx + rw - cl, ry + t, 0, 0, col);
      draw_list.emit_quad(v, v + 1, v + 2, v + 3);

      v = draw_list.emit_vtx(rx + rw - t, ry + t, 0, 0, col);
      draw_list.emit_vtx(rx + rw, ry + t, 0, 0, col);
      draw_list.emit_vtx(rx + rw, ry + cl, 0, 0, col);
      draw_list.emit_vtx(rx + rw - t, ry + cl, 0, 0, col);
      draw_list.emit_quad(v, v + 1, v + 2, v + 3);

      v = draw_list.emit_vtx(rx + rw - t, ry + rh - cl, 0, 0, col);
      draw_list.emit_vtx(rx + rw, ry + rh - cl, 0, 0, col);
      draw_list.emit_vtx(rx + rw, ry + rh - t, 0, 0, col);
      draw_list.emit_vtx(rx + rw - t, ry + rh - t, 0, 0, col);
      draw_list.emit_quad(v, v + 1, v + 2, v + 3);

      v = draw_list.emit_vtx(rx + rw - cl, ry + rh - t, 0, 0, col);
      draw_list.emit_vtx(rx + rw, ry + rh - t, 0, 0, col);
      draw_list.emit_vtx(rx + rw, ry + rh, 0, 0, col);
      draw_list.emit_vtx(rx + rw - cl, ry + rh, 0, 0, col);
      draw_list.emit_quad(v, v + 1, v + 2, v + 3);

      v = draw_list.emit_vtx(rx, ry + rh - cl, 0, 0, col);
      draw_list.emit_vtx(rx + t, ry + rh - cl, 0, 0, col);
      draw_list.emit_vtx(rx + t, ry + rh - t, 0, 0, col);
      draw_list.emit_vtx(rx, ry + rh - t, 0, 0, col);
      draw_list.emit_quad(v, v + 1, v + 2, v + 3);

      v = draw_list.emit_vtx(rx, ry + rh - t, 0, 0, col);
      draw_list.emit_vtx(rx + cl, ry + rh - t, 0, 0, col);
      draw_list.emit_vtx(rx + cl, ry + rh, 0, 0, col);
      draw_list.emit_vtx(rx, ry + rh, 0, 0, col);
      draw_list.emit_quad(v, v + 1, v + 2, v + 3);
    };

    if (cfg.outline) {
      draw_cornered_rect(x - 1, y - 1, w + 2, h + 2, xdraw::color(0, 0, 0, 180),
                         corner + 1, 1.0f);
      draw_cornered_rect(x, y, w, h, xdraw::color(0, 0, 0, 200), corner, 2.0f);
    }

    draw_cornered_rect(x, y, w, h, color, corner, 1.0f);
  }
}

void Overlay::AddSkeleton(xdraw::draw_list& draw_list, const Info& info,
                          const settings::Esp::Player::Overlay::Skeleton& cfg,
                          bool visible, const systems::Local::Snapshot& local) {
  const auto& color = visible ? cfg.visible_color : cfg.occluded_color;

  constexpr auto kNone{~0u};
  constexpr std::array kChains{
      std::array{cstypes::bone_ids::kHead, cstypes::bone_ids::kNeck,
                 cstypes::bone_ids::kSpine4, cstypes::bone_ids::kSpine3,
                 cstypes::bone_ids::kSpine2, cstypes::bone_ids::kSpine1,
                 cstypes::bone_ids::kPelvis},
      std::array{cstypes::bone_ids::kLeftHand, cstypes::bone_ids::kLeftElbow,
                 cstypes::bone_ids::kLeftShoulder,
                 cstypes::bone_ids::kLeftClavicle, cstypes::bone_ids::kSpine4,
                 kNone, kNone},
      std::array{cstypes::bone_ids::kRightHand, cstypes::bone_ids::kRightElbow,
                 cstypes::bone_ids::kRightShoulder,
                 cstypes::bone_ids::kRightClavicle, cstypes::bone_ids::kSpine4,
                 kNone, kNone},
      std::array{cstypes::bone_ids::kLeftFoot, cstypes::bone_ids::kLeftKnee,
                 cstypes::bone_ids::kLeftHip, cstypes::bone_ids::kPelvis, kNone,
                 kNone, kNone},
      std::array{cstypes::bone_ids::kRightFoot, cstypes::bone_ids::kRightKnee,
                 cstypes::bone_ids::kRightHip, cstypes::bone_ids::kPelvis,
                 kNone, kNone, kNone},
  };

  std::array<math::Vector3, 27> positions{};
  auto valid_bones{false};

  if (cfg.type == settings::Esp::Player::Overlay::Skeleton::Mode::kBacktrack &&
      local.is_alive) {
    const auto record =
        combat::g_shared.Lc().GetOldestWasValidVisual(info.pawn);
    if (record) {
      if (record->origin.Distance(info.origin) >= 0.25f) {
        for (auto i = 0; i < 27; ++i) {
          positions[i] = record->bones[i].position;
        }

        valid_bones = true;
      }
    }
  }

  if (!valid_bones) {
    if (cfg.type ==
        settings::Esp::Player::Overlay::Skeleton::Mode::kBacktrack) {
      return;
    }

    for (auto i = 0ull; i < info.bones.size() && i < 27; ++i) {
      positions[i] = info.bones[i].position;
    }
  }

  constexpr auto kStep{0.08f};

  const auto flush_segment = [&](std::vector<math::Vector2>& screen_points) {
    if (screen_points.size() < 2) {
      screen_points.clear();
      return;
    }

    screen_points.insert(screen_points.begin(), screen_points.front());
    screen_points.push_back(screen_points.back());

    std::vector<float> spline_points;
    spline_points.reserve(
        (screen_points.size() * static_cast<std::size_t>(1.f / kStep) + 1) * 2);

    for (auto i = 0ull; i + 3 < screen_points.size(); ++i) {
      const auto& p0 = screen_points[i];
      const auto& p1 = screen_points[i + 1];
      const auto& p2 = screen_points[i + 2];
      const auto& p3 = screen_points[i + 3];

      for (float t = 0.f; t <= 1.f; t += kStep) {
        const auto t2 = t * t;
        const auto t3 = t2 * t;

        spline_points.push_back(
            0.5f * ((2.f * p1.x) + (-p0.x + p2.x) * t +
                    (2.f * p0.x - 5.f * p1.x + 4.f * p2.x - p3.x) * t2 +
                    (-p0.x + 3.f * p1.x - 3.f * p2.x + p3.x) * t3));
        spline_points.push_back(
            0.5f * ((2.f * p1.y) + (-p0.y + p2.y) * t +
                    (2.f * p0.y - 5.f * p1.y + 4.f * p2.y - p3.y) * t2 +
                    (-p0.y + 3.f * p1.y - 3.f * p2.y + p3.y) * t3));
      }
    }

    if (spline_points.size() >= 4) {
      draw_list.polyline(spline_points, color, false, cfg.thickness);
    }

    screen_points.clear();
  };

  for (const auto& chain : kChains) {
    std::vector<math::Vector2> screen_points;
    screen_points.reserve(chain.size());

    for (const auto& b : chain) {
      if (b == kNone) {
        break;
      }

      const auto idx = static_cast<std::size_t>(b);
      if (idx >= 28 || positions[idx].LengthSqr() < 1.0f) {
        flush_segment(screen_points);
        continue;
      }

      const auto projected = systems::g_view.Project(positions[idx]);
      if (!systems::g_view.ProjectionValid(projected)) {
        flush_segment(screen_points);
        continue;
      }

      math::Vector2 pt{projected.x, projected.y};

      if (!screen_points.empty()) {
        const auto& prev = screen_points.back();
        const auto dx = pt.x - prev.x;
        const auto dy = pt.y - prev.y;

        if (dx * dx + dy * dy > 250000.0f) {
          flush_segment(screen_points);
        }
      }

      screen_points.push_back(pt);
    }

    flush_segment(screen_points);
  }
}

void Overlay::AddHealthBar(xdraw::draw_list& draw_list,
                           const systems::Bounds::Data& bounds,
                           const Info& info,
                           const settings::Esp::Player::Overlay::HealthBar& cfg,
                           DrawOffsets& offsets) {
  auto& anim = this->m_animations_[info.controller];

  constexpr auto kBarSize = 3.5f, kPadding = 4.0f;
  const auto clamped_health = std::clamp(info.health, 0, 100);
  const auto target_fraction = clamped_health / 100.0f;

  if (!anim.initialized || (target_fraction - anim.health.Value() > 0.5f)) {
    anim.health.Snap(target_fraction);
    anim.initialized = true;
  } else {
    anim.health.SetTarget(target_fraction);
    anim.health.Update();
  }

  const auto fraction = anim.health.Value();
  const auto outline_size = cfg.outline_setting.value ? 1.0f : 0.0f;
  const auto vertical =
      cfg.position ==
      settings::Esp::Player::Overlay::HealthBar::PositionType::kLeft;

  const auto bar_w = vertical ? kBarSize : std::floorf(bounds.Width());
  const auto bar_h = vertical ? std::floorf(bounds.Height()) : kBarSize;
  const auto filled = (clamped_health >= 100)
                          ? (vertical ? bar_h : bar_w)
                          : std::floorf((vertical ? bar_h : bar_w) * fraction);

  const auto x = [&]() {
    if (cfg.position ==
        settings::Esp::Player::Overlay::HealthBar::PositionType::kLeft) {
      return std::floorf(bounds.min.x - kBarSize - kPadding - offsets.left -
                         outline_size);
    }

    return std::floorf(bounds.min.x);
  }();

  const auto y = [&]() {
    switch (cfg.position) {
      case settings::Esp::Player::Overlay::HealthBar::PositionType::kLeft:
        return std::floorf(bounds.min.y);
      case settings::Esp::Player::Overlay::HealthBar::PositionType::kTop:
        return std::floorf(bounds.min.y - kBarSize - kPadding - offsets.top -
                           outline_size);
      case settings::Esp::Player::Overlay::HealthBar::PositionType::kBottom:
        return std::floorf(bounds.max.y + kPadding + offsets.bottom +
                           outline_size);
    }
    return 0.0f;
  }();

  switch (cfg.position) {
    case settings::Esp::Player::Overlay::HealthBar::PositionType::kLeft:
      offsets.left += kBarSize + kPadding + (outline_size * 2.0f);
      break;
    case settings::Esp::Player::Overlay::HealthBar::PositionType::kTop:
      offsets.top += kBarSize + kPadding + (outline_size * 2.0f);
      break;
    case settings::Esp::Player::Overlay::HealthBar::PositionType::kBottom:
      offsets.bottom += kBarSize + kPadding + (outline_size * 2.0f);
      break;
  }

  if (cfg.glow) {
    auto& glow = xdraw::get_glow();
    const auto gc = cfg.glow_color.value;
    const auto glow_a =
        static_cast<std::uint8_t>(static_cast<float>(gc.a) * cfg.glow_strength);
    const auto glow_col = xdraw::color{gc.r, gc.g, gc.b, glow_a};

    for (auto t = 0; t < 3; ++t) {
      const auto expand = static_cast<float>(t) * 0.75f;
      glow.rect_filled(x - expand, y - expand, bar_w + expand * 2.0f,
                       bar_h + expand * 2.0f, glow_col);
    }
  }

  if (cfg.outline_setting.value) {
    draw_list.rect_filled(x - 1, y - 1, bar_w + 2, bar_h + 2,
                          cfg.outline_color);
  }

  draw_list.rect_filled(x, y, bar_w, bar_h, cfg.background_color);

  if (filled > 0) {
    if (cfg.gradient) {
      if (vertical) {
        draw_list.rect_filled_gradient(x, y + bar_h - filled, bar_w, filled,
                                       cfg.full_color, cfg.full_color,
                                       cfg.low_color, cfg.low_color);
      } else {
        draw_list.rect_filled_gradient(x, y, filled, bar_h, cfg.low_color,
                                       cfg.full_color, cfg.full_color,
                                       cfg.low_color);
      }
    } else {
      if (vertical) {
        draw_list.rect_filled(x, y + bar_h - filled, bar_w, filled,
                              cfg.full_color);
      } else {
        draw_list.rect_filled(x, y, filled, bar_h, cfg.full_color);
      }
    }
  }

  if (cfg.show_value && clamped_health < 100) {
    xdraw::push_font(
        rendering::g_fonts.smallest_pixel7[rendering::Fonts::Size::kNormal]);

    const auto text = std::to_string(clamped_health);
    const auto [text_w, text_h] = xdraw::measure_text(text);
    const auto text_x = std::floorf(x + (bar_w * 0.5f) - (text_w * 0.5f));
    const auto text_y = vertical
                            ? std::floorf(y + bar_h - filled - text_h - 2.0f)
                            : std::floorf(y - text_h - 2.0f);

    draw_list.text(text_x, text_y, text, cfg.text_color,
                   xdraw::text_style::outlined);
    xdraw::pop_font();
  }
}

void Overlay::AddAmmoBar(xdraw::draw_list& draw_list,
                         const systems::Bounds::Data& bounds, const Info& info,
                         const settings::Esp::Player::Overlay::AmmoBar& cfg,
                         DrawOffsets& offsets) {
  if (info.weapon.max_ammo <= 0) {
    return;
  }

  auto& anim = this->m_animations_[info.controller];

  constexpr auto kBarSize = 3.5f, kPadding = 4.0f;
  const auto clamped_ammo =
      std::clamp(info.weapon.ammo, 0, info.weapon.max_ammo);
  const auto target_fraction =
      static_cast<float>(clamped_ammo) / info.weapon.max_ammo;

  if (!anim.initialized || (target_fraction - anim.ammo.Value() > 0.5f)) {
    anim.ammo.Snap(target_fraction);
    anim.initialized = true;
  } else {
    anim.ammo.SetTarget(target_fraction);
    anim.ammo.Update();
  }

  const auto fraction = anim.ammo.Value();
  const auto outline_size = cfg.outline_setting.value ? 1.0f : 0.0f;
  const auto vertical =
      cfg.position ==
      settings::Esp::Player::Overlay::AmmoBar::PositionType::kLeft;

  const auto bar_w = vertical ? kBarSize : std::floorf(bounds.Width());
  const auto bar_h = vertical ? std::floorf(bounds.Height()) : kBarSize;
  const auto filled = (clamped_ammo == info.weapon.max_ammo)
                          ? (vertical ? bar_h : bar_w)
                          : std::floorf((vertical ? bar_h : bar_w) * fraction);

  const auto x = [&]() {
    if (cfg.position ==
        settings::Esp::Player::Overlay::AmmoBar::PositionType::kLeft) {
      return std::floorf(bounds.min.x - kBarSize - kPadding - offsets.left -
                         outline_size);
    }

    return std::floorf(bounds.min.x);
  }();

  const auto y = [&]() {
    switch (cfg.position) {
      case settings::Esp::Player::Overlay::AmmoBar::PositionType::kLeft:
        return std::floorf(bounds.min.y);
      case settings::Esp::Player::Overlay::AmmoBar::PositionType::kTop:
        return std::floorf(bounds.min.y - kBarSize - kPadding - offsets.top -
                           outline_size);
      case settings::Esp::Player::Overlay::AmmoBar::PositionType::kBottom:
        return std::floorf(bounds.max.y + kPadding + offsets.bottom +
                           outline_size);
    }
    return 0.0f;
  }();

  switch (cfg.position) {
    case settings::Esp::Player::Overlay::AmmoBar::PositionType::kLeft:
      offsets.left += kBarSize + kPadding + (outline_size * 2.0f);
      break;
    case settings::Esp::Player::Overlay::AmmoBar::PositionType::kTop:
      offsets.top += kBarSize + kPadding + (outline_size * 2.0f);
      break;
    case settings::Esp::Player::Overlay::AmmoBar::PositionType::kBottom:
      offsets.bottom += kBarSize + kPadding + (outline_size * 2.0f);
      break;
  }

  if (cfg.glow) {
    auto& glow = xdraw::get_glow();
    const auto gc = cfg.glow_color.value;
    const auto glow_a =
        static_cast<std::uint8_t>(static_cast<float>(gc.a) * cfg.glow_strength);
    const auto glow_col = xdraw::color{gc.r, gc.g, gc.b, glow_a};

    for (auto t = 0; t < 3; ++t) {
      const auto expand = static_cast<float>(t) * 0.75f;
      glow.rect_filled(x - expand, y - expand, bar_w + expand * 2.0f,
                       bar_h + expand * 2.0f, glow_col);
    }
  }

  if (cfg.outline_setting.value) {
    draw_list.rect_filled(x - 1, y - 1, bar_w + 2, bar_h + 2,
                          cfg.outline_color);
  }

  draw_list.rect_filled(x, y, bar_w, bar_h, cfg.background_color);

  if (filled > 0) {
    if (cfg.gradient) {
      if (vertical) {
        draw_list.rect_filled_gradient(x, y + bar_h - filled, bar_w, filled,
                                       cfg.full_color, cfg.full_color,
                                       cfg.low_color, cfg.low_color);
      } else {
        draw_list.rect_filled_gradient(x, y, filled, bar_h, cfg.low_color,
                                       cfg.full_color, cfg.full_color,
                                       cfg.low_color);
      }
    } else {
      if (vertical) {
        draw_list.rect_filled(x, y + bar_h - filled, bar_w, filled,
                              cfg.full_color);
      } else {
        draw_list.rect_filled(x, y, filled, bar_h, cfg.full_color);
      }
    }
  }

  if (cfg.show_value) {
    xdraw::push_font(
        rendering::g_fonts.smallest_pixel7[rendering::Fonts::Size::kNormal]);

    const auto text = std::format("{}/{}", clamped_ammo, info.weapon.max_ammo);
    const auto [text_w, text_h] = xdraw::measure_text(text);
    const auto text_x = std::floorf(x + (bar_w * 0.5f) - (text_w * 0.5f));
    const auto text_y = vertical
                            ? std::floorf(y + bar_h - filled - text_h - 2.0f)
                            : std::floorf(y + bar_h + 2.0f);

    draw_list.text(text_x, text_y, text, cfg.text_color,
                   xdraw::text_style::outlined);
    xdraw::pop_font();
  }
}

void Overlay::AddName(xdraw::draw_list& draw_list,
                      const systems::Bounds::Data& bounds, const Info& info,
                      const settings::Esp::Player::Overlay::Name& cfg,
                      DrawOffsets& offsets) {
  // xdraw::push_font( rendering::g_fonts.inter_medium[
  // rendering::fonts::size::normal ] );

  const auto [text_w, text_h] = xdraw::measure_text(info.name);
  const auto text_x =
      std::floorf(bounds.min.x + (bounds.Width() * 0.5f) - (text_w * 0.5f));
  const auto text_y = std::floorf(bounds.min.y - text_h - 2.0f - offsets.top);

  draw_list.text(text_x, text_y, info.name, cfg.color);
  offsets.top += text_h + 2.0f;

  // xdraw::pop_font( );
}

void Overlay::AddWeapon(xdraw::draw_list& draw_list,
                        const systems::Bounds::Data& bounds, const Info& info,
                        const settings::Esp::Player::Overlay::Weapon& cfg,
                        DrawOffsets& offsets) {
  const auto show_icon =
      cfg.display ==
          settings::Esp::Player::Overlay::Weapon::DisplayType::kIcon ||
      cfg.display ==
          settings::Esp::Player::Overlay::Weapon::DisplayType::kTextAndIcon;
  const auto show_text =
      cfg.display ==
          settings::Esp::Player::Overlay::Weapon::DisplayType::kText ||
      cfg.display ==
          settings::Esp::Player::Overlay::Weapon::DisplayType::kTextAndIcon;
  auto total_height{0.0f};

  if (show_icon) {
    const auto icon_name =
        (info.weapon.name == "knife_ct" || info.weapon.name == "knife_t")
            ? std::string{"knife"}
            : info.weapon.name;
    const auto ico = systems::g_icons.Get(icon_name, 0.35f);

    if (ico && ico->texture) {
      const auto iw = static_cast<float>(ico->width);
      const auto ih = static_cast<float>(ico->height);
      const auto ix =
          std::floorf(bounds.min.x + (bounds.Width() * 0.5f) - (iw * 0.5f));
      const auto iy =
          std::floorf(bounds.max.y + 2.0f + offsets.bottom + total_height);
      constexpr auto kOutline = xdraw::color{0, 0, 0, 255};

      draw_list.image(ix - 1.0f, iy, iw, ih, ico->texture.Get(), kOutline);
      draw_list.image(ix + 1.0f, iy, iw, ih, ico->texture.Get(), kOutline);
      draw_list.image(ix, iy - 1.0f, iw, ih, ico->texture.Get(), kOutline);
      draw_list.image(ix, iy + 1.0f, iw, ih, ico->texture.Get(), kOutline);
      draw_list.image(ix, iy, iw, ih, ico->texture.Get(), cfg.icon_color);

      total_height += ih + 2.0f;
    }
  }

  if (show_text) {
    xdraw::push_font(
        rendering::g_fonts.smallest_pixel7[rendering::Fonts::Size::kNormal]);

    const auto [text_w, text_h] = xdraw::measure_text(info.weapon.name);
    const auto text_x =
        std::floorf(bounds.min.x + (bounds.Width() * 0.5f) - (text_w * 0.5f));
    const auto text_y =
        std::floorf(bounds.max.y + 2.0f + offsets.bottom + total_height);

    draw_list.text(text_x, text_y, info.weapon.name, cfg.text_color,
                   xdraw::text_style::outlined);
    xdraw::pop_font();

    total_height += text_h + 2.0f;
  }

  offsets.bottom += total_height;
}

void Overlay::AddFlags(xdraw::draw_list& draw_list,
                       const systems::Bounds::Data& bounds, const Info& info,
                       const settings::Esp::Player::Overlay::InfoFlags& cfg,
                       DrawOffsets& offsets) {
  xdraw::push_font(
      rendering::g_fonts.smallest_pixel7[rendering::Fonts::Size::kNormal]);

  const auto x = std::floorf(bounds.max.x + 4.0f);
  auto y = std::floorf(bounds.min.y);

  const auto draw_flag = [&](const std::string& text,
                             const xdraw::color& color) {
    draw_list.text(x, y, text, color, xdraw::text_style::outlined);
    const auto [text_w, text_h] = xdraw::measure_text(text);
    y += text_h;
  };

  if (cfg.Has(settings::Esp::Player::Overlay::InfoFlags::Flag::kMoney)) {
    draw_flag(std::format("${}", info.money), cfg.money_color);
  }

  if (cfg.Has(settings::Esp::Player::Overlay::InfoFlags::Flag::kArmor) &&
      info.armor > 0) {
    draw_flag(info.has_helmet ? "hk" : "k", cfg.armor_color);
  }

  if (cfg.Has(settings::Esp::Player::Overlay::InfoFlags::Flag::kIt) &&
      info.has_defuser) {
    draw_flag("kit", cfg.kit_color);
  }

  if (cfg.Has(settings::Esp::Player::Overlay::InfoFlags::Flag::kScoped) &&
      info.is_scoped) {
    draw_flag("zoom", cfg.scoped_color);
  }

  if (cfg.Has(settings::Esp::Player::Overlay::InfoFlags::Flag::kDefusing) &&
      info.is_defusing) {
    draw_flag("defusing", cfg.defusing_color);
  }

  if (cfg.Has(settings::Esp::Player::Overlay::InfoFlags::Flag::kFlashed) &&
      info.is_flashed) {
    draw_flag("flashed", cfg.flashed_color);
  }

  if (cfg.Has(settings::Esp::Player::Overlay::InfoFlags::Flag::kPing)) {
    const auto ping_color = [&]() {
      if (info.ping <= 20) {
        return xdraw::color(98, 217, 109, 255);
      }

      if (info.ping <= 50) {
        return xdraw::color(230, 206, 137, 255);
      }

      if (info.ping <= 70) {
        return xdraw::color(230, 156, 110, 255);
      }

      return xdraw::color(222, 59, 59, 255);
    }();

    draw_flag(std::format("{}ms", info.ping), ping_color);
  }

  if (cfg.Has(settings::Esp::Player::Overlay::InfoFlags::Flag::kDistance)) {
    draw_flag(std::format("{:.0f}m", info.distance), cfg.distance_color);
  }

  xdraw::pop_font();
}

void Overlay::AddOofArrow(xdraw::draw_list& draw_list, const Info& info,
                          const settings::Esp::Player::Overlay::OofArrow& cfg) {
  const auto [screen_w, screen_h] = xdraw::viewport_size();
  const auto sw = static_cast<float>(screen_w);
  const auto sh = static_cast<float>(screen_h);
  const auto center_x = sw * 0.5f;
  const auto center_y = sh * 0.5f;
  const auto head = info.bones[cstypes::bone_ids::kHead].position;

  const auto proj = systems::g_view.ProjectFull(head);

  if (proj.on_screen && proj.w > 0.0f) {
    return;
  }

  if (!systems::g_frame_data.Valid()) {
    return;
  }

  const auto to_target = info.origin - systems::g_frame_data.Origin();
  if (!std::isfinite(to_target.x) || !std::isfinite(to_target.y) ||
      to_target.Length2d() < 1.0f) {
    return;
  }

  // Match the live input yaw and player origins used by the reference OOF
  // implementation. The view system's cached origin/angles describe a different
  // render structure.
  const auto target_yaw = std::atan2f(to_target.y, to_target.x);
  const auto view_yaw =
      math::helpers::DegToRad(systems::g_input.GetViewAngles().y);
  const auto angle = view_yaw - target_yaw - std::numbers::pi_v<float> * 0.5f;
  const auto rx = cfg.radius_x.value;
  const auto ry = cfg.radius_y.value;

  const auto tip_x = center_x + std::cosf(angle) * rx;
  const auto tip_y = center_y + std::sinf(angle) * ry;

  const auto& color = info.is_visible ? cfg.visible_color : cfg.occluded_color;
  const auto width = cfg.width.value;
  const auto height = cfg.height.value;

  const auto fx = std::cosf(angle);
  const auto fy = std::sinf(angle);
  const auto px = -fy;
  const auto py = fx;

  const auto base_cx = tip_x - fx * height;
  const auto base_cy = tip_y - fy * height;

  const auto half_w = width * 0.5f;

  const auto bl_x = base_cx - px * half_w;
  const auto bl_y = base_cy - py * half_w;
  const auto br_x = base_cx + px * half_w;
  const auto br_y = base_cy + py * half_w;

  draw_list.triangle_filled(tip_x, tip_y, bl_x, bl_y, br_x, br_y, color);

  if (cfg.glow) {
    auto& glow = xdraw::get_glow();
    const auto glow_a = static_cast<std::uint8_t>(
        static_cast<float>(color.value.a) * cfg.glow_strength);
    const auto glow_col =
        xdraw::color{color.value.r, color.value.g, color.value.b, glow_a};

    glow.triangle_filled(tip_x, tip_y, bl_x, bl_y, br_x, br_y, glow_col);
  }
}

Overlay::Info Overlay::GetInfo(const systems::Entities::Cached& player,
                               const systems::Local::Snapshot& local) {
  Info info{};
  info.controller = player.ptr;

  if (!info.controller) {
    return info;
  }

  if (!memory::Read<bool>(info.controller + SCHEMA("CCSPlayerController",
                                                   "m_bPawnIsAlive"_hash))) {
    return info;
  }

  const auto pawn_handle = memory::Read<std::uint32_t>(
      info.controller + SCHEMA("CBasePlayerController", "m_hPawn"_hash));
  if (!pawn_handle) {
    return info;
  }

  info.pawn = systems::g_entities.Lookup(pawn_handle);
  if (!info.pawn || info.pawn == local.ViewPawn()) {
    return info;
  }

  info.health =
      memory::Read<int>(info.pawn + SCHEMA("C_BaseEntity", "m_iHealth"_hash));
  if (info.health <= 0) {
    return info;
  }

  info.team =
      memory::Read<int>(info.pawn + SCHEMA("C_BaseEntity", "m_iTeamNum"_hash));
  info.is_other_team = local.IsThisOtherTeam(info.team);

  const auto game_scene_node = memory::Read<std::uintptr_t>(
      info.pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
  if (!game_scene_node) {
    return info;
  }

  // m_bDormant is ambiguous across the current schema scopes and can
  // resolve to unrelated data. Keep the last known transform, as chams do.

  const auto name_ptr = memory::Read<std::uintptr_t>(
      info.controller +
      SCHEMA("CCSPlayerController", "m_sSanitizedPlayerName"_hash));
  if (name_ptr) {
    info.name = memory::ReadString(name_ptr, 128);
    std::ranges::transform(info.name, info.name.begin(),
                           [](unsigned char c) { return std::tolower(c); });
  }

  const auto money_services = memory::Read<std::uintptr_t>(
      info.controller +
      SCHEMA("CCSPlayerController", "m_pInGameMoneyServices"_hash));
  if (money_services) {
    info.money = memory::Read<int>(
        money_services +
        SCHEMA("CCSPlayerController_InGameMoneyServices", "m_iAccount"_hash));
  }

  const auto item_services = memory::Read<std::uintptr_t>(
      info.pawn + SCHEMA("C_BasePlayerPawn", "m_pItemServices"_hash));
  if (item_services) {
    info.has_helmet = memory::Read<bool>(
        item_services + SCHEMA("CCSPlayer_ItemServices", "m_bHasHelmet"_hash));
    info.has_defuser = memory::Read<bool>(
        item_services + SCHEMA("CCSPlayer_ItemServices", "m_bHasDefuser"_hash));
  }

  info.origin = memory::Read<math::Vector3>(
      game_scene_node + SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_hash));
  info.distance = systems::g_view.Origin().Distance(info.origin) * 0.01905f;
  info.ping = memory::Read<int>(info.controller +
                                SCHEMA("CCSPlayerController", "m_iPing"_hash));
  info.armor = memory::Read<int>(info.pawn +
                                 SCHEMA("C_CSPlayerPawn", "m_ArmorValue"_hash));
  info.is_scoped = memory::Read<bool>(
      info.pawn + SCHEMA("C_CSPlayerPawn", "m_bIsScoped"_hash));
  info.is_defusing = memory::Read<bool>(
      info.pawn + SCHEMA("C_CSPlayerPawn", "m_bIsDefusing"_hash));
  info.is_flashed =
      memory::Read<float>(info.pawn + SCHEMA("C_CSPlayerPawnBase",
                                             "m_flFlashBangTime"_hash)) > 0.0f;
  info.bones = systems::g_bones.GetSkeleton(info.pawn);
  info.is_visible = systems::g_tracing.IsVisible(
      systems::g_view.Origin(), info.bones[cstypes::bone_ids::kHead].position,
      info.pawn, local.ViewPawn());

  const auto weapon_services = memory::Read<std::uintptr_t>(
      info.pawn + SCHEMA("C_BasePlayerPawn", "m_pWeaponServices"_hash));
  if (weapon_services) {
    const auto weapon_handle = memory::Read<std::uint32_t>(
        weapon_services +
        SCHEMA("CPlayer_WeaponServices", "m_hActiveWeapon"_hash));
    if (weapon_handle) {
      info.weapon.ptr = systems::g_entities.Lookup(weapon_handle);
      if (info.weapon.ptr) {
        info.weapon.vdata = memory::Read<std::uintptr_t>(
            info.weapon.ptr + SCHEMA("C_BaseEntity", "m_nSubclassID"_hash) +
            0x8);
        if (info.weapon.vdata) {
          info.weapon.ammo = memory::Read<int>(
              info.weapon.ptr + SCHEMA("C_BasePlayerWeapon", "m_iClip1"_hash));
          info.weapon.max_ammo = memory::Read<int>(
              info.weapon.vdata +
              SCHEMA("CBasePlayerWeaponVData", "m_iMaxClip1"_hash));

          const auto weapon_name_ptr = memory::Read<const char*>(
              info.weapon.vdata +
              SCHEMA("CCSWeaponBaseVData", "m_szName"_hash));
          if (weapon_name_ptr) {
            info.weapon.name = memory::ReadString(
                reinterpret_cast<std::uintptr_t>(weapon_name_ptr), 64);
            if (info.weapon.name.starts_with("weapon_")) {
              info.weapon.name.erase(0, 7);
            }
          }
        }
      }
    }
  }

  return info;
}

}  // namespace features::esp::player
