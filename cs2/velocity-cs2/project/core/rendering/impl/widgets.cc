module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>
#include <external/stackwalker/stackwalker.hpp>
#include <external/xdraw/xdraw.hpp>

#include "core/systems/schema_macros.h"

module velocity;

import <cstddef>;
import <filesystem>;
import <limits>;
import <string_view>;
import <thread>;
import <type_traits>;

namespace rendering {

void Widgets::Draw() {
  auto& dl = xdraw::get();

  if (settings::g_misc.m_watermark.enabled.value) {
    this->Watermark(dl);
  }

  this->Keybinds(dl);
}

void Widgets::Watermark(xdraw::draw_list& draw_list) {
  const auto [screen_w, screen_h] = xdraw::viewport_size();
  const auto& s = xui::ctx().style;
  const auto& wm = settings::g_misc.m_watermark;
  const auto framerate = xdraw::framerate();
  const auto local = systems::g_local.Get();

  constexpr auto kH{24.0f};
  constexpr auto kMargin{10.0f};
  constexpr auto kR{8.0f};
  constexpr auto kInnerR{6.0f};
  constexpr auto kInnerPad{2.0f};
  constexpr auto kTextPadX{8.0f};
  constexpr auto kTextNudge{0.5f};
  constexpr auto kSectionSpacing{2.0f};
  constexpr auto kLogoIconSize{12.0f};
  constexpr auto kLogoIconPad{7.0f};

  // ── time ────────────────────────────────────────────────────────────
  SYSTEMTIME st{};
  GetLocalTime(&st);
  char time_buf[8]{};
  std::snprintf(time_buf, sizeof(time_buf), "%02d:%02d", st.wHour, st.wMinute);

  // ── fps ─────────────────────────────────────────────────────────────
  static auto smoothed_fps{0.0f};
  if (smoothed_fps == 0.0f) smoothed_fps = framerate;
  smoothed_fps +=
      (framerate - smoothed_fps) * std::min(2.0f * xdraw::delta_time(), 1.0f);
  char fps_val[8]{};
  std::snprintf(fps_val, sizeof(fps_val), "%.0f", smoothed_fps);

  // ── ping ────────────────────────────────────────────────────────────
  auto ping{0};
  if (local.is_alive && local.controller &&
      systems::g_entities.Exists(local.controller))
    ping = memory::Read<std::uint32_t>(
        local.controller + SCHEMA("CCSPlayerController", "m_iPing"_hash));
  char ping_val[8]{};
  std::snprintf(ping_val, sizeof(ping_val), "%d", ping);

  // ── map name (stored reliably from level_initialization hook) ────────
  const bool has_map = wm.show_map.value && !s_map_name_.empty();

  // ── tick rate (measured from server_tick delta over ~2 s of real time) ─
  static auto last_server_tick{0};
  static auto last_curtime{0.0f};
  static auto measured_tickrate{0};

  if (local.controller) {
    const auto net_for_tick = addresses::globals::network_client_service;
    const auto tick_state =
        net_for_tick ? memory::CallVfunc<std::uintptr_t>(net_for_tick, 23) : 0;
    const auto server_tick =
        tick_state ? memory::Read<int>(tick_state + 892) : 0;
    const auto gv =
        memory::Read<std::uintptr_t>(addresses::globals::global_vars);
    const auto curtime = gv ? memory::Read<float>(gv + 0x30) : 0.0f;

    if (server_tick > 0 && last_server_tick > 0 &&
        curtime - last_curtime >= 2.0f) {
      const auto tick_delta = server_tick - last_server_tick;
      const auto time_delta = curtime - last_curtime;
      if (tick_delta > 0 && time_delta > 0.5f) {
        const auto rate = static_cast<int>(std::round(tick_delta / time_delta));
        if (rate >= 16 && rate <= 256) measured_tickrate = rate;
      }
      last_server_tick = server_tick;
      last_curtime = curtime;
    } else if (last_server_tick == 0 && server_tick > 0) {
      last_server_tick = server_tick;
      last_curtime = curtime;
    }
  } else {
    last_server_tick = 0;
    last_curtime = 0.0f;
    measured_tickrate = 0;
  }

  const bool has_tick =
      wm.show_tick.value && local.controller && measured_tickrate > 0;
  char tick_val[8]{};
  if (has_tick)
    std::snprintf(tick_val, sizeof(tick_val), "%d", measured_tickrate);

  // ── velocity ──────────────────────────────────────────────────────
  const bool has_velocity =
      wm.show_velocity.value && local.is_alive && local.pawn;
  static auto smoothed_velocity{0.0f};
  char vel_val[8]{};
  if (has_velocity) {
    const auto velocity = memory::Read<math::Vector3>(
        local.pawn + SCHEMA("C_BaseEntity", "m_vecAbsVelocity"_hash));
    const auto speed = velocity.Length2d();
    smoothed_velocity += (speed - smoothed_velocity) *
                         std::min(8.0f * xdraw::delta_time(), 1.0f);
    std::snprintf(vel_val, sizeof(vel_val), "%.0f", smoothed_velocity);
  } else {
    smoothed_velocity = 0.0f;
  }

  // ── logo ────────────────────────────────────────────────────────────
  const auto logo_scale = kLogoIconSize / 12.0f;
  static auto logo_w = 0, logo_h = 0;
  static const auto logo = xdraw::load_svg(
      R"(<svg width="15" height="12" viewBox="0 0 15 12" fill="none" xmlns="http://www.w3.org/2000/svg"><path d="M0.131688 9.02626L6.40009 0.551371C6.94385 -0.18379 8.07861 -0.18379 8.62237 0.551371L14.8681 8.99564C15.2003 9.44476 14.8666 10.0674 14.2937 10.0674H12.9205C12.5679 10.0674 12.2512 9.86022 12.1214 9.54481L10.2638 5.0302C10.1631 4.78558 9.91739 4.62489 9.64393 4.62489C9.52346 4.62489 9.43618 4.73535 9.46834 4.84701L11.2808 11.1405C11.4053 11.5727 11.0674 12 10.6014 12H9.36667C9.09606 12 8.86578 11.8102 8.82422 11.5529L7.71627 3.99646C7.68733 3.81739 7.36739 3.82052 7.33103 3.99836L5.84387 11.5738C5.79319 11.8214 5.56756 12 5.30526 12H4.07334C3.594 12 3.25442 11.5497 3.40311 11.1112L5.4932 4.94752C5.54344 4.79932 5.42867 4.64708 5.26665 4.64708H5.22153C4.95747 4.64708 4.71827 4.79707 4.61165 5.02955L2.5027 9.62798C2.36225 9.93422 2.04374 10.1288 1.69595 10.1208L0.689398 10.0978C0.124293 10.0848 -0.195983 9.46937 0.131756 9.02626H0.131688Z" fill="#111111"/></svg>)",
      logo_scale, &logo_w, &logo_h);

  const auto inner_h = kH - kInnerPad * 2.0f;
  const auto logo_draw_w = static_cast<float>(logo_w);

  // ── measure text ─────────────────────────────────────────────────────
  const auto [name_tw, name_th] = xdraw::measure_text("velocity.cat");
  const auto [user_tw, user_th] = xdraw::measure_text("developer");
  const auto [ping_vw, ping_vh] = xdraw::measure_text(ping_val);
  const auto [ping_uw, ping_uh] = xdraw::measure_text(" ms");
  const auto [fps_vw, fps_vh] = xdraw::measure_text(fps_val);
  const auto [fps_uw, fps_uh] = xdraw::measure_text(" fps");
  const auto [time_tw, time_th] = xdraw::measure_text(time_buf);

  float map_tw{}, map_th{};
  if (has_map)
    std::tie(map_tw, map_th) = xdraw::measure_text(s_map_name_.c_str());

  float tick_vw{}, tick_vh{}, tick_uw{}, tick_uh{};
  if (has_tick) {
    std::tie(tick_vw, tick_vh) = xdraw::measure_text(tick_val);
    std::tie(tick_uw, tick_uh) = xdraw::measure_text(" tick");
  }

  float vel_vw{}, vel_vh{}, vel_uw{}, vel_uh{};
  if (has_velocity) {
    std::tie(vel_vw, vel_vh) = xdraw::measure_text(vel_val);
    std::tie(vel_uw, vel_uh) = xdraw::measure_text(" u/s");
  }

  // ── pill widths ──────────────────────────────────────────────────────
  const auto logo_pill_w =
      kLogoIconPad + logo_draw_w + kLogoIconPad + name_tw + kTextPadX;
  const auto user_pill_w = user_tw + kTextPadX * 2.0f;
  const auto ping_pill_w = ping_vw + ping_uw + kTextPadX * 2.0f;
  const auto fps_pill_w = fps_vw + fps_uw + kTextPadX * 2.0f;
  const auto time_pill_w = time_tw + kTextPadX * 2.0f;
  const auto map_pill_w = map_tw + kTextPadX * 2.0f;
  const auto tick_pill_w = tick_vw + tick_uw + kTextPadX * 2.0f;
  const auto vel_pill_w = vel_vw + vel_uw + kTextPadX * 2.0f;

  // ── dynamic total width ──────────────────────────────────────────────
  float target_w = kInnerPad + logo_pill_w + kSectionSpacing;
  if (wm.show_user.value) target_w += user_pill_w + kSectionSpacing;
  if (has_map) target_w += map_pill_w + kSectionSpacing;
  if (wm.show_ping.value) target_w += ping_pill_w + kSectionSpacing;
  if (has_velocity) target_w += vel_pill_w + kSectionSpacing;
  if (wm.show_fps.value) target_w += fps_pill_w + kSectionSpacing;
  if (has_tick) target_w += tick_pill_w + kSectionSpacing;
  if (wm.show_time.value) target_w += time_pill_w + kSectionSpacing;
  target_w = target_w - kSectionSpacing + kInnerPad;

  static auto smoothed_w{0.0f};
  if (smoothed_w == 0.0f) smoothed_w = target_w;
  smoothed_w +=
      (target_w - smoothed_w) * std::min(8.0f * xdraw::delta_time(), 1.0f);

  const auto w = smoothed_w;
  const auto x = static_cast<float>(screen_w) - w - kMargin;
  const auto y = kMargin;

  draw_list.rect_filled_blurred(x, y, w, kH, xdraw::corner_radius{kR});
  draw_list.rect_filled(x, y, w, kH, s.window_bg, xdraw::corner_radius{kR});

  auto cx = x + kInnerPad;

  auto draw_split_pill = [&](const char* value, float vw, float vh,
                             const char* unit, float uw, float uh,
                             float pill_w) {
    draw_list.rect_filled(cx, y + kInnerPad, pill_w, inner_h, s.child_bg,
                          xdraw::corner_radius{kInnerR});
    draw_list.text(cx + kTextPadX, y + (kH - vh) * 0.5f + kTextNudge, value,
                   s.accent);
    draw_list.text(cx + kTextPadX + vw, y + (kH - uh) * 0.5f + kTextNudge, unit,
                   s.text_dim);
    cx += pill_w + kSectionSpacing;
  };

  auto draw_pill = [&](const char* text, float tw, float th, float pill_w) {
    draw_list.rect_filled(cx, y + kInnerPad, pill_w, inner_h, s.child_bg,
                          xdraw::corner_radius{kInnerR});
    draw_list.text(cx + kTextPadX, y + (kH - th) * 0.5f + kTextNudge, text,
                   s.accent);
    cx += pill_w + kSectionSpacing;
  };

  // logo pill (always shown)
  draw_list.rect_filled(cx, y + kInnerPad, logo_pill_w, inner_h, s.accent,
                        xdraw::corner_radius{kInnerR});
  if (logo)
    draw_list.image(cx + kLogoIconPad,
                    y + (kH - static_cast<float>(logo_h)) * 0.5f,
                    static_cast<float>(logo_w), static_cast<float>(logo_h),
                    logo.Get(), s.checkbox_mark_icon);
  draw_list.text(cx + kLogoIconPad + logo_draw_w + kLogoIconPad,
                 y + (kH - name_th) * 0.5f + kTextNudge, "velocity.cat",
                 s.checkbox_mark_icon);
  cx += logo_pill_w + kSectionSpacing;

  if (wm.show_user.value) draw_pill("developer", user_tw, user_th, user_pill_w);
  if (has_map) draw_pill(s_map_name_.c_str(), map_tw, map_th, map_pill_w);
  if (wm.show_ping.value)
    draw_split_pill(ping_val, ping_vw, ping_vh, " ms", ping_uw, ping_uh,
                    ping_pill_w);
  if (has_velocity)
    draw_split_pill(vel_val, vel_vw, vel_vh, " u/s", vel_uw, vel_uh,
                    vel_pill_w);
  if (wm.show_fps.value)
    draw_split_pill(fps_val, fps_vw, fps_vh, " fps", fps_uw, fps_uh,
                    fps_pill_w);
  if (has_tick)
    draw_split_pill(tick_val, tick_vw, tick_vh, " tick", tick_uw, tick_uh,
                    tick_pill_w);
  if (wm.show_time.value) draw_pill(time_buf, time_tw, time_th, time_pill_w);
}

void Widgets::Keybinds(xdraw::draw_list& draw_list) {
  struct RowAnimT {
    animation::Fade alpha;
    animation::Spring offset_y;
    bool active_this_frame{false};
  };

  static std::map<std::string, RowAnimT> row_states;
  static animation::Fade container_alpha;
  static animation::Spring smoothed_base_y;

  const auto [screen_w, screen_h] = xdraw::viewport_size();
  const auto& s = xui::ctx().style;

  constexpr auto kMargin{10.0f};
  constexpr auto kRowSpacing{3.0f};
  constexpr auto kRowH{21.0f};
  constexpr auto kHeaderH{24.0f};
  constexpr auto kR{8.0f};
  constexpr auto kInnerR{6.0f};
  constexpr auto kInnerPad{2.0f};
  constexpr auto kTextPadX{8.0f};
  constexpr auto kTextNudge{0.5f};
  constexpr auto kIconSize{20.0f};
  constexpr auto kIconInnerPad{4.0f};

  struct BindEntry {
    const char* name;
    char value[32];
    bool has_value_pill;
    xui::bind_mode mode;
  };

  BindEntry entries[32]{};
  auto count{0};

  const auto& ctx = features::combat::g_shared.Ctx();
  const auto has_weapon = ctx.valid &&
                          ctx.weapon_type >= cstypes::weapon_type::kPistol &&
                          ctx.weapon_type <= cstypes::weapon_type::kLmg;

  for (const auto setting : xui::binds::all()) {
    if (!setting || setting->bind.key == 0 || !setting->bind.active ||
        count >= 32) {
      continue;
    }

    auto is_rage_group{false};
    for (auto i = 0u; i < settings::Combat::Ragebot::kGroupCount; ++i) {
      const auto& g = settings::g_combat.m_ragebot.groups[i];
      if (setting == &g.min_damage_override ||
          setting == &g.hitchance_override || setting == &g.force_shot ||
          setting == &g.force_shot_air || setting == &g.body_aim ||
          setting == &g.silent || setting == &g.no_spread) {
        is_rage_group = true;
        break;
      }
    }

    if (is_rage_group) {
      if (!settings::g_combat.m_ragebot.enabled || !has_weapon) {
        continue;
      }

      const auto active_group =
          &settings::g_combat.m_ragebot.GetGroup(ctx.weapon_type);
      auto is_active{false};

      for (auto i = 0u; i < settings::Combat::Ragebot::kGroupCount; ++i) {
        const auto& g = settings::g_combat.m_ragebot.groups[i];
        if (&g == active_group) {
          if (setting == &g.min_damage_override ||
              setting == &g.hitchance_override || setting == &g.force_shot ||
              setting == &g.force_shot_air || setting == &g.body_aim) {
            is_active = true;
          }
          break;
        }
      }

      if (!is_active) {
        continue;
      }

      auto& e = entries[count++];
      e.name = setting->name.c_str();
      e.mode = setting->bind.mode;

      if (setting == &active_group->min_damage_override) {
        std::snprintf(e.value, sizeof(e.value), "%d",
                      active_group->min_damage_override_value.value);
        e.has_value_pill = true;
      } else if (setting == &active_group->hitchance_override) {
        std::snprintf(e.value, sizeof(e.value), "%d%%",
                      active_group->hitchance_override_value.value);
        e.has_value_pill = true;
      } else {
        e.value[0] = '\0';
        e.has_value_pill = false;
      }
      continue;
    }

    auto is_legit_group{false};
    for (auto i = 0u; i < settings::Combat::Legitbot::kGroupCount; ++i) {
      const auto& g = settings::g_combat.m_legitbot.groups[i];
      if (setting == &g.aimbot || setting == &g.rcs ||
          setting == &g.standalone_rcs || setting == &g.triggerbot ||
          setting == &g.autowall || setting == &g.visualize_fov ||
          setting == &g.trigger_head_only || setting == &g.give_me_your_seed) {
        is_legit_group = true;
        break;
      }
    }

    if (is_legit_group) {
      if (!settings::g_combat.m_legitbot.enabled.value || !has_weapon) {
        continue;
      }

      const auto* active_group =
          &settings::g_combat.m_legitbot.GetGroup(ctx.weapon_type);
      auto is_active{false};

      for (auto i = 0u; i < settings::Combat::Legitbot::kGroupCount; ++i) {
        if (&settings::g_combat.m_legitbot.groups[i] == active_group) {
          const auto& g = settings::g_combat.m_legitbot.groups[i];
          if (setting == &g.aimbot || setting == &g.rcs ||
              setting == &g.standalone_rcs || setting == &g.triggerbot ||
              setting == &g.autowall || setting == &g.visualize_fov ||
              setting == &g.trigger_head_only ||
              setting == &g.give_me_your_seed) {
            is_active = true;
          }

          if (is_active && setting == &active_group->give_me_your_seed &&
              !active_group->triggerbot.value) {
            is_active = false;
          }
          break;
        }
      }

      if (!is_active) {
        continue;
      }

      auto& e = entries[count++];
      e.name = setting->name.c_str();
      e.mode = setting->bind.mode;
      e.value[0] = '\0';
      e.has_value_pill = false;
      continue;
    }

    if (setting == &settings::g_combat.m_antiaim.enabled ||
        setting == &settings::g_combat.m_antiaim.manual_left ||
        setting == &settings::g_combat.m_antiaim.manual_right ||
        setting == &settings::g_combat.m_antiaim.hide_shots ||
        setting == &settings::g_combat.m_antiaim.avoid_backstab ||
        setting == &settings::g_combat.m_antiaim.direction_indicator) {
      if (!settings::g_combat.m_antiaim.enabled.value) {
        continue;
      }
    }

    auto& e = entries[count++];
    e.name = setting->name.c_str();
    e.mode = setting->bind.mode;
    e.value[0] = '\0';
    e.has_value_pill = false;
  }

  if (count > 0)
    container_alpha.FadeIn(0.2f);
  else
    container_alpha.FadeOut(0.2f);

  container_alpha.Update();
  if (!container_alpha.Visible()) return;

  const auto master_alpha = container_alpha.Alpha();
  const auto total_h = kHeaderH + kRowSpacing +
                       (static_cast<float>(count) * (kRowH + kRowSpacing));
  const auto target_base_y =
      (static_cast<float>(screen_h) * 0.5f) - (total_h * 0.5f);

  smoothed_base_y.SetTarget(target_base_y);
  smoothed_base_y.Update();

  const auto base_ry = smoothed_base_y.Value();
  const auto x = kMargin;

  static auto icon_w_px = 0, icon_h_px = 0;
  static const auto kb_icon = xdraw::load_svg(
      R"(<svg width="12" height="12" viewBox="0 0 12 12" fill="none" xmlns="http://www.w3.org/2000/svg"><path d="M2.78571 4.07143C2.53142 4.07143 2.28285 3.99602 2.07141 3.85475C1.85998 3.71347 1.69518 3.51267 1.59787 3.27774C1.50056 3.0428 1.4751 2.78429 1.52471 2.53488C1.57431 2.28548 1.69677 2.05639 1.87658 1.87658C2.05639 1.69677 2.28548 1.57431 2.53488 1.52471C2.78429 1.4751 3.0428 1.50056 3.27774 1.59787C3.51267 1.69518 3.71347 1.85998 3.85475 2.07141C3.99602 2.28285 4.07143 2.53142 4.07143 2.78571V9.21429C4.07143 9.46858 3.99602 9.71716 3.85475 9.92859C3.71347 10.14 3.51267 10.3048 3.27774 10.4021C3.0428 10.4994 2.78429 10.5249 2.53488 10.4753C2.28548 10.4257 2.05639 10.3032 1.87658 10.1234C1.69677 9.94361 1.57431 9.71452 1.52471 9.46512C1.4751 9.21571 1.50056 8.9572 1.59787 8.72226C1.69518 8.48733 1.85998 8.28653 2.07141 8.14525C2.28285 8.00398 2.53142 7.92857 2.78571 7.92857H9.21429C9.46858 7.92857 9.71716 8.00398 9.92859 8.14525C10.14 8.28653 10.3048 8.48733 10.4021 8.72226C10.4994 8.9572 10.5249 9.21571 10.4753 9.46512C10.4257 9.71452 10.3032 9.94361 10.1234 10.1234C9.94361 10.3032 9.71452 10.4257 9.46512 10.4753C9.21571 10.5249 8.9572 10.4994 8.72226 10.4021C8.48733 10.3048 8.28653 10.14 8.14525 9.92859C8.00398 9.71716 7.92857 9.46858 7.92857 9.21429V2.78571C7.92857 2.53142 8.00398 2.28285 8.14525 2.07141C8.28653 1.85998 8.48733 1.69518 8.72226 1.59787C8.9572 1.50056 9.21571 1.4751 9.46512 1.52471C9.71452 1.57431 9.94361 1.69677 10.1234 1.87658C10.3032 2.05639 10.4257 2.28548 10.4753 2.53488C10.5249 2.78429 10.4994 3.0428 10.4021 3.27774C10.3048 3.51267 10.14 3.71347 9.92859 3.85475C9.71716 3.99602 9.46858 4.07143 9.21429 4.07143H2.78571Z" stroke="#FFFFFF" stroke-linecap="round" stroke-linejoin="round"/></svg>)", 1.0f,
      &icon_w_px, &icon_h_px);

  const auto [header_tw, header_th] = xdraw::measure_text("keybinds");
  const auto header_w = kInnerPad + kIconSize + kInnerPad + header_tw +
                        kTextPadX * 2.0f + kInnerPad;
  const auto header_inner_h = kHeaderH - kInnerPad * 2.0f;
  const auto inner_h = kRowH - kInnerPad * 2.0f;
  const auto master_u8 = static_cast<std::uint8_t>(255.0f * master_alpha);

  draw_list.rect_filled_blurred(x, base_ry, header_w, kHeaderH,
                                xdraw::corner_radius{kR},
                                xdraw::color{255, 255, 255, master_u8});
  draw_list.rect_filled(x, base_ry, header_w, kHeaderH,
                        s.window_bg.alpha(static_cast<std::uint8_t>(
                            s.window_bg.a * master_alpha)),
                        xdraw::corner_radius{kR});
  draw_list.rect_filled(
      x + kInnerPad, base_ry + kInnerPad, kIconSize, header_inner_h,
      s.accent.alpha(static_cast<std::uint8_t>(s.accent.a * master_alpha)),
      xdraw::corner_radius{kInnerR});

  if (kb_icon) {
    const auto icon_draw = kIconSize - kIconInnerPad * 2.0f;
    const auto ix = std::floor(x + kInnerPad + kIconInnerPad);
    const auto iy = std::floor(base_ry + kInnerPad +
                               (header_inner_h - icon_draw) * 0.5f + 1.0f);
    draw_list.image(ix, iy, icon_draw, icon_draw, kb_icon.Get(),
                    s.checkbox_mark_icon.alpha(static_cast<std::uint8_t>(
                        s.checkbox_mark_icon.a * master_alpha)));
  }

  const auto htx = x + kInnerPad + kIconSize + kInnerPad;
  const auto htw = header_tw + kTextPadX * 2.0f;
  draw_list.rect_filled(
      htx, base_ry + kInnerPad, htw, header_inner_h,
      s.child_bg.alpha(static_cast<std::uint8_t>(s.child_bg.a * master_alpha)),
      xdraw::corner_radius{kInnerR});
  draw_list.text(
      htx + kTextPadX, base_ry + (kHeaderH - header_th) * 0.5f + kTextNudge,
      "keybinds",
      s.accent.alpha(static_cast<std::uint8_t>(s.accent.a * master_alpha)));

  for (auto& [name, state] : row_states) state.active_this_frame = false;

  float current_offset_y = kHeaderH + kRowSpacing;
  for (auto i = 0; i < count; ++i) {
    const auto& e = entries[i];
    auto& anim = row_states[e.name];

    if (!anim.active_this_frame && anim.alpha.Alpha() <= 0.01f)
      anim.offset_y.Snap(current_offset_y);

    anim.active_this_frame = true;
    anim.alpha.FadeIn(0.2f);
    anim.offset_y.SetTarget(current_offset_y);
    anim.alpha.Update();
    anim.offset_y.Update();

    const auto row_alpha = anim.alpha.Alpha() * master_alpha;
    const auto draw_y = base_ry + anim.offset_y.Value();
    const auto [nw, nh] = xdraw::measure_text(e.name);
    const auto row_u8 = static_cast<std::uint8_t>(255.0f * row_alpha);

    if (e.has_value_pill) {
      const auto [vw, vh] = xdraw::measure_text(e.value);
      const auto name_pill_w = nw + kTextPadX * 2.0f;
      const auto value_pill_w = vw + kTextPadX * 2.0f;
      const auto row_w =
          kInnerPad + name_pill_w + kInnerPad + value_pill_w + kInnerPad;

      draw_list.rect_filled_blurred(x, draw_y, row_w, kRowH,
                                    xdraw::corner_radius{kR},
                                    xdraw::color{255, 255, 255, row_u8});
      draw_list.rect_filled(x, draw_y, row_w, kRowH,
                            s.window_bg.alpha(static_cast<std::uint8_t>(
                                s.window_bg.a * row_alpha)),
                            xdraw::corner_radius{kR});
      draw_list.rect_filled(
          x + kInnerPad, draw_y + kInnerPad, name_pill_w, inner_h,
          s.child_bg.alpha(static_cast<std::uint8_t>(s.child_bg.a * row_alpha)),
          xdraw::corner_radius{kInnerR});
      draw_list.text(
          x + kInnerPad + kTextPadX, draw_y + (kRowH - nh) * 0.5f + kTextNudge,
          e.name,
          s.accent.alpha(static_cast<std::uint8_t>(s.accent.a * row_alpha)));

      const auto vpx = x + kInnerPad + name_pill_w + kInnerPad;
      draw_list.rect_filled(
          vpx, draw_y + kInnerPad, value_pill_w, inner_h,
          s.accent.alpha(static_cast<std::uint8_t>(s.accent.a * row_alpha)),
          xdraw::corner_radius{kInnerR});
      draw_list.text(
          vpx + kTextPadX, draw_y + (kRowH - vh) * 0.5f + kTextNudge, e.value,
          s.checkbox_mark_icon.alpha(
              static_cast<std::uint8_t>(s.checkbox_mark_icon.a * row_alpha)));
    } else {
      const auto name_pill_w = nw + kTextPadX * 2.0f;
      const auto row_w = kInnerPad + name_pill_w + kInnerPad;
      const auto text_col =
          (e.mode == xui::bind_mode::toggle) ? s.text_dim : s.accent;

      draw_list.rect_filled_blurred(x, draw_y, row_w, kRowH,
                                    xdraw::corner_radius{kR},
                                    xdraw::color{255, 255, 255, row_u8});
      draw_list.rect_filled(x, draw_y, row_w, kRowH,
                            s.window_bg.alpha(static_cast<std::uint8_t>(
                                s.window_bg.a * row_alpha)),
                            xdraw::corner_radius{kR});
      draw_list.rect_filled(
          x + kInnerPad, draw_y + kInnerPad, name_pill_w, inner_h,
          s.child_bg.alpha(static_cast<std::uint8_t>(s.child_bg.a * row_alpha)),
          xdraw::corner_radius{kInnerR});
      draw_list.text(
          x + kInnerPad + kTextPadX, draw_y + (kRowH - nh) * 0.5f + kTextNudge,
          e.name,
          text_col.alpha(static_cast<std::uint8_t>(text_col.a * row_alpha)));
    }

    current_offset_y += kRowH + kRowSpacing;
  }

  for (auto it = row_states.begin(); it != row_states.end();) {
    if (!it->second.active_this_frame) {
      it->second.alpha.FadeOut(0.15f);
      it->second.alpha.Update();
      it->second.offset_y.Update();

      if (it->second.alpha.Alpha() <= 0.001f) {
        it = row_states.erase(it);
        continue;
      }

      const auto row_alpha = it->second.alpha.Alpha() * master_alpha;
      const auto row_u8 = static_cast<std::uint8_t>(255.0f * row_alpha);
      const auto draw_y = base_ry + it->second.offset_y.Value();
      const auto [nw, nh] = xdraw::measure_text(it->first.c_str());
      const auto row_w = kInnerPad + (nw + kTextPadX * 2.0f) + kInnerPad;

      draw_list.rect_filled_blurred(x, draw_y, row_w, kRowH,
                                    xdraw::corner_radius{kR},
                                    xdraw::color{255, 255, 255, row_u8});
      draw_list.rect_filled(x, draw_y, row_w, kRowH,
                            s.window_bg.alpha(static_cast<std::uint8_t>(
                                s.window_bg.a * row_alpha)),
                            xdraw::corner_radius{kR});
    }
    ++it;
  }
}

}  // namespace rendering
