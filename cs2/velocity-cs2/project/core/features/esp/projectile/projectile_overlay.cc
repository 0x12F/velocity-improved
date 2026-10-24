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

namespace features::esp::projectile {

namespace detail {

static constexpr const char* kFireSvg{
    R"(<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 24 24"><path fill="#ffffff" d="M12 23a7.5 7.5 0 0 1-5.138-12.963C8.204 8.774 11.5 6.5 11 1.5c6 4 9 8 3 14c1 0 2.5 0 5-2.47c.27.773.5 1.604.5 2.47A7.5 7.5 0 0 1 12 23"/></svg>)"};

struct FireIcon {
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texture{};
  int width{};
  int height{};
};

[[nodiscard]] static const FireIcon* GetFireIcon() {
  static FireIcon ico{};
  static auto loaded{false};

  if (!loaded) {
    ico.texture = xdraw::load_svg(kFireSvg, 0.75f, &ico.width, &ico.height);
    loaded = true;
  }

  return ico.texture ? &ico : nullptr;
}

constexpr auto kEdgePadding{52.0f};
constexpr auto kIconRadius{18.0f};
constexpr auto kArcRadius{22.0f};
constexpr auto kArcThickness{2.75f};
constexpr auto kSegments{32};
constexpr auto kArrowLength{11.0f};
constexpr auto kArrowSpread{0.45f};
constexpr auto kArrowArcSegments{10};
constexpr auto kArrowGap{3.5f};
constexpr auto kArrowBaseR{kArcRadius + kArrowGap};
constexpr auto kArrowTipR{kArrowBaseR + kArrowLength};

constexpr auto kFadeSpeed{3.0f};
constexpr auto kAnchorWorldOffsetZ{80.0f};

}  // namespace detail

void Overlay::OnRender(xdraw::draw_list& draw_list,
                       xdraw::draw_list& middle_draw_list) {
  const auto& overlay_cfg = settings::g_esp.m_projectile.m_overlay;

  for (const auto& projectile :
       systems::g_entities.GetByType(systems::Entities::Type::kProjectile)) {
    if (projectile.schema_hash == "C_Inferno"_hash) {
      if (overlay_cfg.IsActive(5)) {
        this->AddInferno(draw_list, middle_draw_list, projectile,
                         overlay_cfg.m_infernos);
      }
      continue;
    }

    const auto info = this->GetInfo(projectile);
    if (!info.Valid() || info.detonated) {
      continue;
    }

    const auto& cfg = overlay_cfg.GetGroup(info.group_id);
    if (!overlay_cfg.IsActive(info.group_id) ||
        info.distance > cfg.max_distance) {
      continue;
    }

    const auto screen = systems::g_view.Project(info.origin);
    if (!systems::g_view.ProjectionValid(screen)) {
      continue;
    }

    this->AddLabel(middle_draw_list, screen, info, cfg);
  }

  this->AddLandingIndicators(middle_draw_list);
}

void Overlay::AddLandingIndicators(xdraw::draw_list& draw_list) {
  const auto now = std::chrono::steady_clock::now();
  const auto delta_time = xdraw::delta_time();

  const auto [screen_w, screen_h] = xdraw::viewport_size();

  const auto sw = static_cast<float>(screen_w);
  const auto sh = static_cast<float>(screen_h);

  const auto center_x = sw * 0.5f;
  const auto center_y = sh * 0.5f;

  std::unordered_set<std::uintptr_t> seen;

  const auto& ind = settings::g_esp.m_projectile.m_overlay.m_indicator;

  for (const auto& gren : features::misc::g_projectile_trajectory.InFlight()) {
    if (!gren.traj.valid || gren.detonated) {
      continue;
    }

    const auto is_he = gren.weapon_hash == "weapon_hegrenade"_hash;
    const auto is_molotov = gren.weapon_hash == "weapon_molotov"_hash ||
                            gren.weapon_hash == "weapon_incgrenade"_hash;

    if (!is_he && !is_molotov) {
      continue;
    }

    const auto group_id = is_molotov ? 1u : 0u;
    const auto& cfg = ind.GetGroup(group_id);

    if (!cfg.enabled.value) {
      continue;
    }

    seen.insert(gren.entity);

    auto& state = this->m_indicator_states_[gren.entity];
    state.was_active = true;
    state.fade_alpha = 1.0f;

    const auto elapsed =
        std::chrono::duration<float>(now - gren.throw_time).count();
    if (elapsed >= gren.traj.duration) {
      continue;
    }

    const auto remaining = std::max(0.0f, gren.traj.duration - elapsed);
    const auto frac = std::clamp(remaining / gren.traj.duration, 0.0f, 1.0f);

    const auto proj = systems::g_view.ProjectFull(gren.traj.end_pos);
    const auto on_screen = proj.on_screen;

    if (on_screen) {
      this->AddIndicator(draw_list, proj.screen.x, proj.screen.y, 0.0f, false,
                         frac, state.fade_alpha, is_molotov, cfg);
      continue;
    }

    auto dx = proj.screen.x - center_x;
    auto dy = proj.screen.y - center_y;

    if (proj.w <= 0.0f) {
      dx = -dx;
      dy = -dy;
    }

    const auto len = std::sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) {
      continue;
    }

    dx /= len;
    dy /= len;

    auto t_min = std::numeric_limits<float>::max();

    if (std::fabsf(dx) > 0.001f) {
      const auto t_left = (detail::kEdgePadding - center_x) / dx;
      const auto t_right = (sw - detail::kEdgePadding - center_x) / dx;

      if (t_left > 0.0f) {
        t_min = std::fminf(t_min, t_left);
      }

      if (t_right > 0.0f) {
        t_min = std::fminf(t_min, t_right);
      }
    }

    if (std::fabsf(dy) > 0.001f) {
      const auto t_top = (detail::kEdgePadding - center_y) / dy;
      const auto t_bottom = (sh - detail::kEdgePadding - center_y) / dy;

      if (t_top > 0.0f) {
        t_min = std::fminf(t_min, t_top);
      }

      if (t_bottom > 0.0f) {
        t_min = std::fminf(t_min, t_bottom);
      }
    }

    const auto edge_x = std::clamp(center_x + dx * t_min, detail::kEdgePadding,
                                   sw - detail::kEdgePadding);
    const auto edge_y = std::clamp(center_y + dy * t_min, detail::kEdgePadding,
                                   sh - detail::kEdgePadding);

    const auto dir_angle = std::atan2f(dy, dx);
    const auto cx = edge_x - std::cosf(dir_angle) * detail::kArrowTipR;
    const auto cy = edge_y - std::sinf(dir_angle) * detail::kArrowTipR;

    this->AddIndicator(draw_list, cx, cy, dir_angle, true, frac,
                       state.fade_alpha, is_molotov, cfg);
  }

  for (auto it = this->m_indicator_states_.begin();
       it != this->m_indicator_states_.end();) {
    if (seen.contains(it->first)) {
      ++it;
      continue;
    }

    it->second.fade_alpha -= detail::kFadeSpeed * delta_time;

    if (it->second.fade_alpha <= 0.0f) {
      it = this->m_indicator_states_.erase(it);
    } else {
      ++it;
    }
  }
}

void Overlay::AddIndicator(
    xdraw::draw_list& draw_list, float cx, float cy, float dir_angle,
    bool has_arrow, float timer_frac, float alpha, bool is_fire,
    const settings::Esp::Projectile::Overlay::Indicator::Group& cfg) {
  if (alpha <= 0.0f) {
    return;
  }

  constexpr auto kTwoPi{std::numbers::pi_v<float> * 2.0f};
  constexpr auto kHalfPi{std::numbers::pi_v<float> * 0.5f};

  {
    const auto& bg = cfg.background_color.value;
    const auto ba = static_cast<std::uint8_t>(static_cast<float>(bg.a) * alpha);
    draw_list.circle_filled(cx, cy, detail::kIconRadius,
                            xdraw::color{bg.r, bg.g, bg.b, ba},
                            detail::kSegments);
  }

  const auto& icon_col = cfg.icon_color.value;
  const auto icon_a =
      static_cast<std::uint8_t>(static_cast<float>(icon_col.a) * alpha);
  const auto icon_tint =
      xdraw::color{icon_col.r, icon_col.g, icon_col.b, icon_a};

  if (is_fire) {
    const auto ico = detail::GetFireIcon();
    if (ico && ico->texture) {
      const auto iw = static_cast<float>(ico->width);
      const auto ih = static_cast<float>(ico->height);
      draw_list.image(std::floorf(cx - iw * 0.5f), std::floorf(cy - ih * 0.5f),
                      iw, ih, ico->texture.Get(), icon_tint);
    }
  } else {
    const auto ico = systems::g_icons.Get("hegrenade", 0.55f);
    if (ico && ico->texture) {
      const auto iw = static_cast<float>(ico->width);
      const auto ih = static_cast<float>(ico->height);
      draw_list.image(std::floorf(cx - iw * 0.5f), std::floorf(cy - ih * 0.5f),
                      iw, ih, ico->texture.Get(), icon_tint);
    }
  }

  const auto arc_end = timer_frac * kTwoPi;

  if (arc_end > 0.01f) {
    const auto& arc_col = cfg.arc_color.value;
    const auto aa =
        static_cast<std::uint8_t>(static_cast<float>(arc_col.a) * alpha);
    const auto arc_tint = xdraw::color{arc_col.r, arc_col.g, arc_col.b, aa};
    const auto arc_segs =
        std::max(3, static_cast<int>(detail::kSegments * timer_frac));

    std::vector<float> arc_pts;
    arc_pts.reserve((static_cast<std::size_t>(arc_segs) + 1) * 2);

    for (auto i = 0; i <= arc_segs; ++i) {
      const auto t = static_cast<float>(i) / static_cast<float>(arc_segs);
      const auto a = -kHalfPi + t * arc_end;
      arc_pts.push_back(cx + std::cosf(a) * detail::kArcRadius);
      arc_pts.push_back(cy + std::sinf(a) * detail::kArcRadius);
    }

    const auto span = std::span<const float>(arc_pts.data(), arc_pts.size());
    draw_list.polyline(span, arc_tint, false, detail::kArcThickness);

    if (cfg.glow.value) {
      auto& glow = xdraw::get_glow();
      const auto ga = static_cast<std::uint8_t>(static_cast<float>(arc_col.a) *
                                                cfg.glow_strength * alpha);
      glow.polyline(span, xdraw::color{arc_col.r, arc_col.g, arc_col.b, ga},
                    false, detail::kArcThickness + 2.0f);
    }
  }

  if (has_arrow) {
    const auto tip_x = cx + std::cosf(dir_angle) * detail::kArrowTipR;
    const auto tip_y = cy + std::sinf(dir_angle) * detail::kArrowTipR;

    const auto base_left_angle = dir_angle - detail::kArrowSpread;
    const auto base_right_angle = dir_angle + detail::kArrowSpread;

    std::vector<float> arrow_pts;
    arrow_pts.reserve((detail::kArrowArcSegments + 3) * 2);

    arrow_pts.push_back(tip_x);
    arrow_pts.push_back(tip_y);

    arrow_pts.push_back(cx + std::cosf(base_right_angle) * detail::kArrowBaseR);
    arrow_pts.push_back(cy + std::sinf(base_right_angle) * detail::kArrowBaseR);

    for (auto i = detail::kArrowArcSegments - 1; i >= 1; --i) {
      const auto t =
          static_cast<float>(i) / static_cast<float>(detail::kArrowArcSegments);
      const auto a = base_left_angle + t * (base_right_angle - base_left_angle);
      arrow_pts.push_back(cx + std::cosf(a) * detail::kArrowBaseR);
      arrow_pts.push_back(cy + std::sinf(a) * detail::kArrowBaseR);
    }

    arrow_pts.push_back(cx + std::cosf(base_left_angle) * detail::kArrowBaseR);
    arrow_pts.push_back(cy + std::sinf(base_left_angle) * detail::kArrowBaseR);

    const auto arrow_span =
        std::span<const float>(arrow_pts.data(), arrow_pts.size());

    const auto& arc_col = cfg.arc_color.value;
    const auto aa =
        static_cast<std::uint8_t>(static_cast<float>(arc_col.a) * alpha);
    draw_list.convex_filled(arrow_span,
                            xdraw::color{arc_col.r, arc_col.g, arc_col.b, aa});

    if (cfg.glow.value) {
      auto& glow = xdraw::get_glow();
      const auto ga = static_cast<std::uint8_t>(static_cast<float>(arc_col.a) *
                                                cfg.glow_strength * alpha);
      glow.convex_filled(arrow_span,
                         xdraw::color{arc_col.r, arc_col.g, arc_col.b, ga});
    }
  }
}

void Overlay::AddInferno(
    xdraw::draw_list& draw_list, xdraw::draw_list& middle_draw_list,
    const systems::Entities::Cached& entity,
    const settings::Esp::Projectile::Overlay::Infernos& cfg) {
  constexpr auto kNumDirections{15};
  constexpr auto kTwoPi{std::numbers::pi_v<float> * 2.0f};
  constexpr auto kLerpSpeed{6.0f};

  const auto delta_time = xdraw::delta_time();
  const auto drawable_count = memory::Read<int>(entity.ptr + 0x8570);
  auto& state = this->m_inferno_states_[entity.ptr];

  const auto count = std::min(std::max(drawable_count, 0), 64);
  const auto lerp_t = std::fminf(kLerpSpeed * delta_time, 1.0f);

  std::vector<math::Vector3> world_points;
  math::Vector3 avg_pos{};
  auto active_count{0};
  auto any_active{false};

  for (auto i = 0; i < count; ++i) {
    if (!memory::Read<bool>(entity.ptr +
                            SCHEMA("C_Inferno", "m_bFireIsBurning"_hash) + i)) {
      continue;
    }

    const auto base = entity.ptr + 0x1970 + static_cast<std::size_t>(i) * 432u;
    const auto position = memory::Read<math::Vector3>(base);
    const auto current_radius = memory::Read<float>(base + 0x1a8);
    const auto target_radius = memory::Read<float>(base + 0x1ac);

    if (current_radius < 1.0f) {
      continue;
    }

    any_active = true;
    avg_pos = avg_pos + position;
    ++active_count;

    const auto extent = std::fmaxf(60.0f, current_radius);

    for (auto d = 0; d < kNumDirections; ++d) {
      const auto idx = i * kNumDirections + d;
      const auto angle =
          kTwoPi * static_cast<float>(d) / static_cast<float>(kNumDirections);
      const auto dx = std::cosf(angle);
      const auto dy = std::sinf(angle);

      const auto trace_frac =
          memory::Read<float>(base + 0x38 + d * sizeof(float));
      const auto target = std::fminf(trace_frac * target_radius, extent);

      state.current_radii[idx] += (target - state.current_radii[idx]) * lerp_t;

      world_points.push_back(
          position + math::Vector3{dx * state.current_radii[idx],
                                   dy * state.current_radii[idx], 0.0f});
    }
  }

  if (any_active) {
    state.fade_alpha =
        std::fminf(state.fade_alpha + detail::kFadeSpeed * delta_time, 1.0f);
    state.was_active = true;
    state.last_world_points = world_points;

    if (state.spawn_time == std::chrono::steady_clock::time_point{}) {
      state.spawn_time = std::chrono::steady_clock::now();
    }
  } else if (state.was_active) {
    state.fade_alpha -= detail::kFadeSpeed * delta_time;

    if (state.fade_alpha <= 0.0f) {
      this->m_inferno_states_.erase(entity.ptr);
      return;
    }

    world_points = state.last_world_points;
  } else {
    return;
  }

  std::vector<math::Vector2> points;
  points.reserve(world_points.size());

  for (const auto& wp : world_points) {
    const auto projected = systems::g_view.Project(wp);

    if (systems::g_view.ProjectionValid(projected)) {
      points.push_back(projected);
    }
  }

  if (points.size() >= 3) {
    std::ranges::sort(points,
                      [](const math::Vector2& a, const math::Vector2& b) {
                        return a.x < b.x || (a.x == b.x && a.y < b.y);
                      });

    std::vector<math::Vector2> lower;
    std::vector<math::Vector2> upper;

    for (const auto& p : points) {
      while (lower.size() >= 2) {
        const auto& p1 = lower[lower.size() - 2];
        const auto& p2 = lower[lower.size() - 1];

        if ((p2.x - p1.x) * (p.y - p1.y) - (p2.y - p1.y) * (p.x - p1.x) >
            0.0f) {
          break;
        }

        lower.pop_back();
      }

      lower.push_back(p);
    }

    for (auto it = points.rbegin(); it != points.rend(); ++it) {
      while (upper.size() >= 2) {
        const auto& p1 = upper[upper.size() - 2];
        const auto& p2 = upper[upper.size() - 1];

        if ((p2.x - p1.x) * (it->y - p1.y) - (p2.y - p1.y) * (it->x - p1.x) >
            0.0f) {
          break;
        }

        upper.pop_back();
      }

      upper.push_back(*it);
    }

    lower.pop_back();
    upper.pop_back();
    lower.insert(lower.end(), upper.begin(), upper.end());

    if (lower.size() >= 3) {
      auto hx{0.0f};
      auto hy{0.0f};

      for (const auto& p : lower) {
        hx += p.x;
        hy += p.y;
      }

      hx /= static_cast<float>(lower.size());
      hy /= static_cast<float>(lower.size());

      const auto alpha_scale = state.fade_alpha;
      const auto& outline_col = cfg.outline_color.value;
      const auto outline_a = static_cast<std::uint8_t>(
          static_cast<float>(outline_col.a) * alpha_scale);
      const auto hull_span = std::span<const float>(
          reinterpret_cast<const float*>(lower.data()), lower.size() * 2);

      draw_list.polyline(
          hull_span,
          xdraw::color{outline_col.r, outline_col.g, outline_col.b, outline_a},
          true, cfg.outline_thickness);

      if (cfg.glow.value) {
        auto& glow = xdraw::get_glow();
        const auto ga =
            static_cast<std::uint8_t>(static_cast<float>(outline_col.a) *
                                      cfg.glow_strength * alpha_scale);
        const auto glow_col =
            xdraw::color{outline_col.r, outline_col.g, outline_col.b, ga};

        const auto& fill_col = cfg.fill_color.value;
        const auto fa = static_cast<std::uint8_t>(
            static_cast<float>(fill_col.a) * cfg.glow_strength * alpha_scale);

        constexpr auto kRingCount{8};

        for (auto r = 0; r < kRingCount; ++r) {
          const auto t = static_cast<float>(r) / static_cast<float>(kRingCount);
          const auto alpha =
              static_cast<std::uint8_t>(static_cast<float>(fa) * (1.0f - t));

          if (alpha == 0) {
            continue;
          }

          std::vector<math::Vector2> shrunk(lower.size());

          for (auto i = 0u; i < lower.size(); ++i) {
            shrunk[i].x = lower[i].x + (hx - lower[i].x) * t;
            shrunk[i].y = lower[i].y + (hy - lower[i].y) * t;
          }

          const auto span = std::span<const float>(
              reinterpret_cast<const float*>(shrunk.data()), shrunk.size() * 2);
          glow.convex_filled(
              span, xdraw::color{fill_col.r, fill_col.g, fill_col.b, alpha});
        }

        glow.polyline(hull_span, glow_col, true, cfg.outline_thickness + 2.0f);
      }
    }
  }

  const auto& ind_cfg =
      settings::g_esp.m_projectile.m_overlay.m_indicator.GetGroup(2);
  if (ind_cfg.enabled.value) {
    if (active_count > 0) {
      state.last_avg_pos = avg_pos * (1.0f / static_cast<float>(active_count));
    }

    if (state.last_avg_pos.LengthSqr() > 0.0f) {
      const auto indicator_pos = state.last_avg_pos;
      const auto fire_lifetime = memory::Read<float>(
          entity.ptr + SCHEMA("C_Inferno", "m_nFireLifetime"_hash));
      const auto elapsed =
          std::chrono::duration<float>(std::chrono::steady_clock::now() -
                                       state.spawn_time)
              .count();
      const auto remaining = std::max(0.0f, fire_lifetime - elapsed);
      const auto frac = std::clamp(remaining / fire_lifetime, 0.0f, 1.0f);

      const auto target_proj = systems::g_view.ProjectFull(indicator_pos);
      const auto anchor_proj = systems::g_view.ProjectFull(
          indicator_pos +
          math::Vector3{0.0f, 0.0f, detail::kAnchorWorldOffsetZ});

      const auto [screen_w, screen_h] = xdraw::viewport_size();
      const auto sw = static_cast<float>(screen_w);
      const auto sh = static_cast<float>(screen_h);
      const auto center_x = sw * 0.5f;
      const auto center_y = sh * 0.5f;

      constexpr auto kHalfPi{std::numbers::pi_v<float> * 0.5f};

      float icx{}, icy{};
      float dir_angle{};

      if (anchor_proj.on_screen && target_proj.w > 0.0f) {
        icx = anchor_proj.screen.x;
        icy = anchor_proj.screen.y;

        const auto dx = target_proj.screen.x - icx;
        const auto dy = target_proj.screen.y - icy;
        const auto len = std::sqrtf(dx * dx + dy * dy);

        dir_angle = len > 0.1f ? std::atan2f(dy, dx) : kHalfPi;
      } else {
        auto dx = target_proj.screen.x - center_x;
        auto dy = target_proj.screen.y - center_y;

        if (target_proj.w <= 0.0f) {
          dx = -dx;
          dy = -dy;
        }

        const auto len = std::sqrtf(dx * dx + dy * dy);
        if (len >= 1.0f) {
          dx /= len;
          dy /= len;

          auto t_min = std::numeric_limits<float>::max();

          if (std::fabsf(dx) > 0.001f) {
            const auto t_left = (detail::kEdgePadding - center_x) / dx;
            const auto t_right = (sw - detail::kEdgePadding - center_x) / dx;

            if (t_left > 0.0f) {
              t_min = std::fminf(t_min, t_left);
            }

            if (t_right > 0.0f) {
              t_min = std::fminf(t_min, t_right);
            }
          }

          if (std::fabsf(dy) > 0.001f) {
            const auto t_top = (detail::kEdgePadding - center_y) / dy;
            const auto t_bottom = (sh - detail::kEdgePadding - center_y) / dy;

            if (t_top > 0.0f) {
              t_min = std::fminf(t_min, t_top);
            }

            if (t_bottom > 0.0f) {
              t_min = std::fminf(t_min, t_bottom);
            }
          }

          const auto edge_x =
              std::clamp(center_x + dx * t_min, detail::kEdgePadding,
                         sw - detail::kEdgePadding);
          const auto edge_y =
              std::clamp(center_y + dy * t_min, detail::kEdgePadding,
                         sh - detail::kEdgePadding);

          dir_angle = std::atan2f(dy, dx);
          icx = edge_x - std::cosf(dir_angle) * detail::kArrowTipR;
          icy = edge_y - std::sinf(dir_angle) * detail::kArrowTipR;

          this->AddIndicator(middle_draw_list, icx, icy, dir_angle, true, frac,
                             state.fade_alpha, true, ind_cfg);
        }

        return;
      }

      this->AddIndicator(middle_draw_list, icx, icy, dir_angle, true, frac,
                         state.fade_alpha, true, ind_cfg);
    }
  }
}

void Overlay::AddLabel(xdraw::draw_list& draw_list, const math::Vector2& screen,
                       const Info& info,
                       const settings::Esp::Projectile::Overlay::Group& cfg) {
  static constexpr const char* kNames[]{"he", "flash", "smoke", "molotov",
                                        "decoy"};
  static constexpr std::uint32_t kHashes[]{
      "C_HEGrenadeProjectile"_hash, "C_FlashbangProjectile"_hash,
      "C_SmokeGrenadeProjectile"_hash, "C_MolotovProjectile"_hash,
      "C_DecoyProjectile"_hash};

  const auto show_icon =
      cfg.display ==
          settings::Esp::Projectile::Overlay::Group::DisplayType::kIcon ||
      cfg.display ==
          settings::Esp::Projectile::Overlay::Group::DisplayType::kTextAndIcon;
  const auto show_text =
      cfg.display ==
          settings::Esp::Projectile::Overlay::Group::DisplayType::kText ||
      cfg.display ==
          settings::Esp::Projectile::Overlay::Group::DisplayType::kTextAndIcon;
  auto y = screen.y;

  if (show_icon) {
    const auto ico = systems::g_icons.Get(kHashes[info.group_id], 0.35f);
    if (ico && ico->texture) {
      const auto iw = static_cast<float>(ico->width);
      const auto ih = static_cast<float>(ico->height);
      const auto ix = std::floorf(screen.x - iw * 0.5f);
      const auto iy = std::floorf(y - ih * 0.5f);

      constexpr auto kOutline{xdraw::color{0, 0, 0, 255}};

      draw_list.image(ix - 1.0f, iy, iw, ih, ico->texture.Get(), kOutline);
      draw_list.image(ix + 1.0f, iy, iw, ih, ico->texture.Get(), kOutline);
      draw_list.image(ix, iy - 1.0f, iw, ih, ico->texture.Get(), kOutline);
      draw_list.image(ix, iy + 1.0f, iw, ih, ico->texture.Get(), kOutline);
      draw_list.image(ix, iy, iw, ih, ico->texture.Get(), cfg.icon_color);

      y += ih * 0.5f + 1.0f;
    }
  }

  if (show_text) {
    xdraw::push_font(
        rendering::g_fonts.smallest_pixel7[rendering::Fonts::Size::kNormal]);

    const auto name = kNames[info.group_id];
    const auto [w, h] = xdraw::measure_text(name);
    draw_list.text(std::floorf(screen.x - w * 0.5f), std::floorf(y), name,
                   cfg.text_color, xdraw::text_style::outlined);

    xdraw::pop_font();
  }
}

Overlay::Info Overlay::GetInfo(const systems::Entities::Cached& entity) {
  Info info{};
  info.entity = entity.ptr;
  info.schema_hash = entity.schema_hash;
  info.group_id = this->GetProjectileGroup(entity.schema_hash);

  if (!info.entity || info.group_id == UINT32_MAX) {
    return info;
  }

  const auto game_scene_node = memory::Read<std::uintptr_t>(
      info.entity + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
  if (!game_scene_node) {
    info.entity = 0;
    return info;
  }

  info.origin = memory::Read<math::Vector3>(
      game_scene_node + SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_hash));
  info.distance = systems::g_view.Origin().Distance(info.origin) * 0.01905f;

  if (info.group_id == 0 || info.group_id == 1) {
    const auto explode_tick = memory::Read<int>(
        info.entity +
        SCHEMA("C_BaseCSGrenadeProjectile", "m_nExplodeEffectTickBegin"_hash));
    info.detonated = explode_tick > 0;
  } else if (info.group_id == 2) {
    info.effect_tick_begin =
        memory::Read<int>(info.entity + SCHEMA("C_SmokeGrenadeProjectile",
                                               "m_nSmokeEffectTickBegin"_hash));
    info.smoke_active =
        memory::Read<bool>(info.entity + SCHEMA("C_SmokeGrenadeProjectile",
                                                "m_bDidSmokeEffect"_hash));
  } else if (info.group_id == 4) {
    info.effect_tick_begin = memory::Read<int>(
        info.entity + SCHEMA("C_DecoyProjectile", "m_nDecoyShotTick"_hash));
  }

  return info;
}

std::uint32_t Overlay::GetProjectileGroup(std::uint32_t schema_hash) {
  switch (schema_hash) {
    case "C_HEGrenadeProjectile"_hash:
      return 0;
    case "C_FlashbangProjectile"_hash:
      return 1;
    case "C_SmokeGrenadeProjectile"_hash:
      return 2;
    case "C_MolotovProjectile"_hash:
      return 3;
    case "C_DecoyProjectile"_hash:
      return 4;
    default:
      return UINT32_MAX;
  }
}

}  // namespace features::esp::projectile
