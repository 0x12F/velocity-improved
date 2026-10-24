module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>
#include <external/xdraw/xdraw.hpp>
#include <external/xdraw/xui/xui.hpp>

#include "core/systems/schema_macros.h"
#include "protection/address_macros.h"

module velocity;

import <cstddef>;
import <filesystem>;
import <limits>;
import <string_view>;
import <type_traits>;

namespace features::misc {

void Hud::OnRender(xdraw::draw_list& draw_list) {
  if (systems::g_local.IsInCinematic() || systems::g_local.IsInTimeFreeze()) {
    return;
  }

  const auto local = systems::g_local.Get();
  if (!local.pawn || !local.controller ||
      !systems::g_entities.Exists(local.controller) || !local.is_alive) {
    return;
  }

  const auto [screen_w, screen_h] = xdraw::viewport_size();
  const auto cx = static_cast<float>(screen_w) * 0.5f;
  const auto cy = static_cast<float>(screen_h) * 0.5f;

  this->DoScope(draw_list, cx, cy, static_cast<float>(screen_h), local.pawn);
  this->DoCrosshair(draw_list, cx, cy);
  this->DoHat(draw_list, local.pawn);
  this->DoVelocity(draw_list, cx, static_cast<float>(screen_h), local.pawn);
}

void Hud::DoCrosshair(xdraw::draw_list& draw_list, float cx, float cy) const {
  const auto& cfg = settings::g_misc.m_hud.m_crosshair;
  if (!cfg.enabled.value) {
    return;
  }

  if (this->m_scope_anim_ > 0.01f) {
    return;
  }

  const auto s = cfg.size;
  const auto o = cfg.outline;

  if (o > 0.0f) {
    draw_list.rect_filled(cx - s - o, cy - s - o, (s + o) * 2.0f,
                          (s + o) * 2.0f, cfg.outline_color);
  }

  draw_list.rect_filled(cx - s, cy - s, s * 2.0f, s * 2.0f, cfg.color);
}

void Hud::DoScope(xdraw::draw_list& draw_list, float cx, float cy,
                  float screen_h, std::uintptr_t local_pawn) {
  const auto& cfg = settings::g_misc.m_hud.m_scope;
  if (!cfg.enabled.value) {
    return;
  }

  const auto is_scoped = memory::Read<bool>(
      local_pawn + SCHEMA("C_CSPlayerPawn", "m_bIsScoped"_hash));

  this->m_scope_anim_ =
      std::lerp(this->m_scope_anim_, is_scoped ? 1.0f : 0.0f,
                std::min(xdraw::delta_time() * cfg.anim_speed, 1.0f));
  if (this->m_scope_anim_ < 0.01f) {
    this->m_cached_spread_pixels_ = 0.0f;
    this->m_scope_update_frame_ = 0;
    return;
  }

  ++this->m_scope_update_frame_;
  const auto should_recalc =
      this->m_scope_update_frame_ >= 2 || this->m_cached_spread_pixels_ <= 0.0f;

  if (should_recalc) {
    this->m_scope_update_frame_ = 0;

    const auto weapon_services = memory::Read<std::uintptr_t>(
        local_pawn + SCHEMA("C_BasePlayerPawn", "m_pWeaponServices"_hash));
    if (weapon_services) {
      const auto weapon_handle = memory::Read<std::uint32_t>(
          weapon_services +
          SCHEMA("CPlayer_WeaponServices", "m_hActiveWeapon"_hash));
      const auto weapon = (weapon_handle && weapon_handle != 0xffffffff)
                              ? systems::g_entities.Lookup(weapon_handle)
                              : 0ull;

      if (weapon) {
        const auto weapon_vdata = memory::Read<std::uintptr_t>(
            weapon + SCHEMA("C_BaseEntity", "m_nSubclassID"_hash) + 0x8);

        if (weapon_vdata) {
          const auto vel = memory::Read<math::Vector3>(
              local_pawn + SCHEMA("C_BaseEntity", "m_vecAbsVelocity"_hash));
          const auto fire_mode = memory::Read<int>(
              weapon + SCHEMA("C_CSWeaponBase", "m_weaponMode"_hash));
          const auto accuracy_penalty = memory::Read<float>(
              weapon + SCHEMA("C_CSWeaponBase", "m_fAccuracyPenalty"_hash));
          const auto turning_inaccuracy = memory::Read<float>(
              weapon + SCHEMA("C_CSWeaponBase", "m_flTurningInaccuracy"_hash));

          const auto max_speed_pair = memory::Read<std::pair<float, float>>(
              weapon_vdata + SCHEMA("CCSWeaponBaseVData", "m_flMaxSpeed"_hash));
          const auto inaccuracy_move_pair =
              memory::Read<std::pair<float, float>>(
                  weapon_vdata +
                  SCHEMA("CCSWeaponBaseVData", "m_flInaccuracyMove"_hash));
          const auto inaccuracy_jump_initial = memory::Read<float>(
              weapon_vdata +
              SCHEMA("CCSWeaponBaseVData", "m_flInaccuracyJumpInitial"_hash));
          const auto inaccuracy_jump_apex = memory::Read<float>(
              weapon_vdata +
              SCHEMA("CCSWeaponBaseVData", "m_flInaccuracyJumpApex"_hash));

          const auto max_speed =
              fire_mode ? max_speed_pair.second : max_speed_pair.first;
          const auto inaccuracy_move = fire_mode ? inaccuracy_move_pair.second
                                                 : inaccuracy_move_pair.first;

          const auto speed = vel.Length2d();
          const auto flags = memory::Read<std::uint32_t>(
              local_pawn + SCHEMA("C_BaseEntity", "m_fFlags"_hash));
          const auto is_walking = memory::Read<bool>(
              local_pawn + SCHEMA("C_CSPlayerPawn", "m_bIsWalking"_hash));
          const auto on_ground = (flags & 1) != 0;

          const auto edge0 = max_speed * 0.34f;
          const auto edge1 = max_speed * 0.95f;

          auto move_factor =
              (edge0 == edge1)
                  ? (speed >= edge1 ? 1.0f : 0.0f)
                  : std::clamp((speed - edge0) / (edge1 - edge0), 0.0f, 1.0f);
          auto move_inac{0.0f};

          if (move_factor > 0.0f) {
            if (!is_walking) {
              move_factor = std::powf(move_factor, 0.25f);
            }

            move_inac = move_factor * inaccuracy_move;
          }

          auto air_inac{0.0f};

          if (!on_ground) {
            const auto jump_impulse = CONVAR("sv_jump_impulse")->Get<float>();
            const auto sqrt_threshold = std::sqrtf(std::fabsf(jump_impulse));
            const auto sqrt_vertical = std::sqrtf(std::fabsf(vel.z));
            const auto lo = sqrt_threshold * 0.25f;

            if (lo == sqrt_threshold) {
              air_inac = (sqrt_vertical >= sqrt_threshold)
                             ? inaccuracy_jump_initial
                             : inaccuracy_jump_apex;
            } else {
              const auto frac = (sqrt_vertical - lo) / (sqrt_threshold - lo);
              air_inac =
                  inaccuracy_jump_apex +
                  frac * (inaccuracy_jump_initial - inaccuracy_jump_apex);
            }

            air_inac =
                std::clamp(air_inac, 0.0f, inaccuracy_jump_initial * 2.0f);
          }

          const auto inaccuracy =
              std::fminf(1.0f, turning_inaccuracy + move_inac + air_inac +
                                   accuracy_penalty);

          const auto fov_rad =
              systems::g_view.Fov() * (std::numbers::pi_v<float> / 180.0f);
          this->m_cached_spread_pixels_ = inaccuracy * 320.0f /
                                          std::tanf(fov_rad * 0.5f) *
                                          (screen_h / 480.0f);
        }
      }
    }
  }

  this->m_spread_smooth_ =
      std::lerp(this->m_spread_smooth_, this->m_cached_spread_pixels_,
                std::min(xdraw::delta_time() * 50.0f, 1.0f));

  const auto gap = std::max(cfg.gap.value, this->m_spread_smooth_) +
                   (40.0f * (1.0f - this->m_scope_anim_));
  const auto length = cfg.line_length * this->m_scope_anim_;
  const auto alpha =
      static_cast<std::uint8_t>(cfg.color.value.a * this->m_scope_anim_);

  const auto col_clear =
      xdraw::color{cfg.color.value.r, cfg.color.value.g, cfg.color.value.b, 0};
  const auto col_solid = xdraw::color{cfg.color.value.r, cfg.color.value.g,
                                      cfg.color.value.b, alpha};
  const auto& col_start = cfg.fade_in ? col_clear : col_solid;

  if (cfg.glow && alpha > 0) {
    auto& glow = xdraw::get_glow();
    const auto glow_a = static_cast<std::uint8_t>(static_cast<float>(alpha) *
                                                  cfg.glow_strength);
    const auto glow_col = xdraw::color{cfg.color.value.r, cfg.color.value.g,
                                       cfg.color.value.b, glow_a};

    const auto glow_draw_line = [&](float x1, float y1, float x2, float y2) {
      const auto mx = (x1 + x2) * 0.5f;
      const auto my = (y1 + y2) * 0.5f;

      const auto glow_thickness = cfg.thickness + 0.5f;

      const auto gc_clear = xdraw::color{cfg.color.value.r, cfg.color.value.g,
                                         cfg.color.value.b, 0};
      const auto& gc_start = cfg.fade_in ? gc_clear : glow_col;

      const float pts[]{x1, y1, mx, my, x2, y2};
      const xdraw::color cols[]{gc_start, glow_col, glow_col};

      glow.polyline_gradient(pts, cols, false, glow_thickness);
    };

    glow_draw_line(cx, cy - gap, cx, cy - gap - length);
    glow_draw_line(cx, cy + gap, cx, cy + gap + length);
    glow_draw_line(cx - gap, cy, cx - gap - length, cy);
    glow_draw_line(cx + gap, cy, cx + gap + length, cy);
  }

  const auto draw_line = [&](float x1, float y1, float x2, float y2) {
    const auto mx = (x1 + x2) * 0.5f;
    const auto my = (y1 + y2) * 0.5f;

    const float pts[]{x1, y1, mx, my, x2, y2};
    const xdraw::color cols[]{col_start, col_solid, col_solid};

    draw_list.polyline_gradient(pts, cols, false, cfg.thickness);
  };

  draw_line(cx, cy - gap, cx, cy - gap - length);
  draw_line(cx, cy + gap, cx, cy + gap + length);
  draw_line(cx - gap, cy, cx - gap - length, cy);
  draw_line(cx + gap, cy, cx + gap + length, cy);
}

void Hud::DoHat(xdraw::draw_list& draw_list, std::uintptr_t local_pawn) const {
  const auto& cfg = settings::g_misc.m_hud.m_hat;
  if (!cfg.enabled || !systems::g_frame_data.Valid() ||
      !settings::g_misc.m_camera.thirdperson.value) {
    return;
  }

  const auto& primary_col = cfg.color.value;
  const auto& secondary_col = cfg.secondary_color.value;

  const auto game_scene_node = memory::Read<std::uintptr_t>(
      local_pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
  if (!game_scene_node) {
    return;
  }

  const auto hitbox_set = systems::g_hitboxes.Query(game_scene_node, false);
  if (hitbox_set.count < 1 || hitbox_set.entries[0].bone < 0 ||
      hitbox_set.entries[0].bone >= 27) {
    return;
  }

  const auto& head_hb = hitbox_set.entries[0];
  if (head_hb.bone < 0 || head_hb.bone >= 27) {
    return;
  }

  const auto head_bone =
      systems::g_bones.Get(local_pawn, cstypes::bone_ids::kHead);
  if (head_bone.position.LengthSqr() < 1.0f) {
    return;
  }

  const auto hb_mid = (head_hb.mins + head_hb.maxs) * 0.5f;
  const auto center =
      head_bone.rotation.RotateVector(hb_mid) + head_bone.position;

  const auto capsule_a =
      head_bone.rotation.RotateVector(head_hb.mins - hb_mid) + center;
  const auto capsule_b =
      head_bone.rotation.RotateVector(head_hb.maxs - hb_mid) + center;
  const auto top_cap = (capsule_a.z > capsule_b.z) ? capsule_a : capsule_b;
  const auto up_world = (top_cap - center).Normalized();

  auto right_world = up_world.Cross(math::Vector3{0.0f, 1.0f, 0.0f});
  if (right_world.LengthSqr() < 0.001f) {
    right_world = math::Vector3{1.0f, 0.0f, 0.0f};
  } else {
    right_world = right_world.Normalized();
  }

  const auto forward_world = up_world.Cross(right_world).Normalized();
  const auto hat_origin = top_cap + up_world * 0.55f;

  constexpr auto kSegments{32};

  auto project_ring = [&](const math::Vector3& ring_center, float radius,
                          math::Vector2* out, int count) -> bool {
    for (auto i = 0; i < count; ++i) {
      const auto angle = (static_cast<float>(i) / static_cast<float>(count)) *
                         2.0f * std::numbers::pi_v<float>;
      const auto world_pt = ring_center +
                            right_world * (std::cosf(angle) * radius) +
                            forward_world * (std::sinf(angle) * radius);

      const auto sp = systems::g_view.Project(world_pt);
      if (!systems::g_view.ProjectionValid(sp)) {
        return false;
      }

      out[i] = {sp.x, sp.y};
    }

    return true;
  };

  auto draw_ring = [&](const math::Vector2* pts, int count,
                       const xdraw::color& col, float thickness) {
    for (auto i = 0; i < count; ++i) {
      const auto next = (i + 1) % count;
      draw_list.line(pts[i].x, pts[i].y, pts[next].x, pts[next].y, col,
                     thickness);
    }
  };

  if (cfg.type == settings::Misc::Hud::Hat::HatType::kAsa) {
    constexpr auto kBaseRadius{10.0f};
    constexpr auto kRimPoints{24};
    constexpr auto kSpokes{24};

    const auto peak_world = hat_origin + up_world * 7.0f;
    const auto peak_sp = systems::g_view.Project(peak_world);

    if (!systems::g_view.ProjectionValid(peak_sp)) {
      return;
    }

    const math::Vector2 peak{peak_sp.x, peak_sp.y};
    math::Vector2 base[kRimPoints];

    for (auto i = 0; i < kRimPoints; ++i) {
      const auto angle =
          (static_cast<float>(i) / static_cast<float>(kRimPoints)) * 2.0f *
          std::numbers::pi_v<float>;
      const auto world_pt = hat_origin +
                            right_world * (std::cosf(angle) * kBaseRadius) +
                            forward_world * (std::sinf(angle) * kBaseRadius);

      const auto sp = systems::g_view.Project(world_pt);
      if (!systems::g_view.ProjectionValid(sp)) {
        return;
      }

      base[i] = {sp.x, sp.y};
    }

    if (cfg.glow) {
      auto& glow = xdraw::get_glow();
      const auto ga = static_cast<std::uint8_t>(
          static_cast<float>(primary_col.a) * cfg.glow_strength);
      const auto glow_col =
          xdraw::color{primary_col.r, primary_col.g, primary_col.b, ga};

      for (auto i = 0; i < kRimPoints; ++i) {
        const auto next = (i + 1) % kRimPoints;
        glow.line(base[i].x, base[i].y, base[next].x, base[next].y, glow_col,
                  3.0f);
      }

      const auto spoke_ga = static_cast<std::uint8_t>(
          static_cast<float>(secondary_col.a) * cfg.glow_strength);
      const auto spoke_glow_col = xdraw::color{secondary_col.r, secondary_col.g,
                                               secondary_col.b, spoke_ga};

      for (auto i = 0; i < kSpokes; ++i) {
        const auto idx = (i * kRimPoints) / kSpokes;
        glow.line(base[idx].x, base[idx].y, peak.x, peak.y, spoke_glow_col,
                  2.0f);
      }
    }

    for (auto i = 0; i < kRimPoints; ++i) {
      const auto next = (i + 1) % kRimPoints;
      draw_list.line(base[i].x, base[i].y, base[next].x, base[next].y,
                     primary_col, 1.2f);
    }

    for (auto i = 0; i < kSpokes; ++i) {
      const auto idx = (i * kRimPoints) / kSpokes;
      draw_list.line(base[idx].x, base[idx].y, peak.x, peak.y, secondary_col,
                     0.75f);
    }

    return;
  }

  constexpr auto kBrimRadius{6.5f};
  constexpr auto kCrownBaseRadius{5.0f};
  constexpr auto kCrownTopRadius{4.2f};
  constexpr auto kCrownHeight{3.0f};
  constexpr auto kBrimDroop{1.5f};
  constexpr auto kDomeHeight{1.8f};

  constexpr auto kCrownStitches{2};
  constexpr auto kBrimStitches{4};
  constexpr auto kStitchDashOn{3.0f};
  constexpr auto kStitchDashOff{3.0f};

  const auto junction_center = hat_origin;
  const auto crown_top_center = junction_center + up_world * kCrownHeight;
  const auto brim_edge_center = junction_center - up_world * kBrimDroop;

  auto draw_ring_dashed = [&](const math::Vector2* pts, int count,
                              const xdraw::color& col, float thickness,
                              float dash_on, float dash_off) {
    auto accum{0.0f};
    auto drawing{true};

    for (auto i = 0; i < count; ++i) {
      const auto next = (i + 1) % count;
      const auto dx = pts[next].x - pts[i].x;
      const auto dy = pts[next].y - pts[i].y;
      const auto seg_len = std::sqrtf(dx * dx + dy * dy);

      if (seg_len < 0.001f) {
        continue;
      }

      auto t_start{0.0f};

      while (t_start < 1.0f) {
        const auto budget = drawing ? dash_on : dash_off;
        const auto remaining = budget - accum;
        const auto t_step = remaining / seg_len;
        const auto t_end = std::min(t_start + t_step, 1.0f);

        if (drawing) {
          const auto x0 = pts[i].x + dx * t_start;
          const auto y0 = pts[i].y + dy * t_start;
          const auto x1 = pts[i].x + dx * t_end;
          const auto y1 = pts[i].y + dy * t_end;
          draw_list.line(x0, y0, x1, y1, col, thickness);
        }

        accum += (t_end - t_start) * seg_len;

        if (accum >= budget - 0.01f) {
          drawing = !drawing;
          accum = 0.0f;
        }

        t_start = t_end;
      }
    }
  };

  auto find_extremes = [&](const math::Vector2* pts, int count, int& out_left,
                           int& out_right) {
    auto min_x = std::numeric_limits<float>::max();
    auto max_x = std::numeric_limits<float>::lowest();
    out_left = 0;
    out_right = 0;

    for (auto i = 0; i < count; ++i) {
      if (pts[i].x < min_x) {
        min_x = pts[i].x;
        out_left = i;
      }
      if (pts[i].x > max_x) {
        max_x = pts[i].x;
        out_right = i;
      }
    }
  };

  math::Vector2 brim_pts[kSegments];
  math::Vector2 junction_pts[kSegments];
  math::Vector2 crown_top_pts[kSegments];

  if (!project_ring(brim_edge_center, kBrimRadius, brim_pts, kSegments)) {
    return;
  }

  if (!project_ring(junction_center, kCrownBaseRadius, junction_pts,
                    kSegments)) {
    return;
  }

  if (!project_ring(crown_top_center, kCrownTopRadius, crown_top_pts,
                    kSegments)) {
    return;
  }

  const auto pole_world = crown_top_center + up_world * kDomeHeight;
  const auto pole_sp = systems::g_view.Project(pole_world);

  if (!systems::g_view.ProjectionValid(pole_sp)) {
    return;
  }

  int brim_l{}, brim_r{}, junc_l{}, junc_r{}, top_l{}, top_r{};
  find_extremes(brim_pts, kSegments, brim_l, brim_r);
  find_extremes(junction_pts, kSegments, junc_l, junc_r);
  find_extremes(crown_top_pts, kSegments, top_l, top_r);

  constexpr auto kArcSteps{16};
  math::Vector2 arc_left[kArcSteps + 1];
  math::Vector2 arc_right[kArcSteps + 1];

  const auto left_azimuth =
      (static_cast<float>(top_l) / static_cast<float>(kSegments)) * 2.0f *
      std::numbers::pi_v<float>;
  const auto right_azimuth =
      (static_cast<float>(top_r) / static_cast<float>(kSegments)) * 2.0f *
      std::numbers::pi_v<float>;

  for (auto i = 0; i <= kArcSteps; ++i) {
    const auto t = static_cast<float>(i) / static_cast<float>(kArcSteps);
    const auto phi = t * (std::numbers::pi_v<float> * 0.5f);
    const auto r = kCrownTopRadius * std::cosf(phi);
    const auto h = std::sinf(phi) * kDomeHeight;

    const auto left_world = crown_top_center + up_world * h +
                            right_world * (std::cosf(left_azimuth) * r) +
                            forward_world * (std::sinf(left_azimuth) * r);
    const auto right_world_pt = crown_top_center + up_world * h +
                                right_world * (std::cosf(right_azimuth) * r) +
                                forward_world * (std::sinf(right_azimuth) * r);

    const auto lsp = systems::g_view.Project(left_world);
    const auto rsp = systems::g_view.Project(right_world_pt);

    arc_left[i] = {lsp.x, lsp.y};
    arc_right[i] = {rsp.x, rsp.y};
  }

  if (cfg.glow) {
    auto& glow = xdraw::get_glow();
    const auto ga = static_cast<std::uint8_t>(
        static_cast<float>(primary_col.a) * cfg.glow_strength);
    const auto glow_col =
        xdraw::color{primary_col.r, primary_col.g, primary_col.b, ga};

    for (auto i = 0; i < kSegments; ++i) {
      const auto next = (i + 1) % kSegments;
      glow.line(brim_pts[i].x, brim_pts[i].y, brim_pts[next].x,
                brim_pts[next].y, glow_col, 3.0f);
    }

    for (auto i = 0; i < kArcSteps; ++i) {
      glow.line(arc_left[i].x, arc_left[i].y, arc_left[i + 1].x,
                arc_left[i + 1].y, glow_col, 3.0f);
      glow.line(arc_right[i].x, arc_right[i].y, arc_right[i + 1].x,
                arc_right[i + 1].y, glow_col, 3.0f);
    }
  }

  draw_ring(brim_pts, kSegments, primary_col, 1.4f);
  draw_ring(junction_pts, kSegments, primary_col, 1.2f);

  draw_list.line(brim_pts[brim_l].x, brim_pts[brim_l].y, junction_pts[junc_l].x,
                 junction_pts[junc_l].y, primary_col, 1.4f);
  draw_list.line(brim_pts[brim_r].x, brim_pts[brim_r].y, junction_pts[junc_r].x,
                 junction_pts[junc_r].y, primary_col, 1.4f);

  draw_list.line(junction_pts[junc_l].x, junction_pts[junc_l].y,
                 crown_top_pts[top_l].x, crown_top_pts[top_l].y, primary_col,
                 1.2f);
  draw_list.line(junction_pts[junc_r].x, junction_pts[junc_r].y,
                 crown_top_pts[top_r].x, crown_top_pts[top_r].y, primary_col,
                 1.2f);

  for (auto i = 0; i < kArcSteps; ++i) {
    draw_list.line(arc_left[i].x, arc_left[i].y, arc_left[i + 1].x,
                   arc_left[i + 1].y, primary_col, 1.2f);
    draw_list.line(arc_right[i].x, arc_right[i].y, arc_right[i + 1].x,
                   arc_right[i + 1].y, primary_col, 1.2f);
  }

  const auto stitch_a =
      static_cast<std::uint8_t>(static_cast<float>(secondary_col.a) * 0.7f);
  const auto stitch_draw_col =
      xdraw::color{secondary_col.r, secondary_col.g, secondary_col.b, stitch_a};

  for (auto s = 1; s <= kCrownStitches; ++s) {
    const auto t =
        static_cast<float>(s) / static_cast<float>(kCrownStitches + 1);
    const auto lerp_center =
        junction_center + (crown_top_center - junction_center) * t;
    const auto lerp_radius =
        kCrownBaseRadius + (kCrownTopRadius - kCrownBaseRadius) * t;

    math::Vector2 stitch_ring[kSegments];
    if (!project_ring(lerp_center, lerp_radius, stitch_ring, kSegments)) {
      continue;
    }

    draw_ring_dashed(stitch_ring, kSegments, stitch_draw_col, 0.6f,
                     kStitchDashOn, kStitchDashOff);
  }

  for (auto s = 1; s <= kBrimStitches; ++s) {
    const auto t =
        static_cast<float>(s) / static_cast<float>(kBrimStitches + 1);
    const auto lerp_center =
        junction_center + (brim_edge_center - junction_center) * t;
    const auto lerp_radius =
        kCrownBaseRadius + (kBrimRadius - kCrownBaseRadius) * t;

    math::Vector2 stitch_ring[kSegments];
    if (!project_ring(lerp_center, lerp_radius, stitch_ring, kSegments)) {
      continue;
    }

    draw_ring_dashed(stitch_ring, kSegments, stitch_draw_col, 0.6f,
                     kStitchDashOn, kStitchDashOff);
  }
}

void Hud::DoVelocity(xdraw::draw_list& draw_list, float cx, float screen_h,
                     std::uintptr_t local_pawn) {
  const auto& cfg = settings::g_misc.m_hud.m_velocity;
  if (!cfg.counter.value && !cfg.chart.value) {
    return;
  }

  const auto velocity = memory::Read<math::Vector3>(
      local_pawn + SCHEMA("C_BaseEntity", "m_vecAbsVelocity"_hash));
  const auto speed = velocity.Length2d();
  const auto dt = xdraw::delta_time();

  this->m_velocity_smoothed_ +=
      (speed - this->m_velocity_smoothed_) * std::min(14.0f * dt, 1.0f);
  this->m_velocity_history_[this->m_velocity_history_head_] =
      this->m_velocity_smoothed_;
  this->m_velocity_history_head_ =
      (this->m_velocity_history_head_ + 1) % kVelocityHistory;
  this->m_velocity_history_count_ =
      std::min(this->m_velocity_history_count_ + 1, kVelocityHistory);

  const auto& s = xui::ctx().style;
  const xdraw::color accent = cfg.color;
  const auto accent_dim =
      xdraw::color{accent.r, accent.g, accent.b,
                   static_cast<std::uint8_t>(accent.a * 0.45f)};
  const auto accent_fill =
      xdraw::color{accent.r, accent.g, accent.b,
                   static_cast<std::uint8_t>(accent.a * 0.18f)};

  constexpr auto kPanelR{10.0f};
  constexpr auto kInnerPad{4.0f};
  constexpr auto kInnerR{7.0f};
  constexpr auto kTextPadX{8.0f};
  constexpr auto kTextNudge{-1.0f};
  constexpr auto kSectionGap{4.0f};

  const auto chart_w = std::clamp(cfg.chart_width.value, 120.0f, 320.0f);
  const auto chart_h = std::clamp(cfg.chart_height.value, 24.0f, 80.0f);
  const auto chart_inner_h = chart_h - kInnerPad * 2.0f;
  const auto chart_inner_w = chart_w - kInnerPad * 2.0f;

  char speed_buf[16]{};
  std::snprintf(speed_buf, sizeof(speed_buf), "%.0f",
                this->m_velocity_smoothed_);

  const auto [speed_vw, speed_vh] = xdraw::measure_text(speed_buf);
  const auto [speed_uw, speed_uh] = xdraw::measure_text(" u/s");
  const auto counter_pill_w = speed_vw + speed_uw + kTextPadX * 2.0f;
  const auto counter_pill_h = speed_vh + kInnerPad * 2.0f;

  const auto panel_w =
      cfg.chart.value ? chart_w : counter_pill_w + kInnerPad * 2.0f;
  const auto counter_block_h =
      cfg.counter.value ? counter_pill_h + kInnerPad * 2.0f : 0.0f;
  const auto chart_block_h = cfg.chart.value ? chart_h : 0.0f;
  const auto stack_gap =
      (cfg.counter.value && cfg.chart.value) ? kSectionGap : 0.0f;
  const auto panel_h = counter_block_h + stack_gap + chart_block_h;

  const auto bottom_offset = std::clamp(cfg.bottom_offset.value, 20.0f, 220.0f);
  const auto panel_x = std::floor(cx - panel_w * 0.5f);
  const auto panel_y = std::floor(screen_h - bottom_offset - panel_h);

  draw_list.rect_filled_blurred(panel_x, panel_y, panel_w, panel_h,
                                xdraw::corner_radius{kPanelR});
  draw_list.rect_filled(panel_x, panel_y, panel_w, panel_h, s.window_bg,
                        xdraw::corner_radius{kPanelR});

  auto content_y = panel_y;

  if (cfg.counter.value) {
    const auto pill_x = std::floor(cx - counter_pill_w * 0.5f);
    const auto pill_y = content_y + kInnerPad;
    draw_list.rect_filled(pill_x, pill_y, counter_pill_w, counter_pill_h,
                          s.child_bg, xdraw::corner_radius{kInnerR});
    draw_list.text(pill_x + kTextPadX,
                   pill_y + (counter_pill_h - speed_vh) * 0.5f + kTextNudge,
                   speed_buf, accent);
    draw_list.text(pill_x + kTextPadX + speed_vw,
                   pill_y + (counter_pill_h - speed_uh) * 0.5f + kTextNudge,
                   " u/s", accent_dim);
    content_y += counter_block_h + stack_gap;
  }

  if (!cfg.chart.value || this->m_velocity_history_count_ < 2 ||
      chart_inner_w <= 1.0f || chart_inner_h <= 1.0f) {
    return;
  }

  const auto chart_x = panel_x + kInnerPad;
  const auto chart_y = content_y + kInnerPad;
  draw_list.rect_filled(chart_x, chart_y, chart_w - kInnerPad * 2.0f,
                        chart_inner_h, s.child_bg,
                        xdraw::corner_radius{kInnerR});

  auto peak = 50.0f;
  for (std::size_t i = 0; i < this->m_velocity_history_count_; ++i) {
    peak = std::max(peak, this->m_velocity_history_[i]);
  }

  const auto target_scale =
      std::ceil(std::max(peak, this->m_velocity_smoothed_) / 50.0f) * 50.0f;
  this->m_velocity_scale_ +=
      (target_scale - this->m_velocity_scale_) * std::min(6.0f * dt, 1.0f);
  const auto scale = std::max(this->m_velocity_scale_, 50.0f);

  const auto plot_x = chart_x + kInnerPad;
  const auto plot_y = chart_y + kInnerPad;
  const auto plot_w = chart_inner_w - kInnerPad * 2.0f;
  const auto plot_h = chart_inner_h - kInnerPad * 2.0f;
  const auto baseline_y = std::floor(plot_y + plot_h);

  draw_list.line(plot_x, baseline_y, plot_x + plot_w, baseline_y,
                 xdraw::color{255, 255, 255, 18}, 1.0f, false);

  const auto sample_count = this->m_velocity_history_count_;
  const auto step_x = plot_w / static_cast<float>(sample_count - 1);

  std::array<float, kVelocityHistory * 2> points{};
  std::array<xdraw::color, kVelocityHistory> point_colors{};

  for (std::size_t i = 0; i < sample_count; ++i) {
    const auto idx =
        (this->m_velocity_history_head_ + kVelocityHistory - sample_count + i) %
        kVelocityHistory;
    const auto value = this->m_velocity_history_[idx];
    const auto nx = plot_x + step_x * static_cast<float>(i);
    const auto ny = std::floor(plot_y + plot_h - (value / scale) * plot_h);

    points[i * 2] = nx;
    points[i * 2 + 1] = ny;
    point_colors[i] = accent;
  }

  for (std::size_t i = 0; i + 1 < sample_count; ++i) {
    const auto x0 = points[i * 2];
    const auto y0 = points[i * 2 + 1];
    const auto x1 = points[(i + 1) * 2];
    const auto y1 = points[(i + 1) * 2 + 1];

    const auto fill_top = std::min(y0, y1);
    const auto fill_h = std::max(0.0f, baseline_y - fill_top);
    if (fill_h > 0.0f) {
      const auto seg_w = std::max(1.0f, x1 - x0);
      draw_list.rect_filled_gradient(
          x0, fill_top, seg_w, fill_h, accent_fill, accent_fill,
          xdraw::color{accent_fill.r, accent_fill.g, accent_fill.b, 0},
          xdraw::color{accent_fill.r, accent_fill.g, accent_fill.b, 0});
    }
  }

  draw_list.polyline_gradient(
      std::span<const float>{points.data(), sample_count * 2},
      std::span<const xdraw::color>{point_colors.data(), sample_count}, false,
      1.5f, false);

  const auto dot_x = points[(sample_count - 1) * 2];
  const auto dot_y = points[(sample_count - 1) * 2 + 1];
  draw_list.circle_filled(dot_x, dot_y, 2.0f, accent);
}

}  // namespace features::misc
