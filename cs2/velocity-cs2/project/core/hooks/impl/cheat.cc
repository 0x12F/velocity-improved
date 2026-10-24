module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>
#include <external/stackwalker/stackwalker.hpp>
#include <external/xdraw/xdraw.hpp>

#include "core/systems/schema_macros.h"
#include "protection/address_macros.h"

module velocity;

import <cstddef>;
import <filesystem>;
import <initializer_list>;
import <limits>;
import <string_view>;
import <thread>;
import <type_traits>;
import <utility>;

namespace hooks {

namespace {
std::atomic<int> forced_server_region{
    features::misc::server_regions::kDisabled};
enum class PingReportStatus {
  kWaiting,
  kApplied,
  kMalformed,
  kNoCenters,
  kSendFailed
};
std::atomic<PingReportStatus> last_ping_report_status{
    PingReportStatus::kWaiting};
}  // namespace

bool Cheat::Initialize() {
  if (!hooking::manager::Create(
          {{&m_present_, &Present, xs("present"),
            addresses::functions::present},
           {&m_resize_buffers_, &ResizeBuffers, xs("resize_buffers"),
            addresses::functions::resize_buffers}})) {
    return false;
  }

  const hooking::manager::Entry feature_hooks[]{
      {&m_cmd_interpreter_, &CmdInterpreter, xs("cmd_interpreter"),
       PATTERN(patterns::cmd_interpreter)},
      {&m_frame_stage_notify_, &FrameStageNotify, xs("frame_stage_notify"),
       PATTERN(patterns::frame_stage_notify)},
      {&m_create_move_, &CreateMove, xs("create_move"),
       PATTERN(patterns::create_move)},
      {&m_handle_view_angles_, &HandleViewAngles, xs("handle_view_angles"),
       PATTERN(patterns::handle_view_angles)},
      {&m_add_entity_, &AddEntity, xs("add_entity"),
       PATTERN(patterns::add_entity)},
      {&m_remove_entity_, &RemoveEntity, xs("remove_entity"),
       PATTERN(patterns::remove_entity)},
      {&m_render_view_, &RenderView, xs("render_view"),
       PATTERN(patterns::render_view)},
      {&m_draw_skybox_array_, &DrawSkyboxArray, xs("draw_skybox_array"),
       PATTERN(patterns::draw_skybox_array)},
      {&m_light_scene_object_, &LightSceneObject, xs("light_scene_object"),
       PATTERN(patterns::light_scene_object)},
      {&m_draw_scene_object_array_, &DrawSceneObjectArray,
       xs("draw_scene_object_array"),
       PATTERN(patterns::draw_scene_object_array)},
      {&m_draw_scene_object_, &DrawSceneObject, xs("draw_scene_object"),
       PATTERN(patterns::draw_scene_object)},
      {&m_is_glowing_, &IsGlowing, xs("is_glowing"),
       PATTERN(patterns::is_glowing)},
      {&m_get_glow_color_, &GetGlowColor, xs("get_glow_color"),
       PATTERN(patterns::get_glow_color)},
      {&m_generate_primitives_, &GeneratePrimitives, xs("generate_primitives"),
       PATTERN(patterns::generate_primitives)},
      {&m_parse_report_hit_, &ParseReportHit, xs("parse_report_hit"),
       PATTERN(patterns::parse_report_hit)},
      {&m_setup_fog_, &SetupFog, xs("setup_fog"), PATTERN(patterns::setup_fog)},
      {&m_set_shader_param_, &SetShaderParam, xs("set_shader_param"),
       PATTERN(patterns::set_shader_param)},
      {&m_set_postprocess_vec_, &SetPostprocessVec, xs("set_postprocess_vec"),
       PATTERN(patterns::set_postprocess_vec)},
      {&m_override_view_, &OverrideView, xs("override_view"),
       PATTERN(patterns::override_view)},
      {&m_update_fov_sensitivity_, &UpdateFovSensitivity,
       xs("update_fov_sensitivity"), PATTERN(patterns::update_fov_sensitivity)},
      {&m_render_scope_, &RenderScope, xs("render_scope"),
       PATTERN(patterns::render_scope)},
      {&m_render_crosshair_, &RenderCrosshair, xs("render_crosshair"),
       PATTERN(patterns::render_crosshair)},
      {&m_prepare_scene_material_, &PrepareSceneMaterial,
       xs("prepare_scene_material"), PATTERN(patterns::prepare_scene_material)},
      {&m_post_network_data_received_, &PostNetworkDataReceived,
       xs("post_network_data_received"),
       PATTERN(patterns::post_network_data_received)},
      {&m_draw_overhead_, &DrawOverhead, xs("draw_overhead"),
       PATTERN(patterns::draw_overhead)},
      {&m_draw_legs_, &DrawLegs, xs("draw_legs"), PATTERN(patterns::draw_legs)},
      {&m_get_transforms_for_hitbox_list_, &GetTransformsForHitboxList,
       xs("get_transforms_for_hitbox_list"),
       PATTERN(patterns::get_transforms_for_hitbox_list)},
      {&m_sort_primitives_, &SortPrimitives, xs("sort_primitives"),
       PATTERN(patterns::sort_primitives)},
      {&m_get_interpolated_shoot_position_, &GetInterpolatedShootPosition,
       xs("get_interpolated_shoot_position"),
       PATTERN(patterns::get_interpolated_shoot_position)},
      {&m_level_initialization_, &LevelInitialization,
       xs("level_initialization"), PATTERN(patterns::level_initialization)},
      {&m_level_shutdown_, &LevelShutdown, xs("level_shutdown"),
       PATTERN(patterns::level_shutdown)},
      {&m_read_frame_input_, &ReadFrameInput, xs("read_frame_input"),
       PATTERN(patterns::read_frame_input)},
      {&m_process_input_event_, &ProcessInputEvent, xs("process_input_event"),
       PATTERN(patterns::process_input_event)},
      {&m_render_decals_, &RenderDecals, xs("render_decals"),
       PATTERN(patterns::render_decals)},
      {&m_render_smoke_, &RenderSmoke, xs("render_smoke"),
       PATTERN(patterns::render_smoke)},
      {&m_draw_flash_effect_, &DrawFlashEffect, xs("draw_flash_effect"),
       PATTERN(patterns::draw_flash_effect)},
      {&m_set_info_, &SetInfo, xs("set_info"), PATTERN(patterns::set_info)}};

  auto unavailable_hooks = 0u;
  for (const auto& entry : feature_hooks) {
    if (!hooking::manager::Create({entry})) {
      ++unavailable_hooks;
      logging::console::Print(xs("skipping unavailable hook: {}"), entry.name);
    }
  }

  if (unavailable_hooks) {
    logging::console::Print(xs("feature hooks initialized with {} unavailable"),
                            unavailable_hooks);
  }

  auto get_networking_utils =
      MODULE_EXPORT("steamnetworkingsockets.dll:SteamNetworkingUtils_LibV4");
  if (!get_networking_utils)
    get_networking_utils = MODULE_EXPORT(
        "steam_api64.dll:SteamAPI_SteamNetworkingUtils_SteamAPI_v004");
  const auto networking_utils =
      get_networking_utils ? memory::Call<std::uintptr_t>(get_networking_utils)
                           : 0;
  // ISteamNetworkingUtils004::GetPingToDataCenter, from Valve's public
  // interface.
  constexpr auto kGetPingToDataCenterIndex{9};
  if (!networking_utils ||
      !hooking::manager::Create(
          {{&m_get_ping_to_data_center_,
            reinterpret_cast<void*>(&GetPingToDataCenter),
            xs("get_ping_to_data_center"),
            memory::GetVfunc(networking_utils, kGetPingToDataCenterIndex)}})) {
    logging::console::Print(
        xs("force server: live ping hook unavailable; using outgoing report "
           "filtering"));
  }

  const auto get_steam_client = MODULE_EXPORT("steam_api64.dll:SteamClient");
  const auto get_steam_user =
      MODULE_EXPORT("steam_api64.dll:SteamAPI_GetHSteamUser");
  const auto get_steam_pipe =
      MODULE_EXPORT("steam_api64.dll:SteamAPI_GetHSteamPipe");
  const auto get_generic_interface = MODULE_EXPORT(
      "steam_api64.dll:SteamAPI_ISteamClient_GetISteamGenericInterface");
  std::uintptr_t coordinator{};
  if (get_steam_client && get_steam_user && get_steam_pipe &&
      get_generic_interface) {
    const auto client = memory::Call<std::uintptr_t>(get_steam_client);
    if (client)
      coordinator = memory::Call<std::uintptr_t>(
          get_generic_interface, client, memory::Call<int>(get_steam_user),
          memory::Call<int>(get_steam_pipe), xs("SteamGameCoordinator001"));
  }
  constexpr auto kSendMessageIndex{0};
  if (!coordinator ||
      !hooking::manager::Create(
          {{&m_send_gc_message_, reinterpret_cast<void*>(&SendGcMessage),
            xs("force_server_ping_report"),
            memory::GetVfunc(coordinator, kSendMessageIndex)}}))
    logging::console::Print(
        xs("force server unavailable: matchmaking report hook could not be "
           "initialized"));

  return true;
}

void Cheat::Shutdown() {
  forced_server_region.store(features::misc::server_regions::kDisabled,
                             std::memory_order_relaxed);
  m_send_gc_message_.Reset();
  m_get_ping_to_data_center_.Reset();
  m_wnd_proc_.Reset();
  m_om_set_render_targets_.Reset();
  m_present_.Reset();
  m_resize_buffers_.Reset();
  m_cmd_interpreter_.Reset();
  m_frame_stage_notify_.Reset();
  m_create_move_.Reset();
  m_handle_view_angles_.Reset();
  m_add_entity_.Reset();
  m_remove_entity_.Reset();
  m_render_view_.Reset();
  m_draw_skybox_array_.Reset();
  m_light_scene_object_.Reset();
  m_draw_scene_object_array_.Reset();
  m_draw_scene_object_.Reset();
  m_is_glowing_.Reset();
  m_get_glow_color_.Reset();
  m_generate_primitives_.Reset();
  m_parse_report_hit_.Reset();
  m_setup_fog_.Reset();
  m_set_shader_param_.Reset();
  m_set_postprocess_vec_.Reset();
  m_override_view_.Reset();
  m_update_fov_sensitivity_.Reset();
  m_render_scope_.Reset();
  m_render_crosshair_.Reset();
  m_prepare_scene_material_.Reset();
  m_post_network_data_received_.Reset();
  m_draw_overhead_.Reset();
  m_draw_legs_.Reset();
  m_get_transforms_for_hitbox_list_.Reset();
  m_sort_primitives_.Reset();
  m_get_inaccuracy_.Reset();
  m_get_interpolated_shoot_position_.Reset();
  m_level_initialization_.Reset();
  m_read_frame_input_.Reset();
  m_process_input_event_.Reset();
  m_render_decals_.Reset();
  m_render_smoke_.Reset();
  m_render_smoke_map_.Reset();
  m_render_smoke_unmap_.Reset();
  m_draw_flash_effect_.Reset();
  m_set_info_.Reset();
}

HRESULT __fastcall Cheat::Present(IDXGISwapChain* thisptr, UINT sync_interval,
                                  UINT flags) {
  rendering::g_context.OnPresent(thisptr);

  const auto& force_server = settings::g_misc.m_force_server;
  const auto selected_region = force_server.enabled.value
                                   ? force_server.region.value
                                   : features::misc::server_regions::kDisabled;
  if (forced_server_region.exchange(
          selected_region, std::memory_order_relaxed) != selected_region)
    last_ping_report_status.store(PingReportStatus::kWaiting,
                                  std::memory_order_relaxed);

  if (!m_wnd_proc_.IsEnabled() && rendering::g_context.GetWindow()) {
    if (m_wnd_proc_.Create(reinterpret_cast<void*>(GetWindowLongPtrW(
                               rendering::g_context.GetWindow(), GWLP_WNDPROC)),
                           &WndProc)) {
      m_wnd_proc_.Enable();
    }
  }

  if (!m_om_set_render_targets_.IsEnabled() &&
      rendering::g_context.IsInitialized()) {
    const auto om_addr = memory::GetVfunc(
        reinterpret_cast<std::uintptr_t>(rendering::g_context.GetContext()),
        33);
    if (m_om_set_render_targets_.Create(reinterpret_cast<void*>(om_addr),
                                        &OmSetRenderTargets)) {
      m_om_set_render_targets_.Enable();
    }
  }

  return m_present_.Call<HRESULT>(thisptr, sync_interval, flags);
}

int __fastcall Cheat::GetPingToDataCenter(std::uintptr_t thisptr,
                                          std::uint32_t pop,
                                          std::uint32_t* via_relay) {
  const auto measured_ping =
      m_get_ping_to_data_center_.Call<int>(thisptr, pop, via_relay);
  return features::misc::server_regions::FilterPing(
      forced_server_region.load(std::memory_order_relaxed), pop, measured_ping);
}

const char* Cheat::ForceServerStatus() {
  if (!ForceServerAvailable()) return "Unavailable: matchmaking report hook.";
  switch (last_ping_report_status.load(std::memory_order_relaxed)) {
    case PingReportStatus::kApplied:
      return "Region applied to matchmaking report.";
    case PingReportStatus::kMalformed:
      return "Report blocked: unsupported format.";
    case PingReportStatus::kNoCenters:
      return "Report blocked: selected region absent.";
    case PingReportStatus::kSendFailed:
      return "Steam rejected the ping report. Retry.";
    default:
      return "Waiting for matchmaking ping report.";
  }
}

int __fastcall Cheat::SendGcMessage(std::uintptr_t thisptr, std::uint32_t type,
                                    const void* data, std::uint32_t size) {
  namespace report = features::misc::server_ping_report;
  const auto region = forced_server_region.load(std::memory_order_relaxed);
  if (type != (report::kMessageType | report::kProtobufFlag) ||
      !features::misc::server_regions::Valid(region))
    return m_send_gc_message_.Call<int>(thisptr, type, data, size);

  std::vector<std::uint8_t> rewritten;
  const auto result =
      data ? report::Rewrite({static_cast<const std::uint8_t*>(data), size},
                             region, rewritten)
           : report::Result::kMalformed;
  if (result != report::Result::kRewritten) {
    const auto status = result == report::Result::kNoSelectedCenters
                            ? PingReportStatus::kNoCenters
                            : PingReportStatus::kMalformed;
    last_ping_report_status.store(status, std::memory_order_relaxed);
    logging::console::Print(xs("force server: blocked ping report for {} ({})"),
                            features::misc::server_regions::kNames[region],
                            ForceServerStatus());
    constexpr auto kInvalidMessage{4};  // k_EGCResultInvalidMessage
    return kInvalidMessage;
  }

  const auto sent = m_send_gc_message_.Call<int>(
      thisptr, type, static_cast<const void*>(rewritten.data()),
      static_cast<std::uint32_t>(rewritten.size()));
  last_ping_report_status.store(
      sent == 0 ? PingReportStatus::kApplied : PingReportStatus::kSendFailed,
      std::memory_order_relaxed);
  logging::console::Print(
      xs("force server: {} matchmaking ping report, Steam result {}"),
      features::misc::server_regions::kNames[region], sent);
  return sent;
}

HRESULT __fastcall Cheat::ResizeBuffers(IDXGISwapChain* thisptr,
                                        UINT buffer_count, UINT width,
                                        UINT height, DXGI_FORMAT new_format,
                                        UINT swap_chain_flags) {
  rendering::g_context.OnResizeBuffers();

  const auto result = m_resize_buffers_.Call<long>(
      thisptr, buffer_count, width, height, new_format, swap_chain_flags);
  if (SUCCEEDED(result)) {
    rendering::g_context.OnResizeBuffersPost(thisptr);
  }

  return result;
}

LRESULT __stdcall Cheat::WndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                 LPARAM lparam) {
  if (msg == WM_ACTIVATE && LOWORD(wparam) != WA_INACTIVE &&
      rendering::g_menu.IsOpen() && rendering::g_context.GetWindow() == hwnd) {
    if (addresses::globals::input_system) {
      memory::CallVfunc<void>(addresses::globals::input_system, 76, false);
    }

    SetCursor(LoadCursor(nullptr, IDC_ARROW));
    rendering::g_menu.ApplySavedCursor();
  }

  if (msg == WM_KEYDOWN && !(lparam & (1 << 30)) &&
      static_cast<int>(wparam) == settings::g_misc.menu_key) {
    rendering::g_menu.Toggle();
    return 0;
  }

  xui::wndproc(msg, wparam, lparam);

  if (rendering::g_menu.IsOpen()) {
    switch (msg) {
      case WM_LBUTTONDOWN:
      case WM_LBUTTONUP:
      case WM_LBUTTONDBLCLK:
      case WM_RBUTTONDOWN:
      case WM_RBUTTONUP:
      case WM_RBUTTONDBLCLK:
      case WM_MBUTTONDOWN:
      case WM_MBUTTONUP:
      case WM_MBUTTONDBLCLK:
      case WM_MOUSEWHEEL:
      case WM_MOUSEHWHEEL:
      case WM_MOUSEMOVE:
        return 0;
      default:
        break;
    }
  }

  return m_wnd_proc_.Call<LRESULT>(hwnd, msg, wparam, lparam);
}

void __stdcall Cheat::OmSetRenderTargets(ID3D11DeviceContext* ctx,
                                         UINT num_views,
                                         ID3D11RenderTargetView* const* rtvs,
                                         ID3D11DepthStencilView* dsv) {
  m_om_set_render_targets_.Call<void>(ctx, num_views, rtvs, dsv);
}

void __fastcall Cheat::CmdInterpreter(std::uintptr_t render_thread,
                                      std::uintptr_t item, std::uint8_t flag) {
  m_cmd_interpreter_.Call<void>(render_thread, item, flag);
}

void __fastcall Cheat::FrameStageNotify(std::uintptr_t thisptr, int stage) {
  if (systems::g_entities.IsEmpty()) {
    systems::g_entities.ForceUpdate();
  }

  systems::g_local.Update();

  if (systems::g_local.Get().IsValid() && systems::g_view.HasCamera()) {
    if (stage == 7) {
      features::world::g_scene.OnFrameStageNotify();
      features::world::g_weather.OnFrameStageNotify();
      features::misc::g_other.OnFrameStageNotify();
      features::misc::g_impacts.OnFrameStageNotify();
    }
  }

  {
    static auto was_active{false};
    const auto is_active = settings::g_misc.m_removals.skybox_3d.value;

    if (is_active != was_active) {
      CONVAR("r_draw3dskybox")->m_value.i1 = is_active;
      was_active = is_active;
    }
  }

  // Source 2 copies dynamic-light entries into scene objects during this stage.
  // Publish our entry first, while keeping all manager mutations on the game
  // thread.
  if (stage == 6) {
    features::misc::g_dlight.OnFrameStageNotify();
  }

  features::changer::runtime::BeforeFrameStage(stage);
  m_frame_stage_notify_.Call<void>(thisptr, stage);
  features::changer::runtime::AfterFrameStage(stage);

  // The current frame's world-to-projection matrix is published by the
  // engine during render-start stage 12.
  if (stage == 12) {
    systems::g_view.UpdateMatrix();
    systems::g_frame_data.Update();
  }

  if (systems::g_local.Get().IsValid() && systems::g_view.HasCamera()) {
    if (stage == 6) {
      // Capture lag records only after Source 2 has committed this network
      // update, so the simulation timestamp, world origin and evaluated bones
      // agree.
      features::combat::g_shared.Lc().Run();
      features::esp::player::g_chams.Bt().Update();
      features::esp::player::g_chams.Os().Update();

      features::misc::g_scoreboard_weapons.OnFrameStageNotify();
      features::misc::g_other.DoKillFeedPreservation();
    }
  }
}

void __fastcall Cheat::CreateMove(std::uintptr_t thisptr, int slot,
                                  bool active) {
  const auto local = systems::g_local.Get();

  if (!local.pawn || !local.controller) {
    return m_create_move_.Call<void>(thisptr, slot, active);
  }

  const auto cmd = systems::g_input.GetCurrentCmd(local.controller);
  if (cmd && systems::g_input.IsSubtickOverwrite(cmd)) {
    systems::g_input.SetWeaponSelect(cmd, thisptr);
    return;
  }

  m_create_move_.Call<void>(thisptr, slot, active);

  {
    features::combat::g_shared.InvalidateIfNeeded();
    features::combat::g_legit.InvalidateIfNeeded();
    features::combat::g_misc.Quickpeek().ResetIfNeeded();
  }

  if (!local.is_alive || !systems::g_view.HasCamera()) {
    return;
  }

  const auto movement_services = memory::Read<std::uintptr_t>(
      local.pawn + SCHEMA("C_BasePlayerPawn", "m_pMovementServices"_hash));
  if (!movement_services) {
    return;
  }

  const auto last_cmd_processed = memory::Read<std::uint32_t>(
      movement_services +
      SCHEMA("CPlayer_MovementServices", "m_nLastCommandNumberProcessed"_hash));
  if (!last_cmd_processed) {
    return;
  }

  systems::g_input.Update();
  {
    const auto current_cmd = systems::g_input.Get();
    if (!current_cmd || !current_cmd->csgo_user_cmd.HasBase()) {
      return;
    }

    static std::atomic_bool first_create_move_traced{};
    const auto trace =
        !first_create_move_traced.exchange(true, std::memory_order_relaxed);
    diag::ExceptionScope exception_scope{"create_move: desubtick"};
    if (trace) {
      diag::Step("create_move: feature pipeline begin");
    }

    systems::g_input.Desubtick(current_cmd);
    systems::g_prediction.CapturePrestate(local.pawn, movement_services);

    {
      diag::SetExceptionPhase("create_move: shared update");
      features::movement::g_airstrafe.StoreAngles();
      features::combat::g_shared.Update();

      diag::SetExceptionPhase("create_move: combat misc");
      features::combat::g_misc.Antiaim().OnCreateMove(current_cmd);
      features::combat::g_misc.Autostop().OnCreateMove(current_cmd);
    }
    if (trace) {
      diag::Step("create_move: shared and combat misc ready");
    }

    {
      diag::SetExceptionPhase("create_move: pre-combat movement");
      features::movement::g_slowwalk.OnCreateMove(current_cmd);
      features::movement::g_edgebug.OnCreateMove(current_cmd);
      features::movement::g_edgejump.OnCreateMove(current_cmd);
      features::movement::g_edgestop.OnCreateMove(current_cmd);
      features::movement::g_jumpbug.OnCreateMove(current_cmd);
      features::movement::g_bhop.OnCreateMove(current_cmd);
      features::movement::g_fastladder.OnCreateMove(current_cmd);
      if (trace) {
        diag::Step("create_move: pre-combat movement end");
        diag::Step("create_move: rage begin");
      }

      {
        diag::SetExceptionPhase("create_move: rage");
        features::combat::g_rage.OnCreateMove(current_cmd);
        if (trace) {
          diag::Step("create_move: rage end");
          diag::Step("create_move: legit begin");
        }
        diag::SetExceptionPhase("create_move: legit");
        features::combat::g_legit.OnCreateMove(current_cmd);
        if (trace) {
          diag::Step("create_move: legit end");
        }
      }

      diag::SetExceptionPhase("create_move: post-combat movement");
      features::combat::g_misc.Duckpeek().OnCreateMove(current_cmd);
      features::movement::g_test_strafer.OnCreateMove(current_cmd);
      features::movement::g_airstrafe.OnCreateMove(current_cmd);
      features::misc::g_projectile_trajectory.OnCreateMove(current_cmd);
    }
    if (trace) {
      diag::Step("create_move: post-combat movement end");
    }

    diag::SetExceptionPhase("create_move: quickpeek");
    features::combat::g_misc.Quickpeek().OnCreateMove(current_cmd);
    if (trace) {
      diag::Step("create_move: quickpeek end");
      diag::Step("create_move: final subtick begin");
    }

    diag::SetExceptionPhase("create_move: final subtick");
    const auto final_base = current_cmd->csgo_user_cmd.MutableBase();
    if (final_base && final_base->SubtickMovesSize() > 0 &&
        !features::movement::g_test_strafer.HandledThisTick()) {
      final_base->SetForwardmove(0.0f);
      final_base->SetLeftmove(0.0f);
    }
    if (trace) {
      diag::Step("create_move: final subtick end");
    }

    // systems::g_legit_input.on_create_move( current_cmd );
  }
  static std::atomic_bool first_input_apply_traced{};
  const auto trace_apply =
      !first_input_apply_traced.exchange(true, std::memory_order_relaxed);
  if (trace_apply) {
    diag::Step("create_move: input apply begin");
  }
  diag::ExceptionScope exception_scope{"create_move: input apply"};
  systems::g_input.Apply();
  if (trace_apply) {
    diag::Step("create_move: input apply end");
  }
}

void __fastcall Cheat::HandleViewAngles(std::uintptr_t thisptr, int a2) {
  const auto view_angles = systems::g_input.GetViewAngles();

  m_handle_view_angles_.Call<void>(thisptr, a2);

  systems::g_input.SetViewAngles(view_angles);
}

void __fastcall Cheat::AddEntity(std::uintptr_t thisptr, std::uintptr_t entity,
                                 std::uint32_t handle) {
  systems::g_entities.OnAddEntity(entity, handle);

  m_add_entity_.Call<void>(thisptr, entity, handle);
}

void __fastcall Cheat::RemoveEntity(std::uintptr_t thisptr,
                                    std::uintptr_t entity,
                                    std::uint32_t handle) {
  systems::g_entities.OnRemoveEntity(entity, handle);

  m_remove_entity_.Call<void>(thisptr, entity, handle);
}

void __fastcall Cheat::RenderView(std::uintptr_t thisptr) {
  m_render_view_.Call<void>(thisptr);

  systems::g_view.Update(thisptr + 0x10);
  systems::g_frame_data.Update();
}

void __fastcall Cheat::DrawSkyboxArray(std::uintptr_t thisptr,
                                       std::uintptr_t a2,
                                       std::uintptr_t mesh_array,
                                       int mesh_count, int a5,
                                       std::uintptr_t a6, std::uintptr_t a7,
                                       std::uintptr_t a8) {
  features::world::g_scene.OnDrawSkyboxArrayPre(mesh_array, mesh_count);

  m_draw_skybox_array_.Call<void>(thisptr, a2, mesh_array, mesh_count, a5, a6,
                                  a7, a8);

  features::world::g_scene.OnDrawSkyboxArrayPost();
}

std::uintptr_t __fastcall Cheat::LightSceneObject(std::uintptr_t thisptr,
                                                  std::uintptr_t object,
                                                  std::uintptr_t a3) {
  features::world::g_scene.OnLightSceneObjectPre(object);
  features::misc::g_dlight.ApplySceneColor(object);

  const auto result =
      m_light_scene_object_.Call<std::uintptr_t>(thisptr, object, a3);

  features::world::g_scene.OnLightSceneObjectPost(object);

  return result;
}

void __fastcall Cheat::DrawSceneObjectArray(std::uintptr_t thisptr,
                                            std::uintptr_t a2,
                                            std::uintptr_t object_array) {
  m_draw_scene_object_array_.Call<void>(thisptr, a2, object_array);

  diag::ExceptionScope exception_scope{"world: aggregate records"};
  features::world::g_scene.OnDrawSceneObjectArray(object_array);
}

std::uintptr_t __fastcall Cheat::DrawSceneObject(
    std::uintptr_t a1, std::uintptr_t a2, std::uintptr_t batch, int batch_count,
    int a5, std::uintptr_t a6, std::uintptr_t a7, std::uintptr_t a8) {
  {
    diag::ExceptionScope exception_scope{"world: primitive tint"};
    features::world::g_scene.OnDrawSceneObject(batch, batch_count);
  }

  return m_draw_scene_object_.Call<std::uintptr_t>(a1, a2, batch, batch_count,
                                                   a5, a6, a7, a8);
}

bool __fastcall Cheat::IsGlowing(std::uintptr_t glow_property) {
  if (glow_property) {
    const auto owner_entity =
        memory::Read<std::uintptr_t>(glow_property + 0x18);
    if (owner_entity) {
      const auto owner_hash =
          fnv1a::RuntimeHash(systems::g_entities.GetSchemaName(owner_entity));
      if (owner_hash) {
        if (features::esp::player::g_glow.OnIsGlowing(owner_entity,
                                                      owner_hash)) {
          return true;
        }

        if (features::esp::item::g_glow.OnIsGlowing(owner_entity, owner_hash)) {
          return true;
        }
      }
    }
  }

  return m_is_glowing_.Call<bool>(glow_property);
}

void __fastcall Cheat::GetGlowColor(std::uintptr_t glow_property,
                                    float* color) {
  if (glow_property) {
    const auto owner_entity =
        memory::Read<std::uintptr_t>(glow_property + 0x18);
    if (owner_entity) {
      const auto owner_hash =
          fnv1a::RuntimeHash(systems::g_entities.GetSchemaName(owner_entity));
      if (owner_hash) {
        if (features::esp::player::g_glow.OnGetGlowColor(owner_entity,
                                                         owner_hash, color)) {
          return;
        }

        if (features::esp::item::g_glow.OnGetGlowColor(owner_entity, owner_hash,
                                                       color)) {
          return;
        }
      }
    }
  }

  m_get_glow_color_.Call<void>(glow_property, color);
}

void __fastcall Cheat::GeneratePrimitives(std::uintptr_t thisptr,
                                          std::uintptr_t scene_object,
                                          std::uintptr_t scene_view,
                                          std::uintptr_t primitive_buffer) {
  diag::ExceptionScope exception_scope{"chams: generate primitives"};

  if (scene_object) {
    if (features::esp::player::g_chams.Bt().IsActive(scene_object)) {
      return;
    }

    if (features::esp::player::g_chams.Os().IsActive(scene_object)) {
      return;
    }

    const auto owner_handle = memory::Read<std::uint32_t>(scene_object + 0xc0);
    if (owner_handle) {
      const auto owner_entity = systems::g_entities.Lookup(owner_handle);
      if (owner_entity) {
        const auto owner_hash =
            fnv1a::RuntimeHash(systems::g_entities.GetSchemaName(owner_entity));
        if (owner_hash) {
          if (features::esp::player::g_chams.OnGeneratePrimitives(
                  owner_entity, owner_hash, scene_object, primitive_buffer,
                  m_generate_primitives_.Original<void(__fastcall*)(
                      std::uintptr_t, std::uintptr_t, std::uintptr_t,
                      std::uintptr_t)>(),
                  thisptr, scene_view)) {
            return;
          }

          if (features::esp::item::g_chams.OnGeneratePrimitives(
                  owner_entity, owner_hash, scene_object, primitive_buffer,
                  m_generate_primitives_.Original<void(__fastcall*)(
                      std::uintptr_t, std::uintptr_t, std::uintptr_t,
                      std::uintptr_t)>(),
                  thisptr, scene_view)) {
            return;
          }
        }
      }
    }
  }

  m_generate_primitives_.Call<void>(thisptr, scene_object, scene_view,
                                    primitive_buffer);
}

std::uintptr_t __fastcall Cheat::ParseReportHit(std::uintptr_t thisptr,
                                                std::uint8_t deleting) {
  // Capture the protobuf fields before the deleting destructor can free them.
  features::misc::g_impacts.OnReportHit(thisptr);

  return m_parse_report_hit_.Call<std::uintptr_t>(thisptr, deleting);
}

std::uintptr_t __fastcall Cheat::SetupFog(__m128i* output, int* mode) {
  if (features::world::g_scene.OnSetupFog(output, mode)) {
    return 0;
  }

  return m_setup_fog_.Call<std::uintptr_t>(output, mode);
}

std::uintptr_t __fastcall Cheat::SetShaderParam(__m128i* map,
                                                std::uint32_t hash,
                                                __m128i* value) {
  features::world::g_scene.OnSetShaderParam(value, hash);

  return m_set_shader_param_.Call<std::uintptr_t>(map, hash, value);
}

std::uintptr_t __fastcall Cheat::SetPostprocessVec(__m128i* map,
                                                   std::uint32_t hash,
                                                   __m128i* value) {
  constexpr std::uint32_t kDofRanges{0x2ACAB07C};

  // The engine only publishes DofRanges when the active camera enables DOF.
  // Insert it alongside any post-process vector, matching Artisan's live path.
  if (settings::g_world.m_scene.dof.value && hash != kDofRanges) {
    __m128i* dof_value{};
    features::world::g_scene.OnSetShaderParam(dof_value, kDofRanges);
    if (dof_value) {
      m_set_postprocess_vec_.Call<std::uintptr_t>(map, kDofRanges, dof_value);
    }
  }

  features::world::g_scene.OnSetShaderParam(value, hash);
  return m_set_postprocess_vec_.Call<std::uintptr_t>(map, hash, value);
}

void __fastcall Cheat::OverrideView(std::uintptr_t thisptr,
                                    std::uintptr_t view_setup) {
  m_override_view_.Call<void>(thisptr, view_setup);

  features::misc::g_camera.OnOverrideView(view_setup);
  features::misc::g_removals.OnOverrideView(view_setup);
  features::combat::g_misc.Duckpeek().OnOverrideView(view_setup);
}

void __fastcall Cheat::UpdateFovSensitivity(std::uintptr_t thisptr) {
  m_update_fov_sensitivity_.Call<void>(thisptr);

  features::misc::g_camera.UpdateFovSensitivity(thisptr);
}

void __fastcall Cheat::RenderScope(std::uintptr_t a1, std::uintptr_t a2) {
  m_render_scope_.Call<void>(a1, a2);

  if (settings::g_misc.m_removals.scope.value) {
    memory::Write<std::uint8_t>(a2 + 4, 0);
  }
}

bool __fastcall Cheat::RenderCrosshair(std::uintptr_t a1) {
  if (settings::g_misc.m_removals.crosshair.value) {
    return false;
  }

  return m_render_crosshair_.Call<bool>(a1);
}

float __fastcall Cheat::PrepareSceneMaterial(std::uintptr_t material, void* a2,
                                             float a3) {
  features::misc::g_removals.OnPrepareSceneMaterial(material);

  return m_prepare_scene_material_.Call<float>(material, a2, a3);
}

void __fastcall Cheat::PostNetworkDataReceived(std::uintptr_t thisptr) {
  m_post_network_data_received_.Call<void>(thisptr);
}

bool __fastcall Cheat::DrawOverhead(std::uintptr_t pawn,
                                    std::uint32_t player_slot) {
  if (settings::g_misc.m_removals.overhead.value &&
      pawn == systems::g_local.Get().pawn) {
    return false;
  }

  return m_draw_overhead_.Call<bool>(pawn, player_slot);
}

std::uintptr_t __fastcall Cheat::DrawLegs(std::uintptr_t a1, std::uintptr_t a2,
                                          std::uintptr_t a3, std::uintptr_t a4,
                                          std::uintptr_t a5) {
  if (settings::g_misc.m_removals.legs.value) {
    return 0;
  }

  return m_draw_legs_.Call<std::uintptr_t>(a1, a2, a3, a4, a5);
}

bool __fastcall Cheat::GetTransformsForHitboxList(std::uintptr_t a1,
                                                  std::uintptr_t a2, int* a3) {
  if (!features::combat::g_shared.Autowalling()) {
    return m_get_transforms_for_hitbox_list_.Call<bool>(a1, a2, a3);
  }

  const auto record = features::combat::g_shared.CurrentAutowallRecord();
  if (!record || !record->valid) {
    return m_get_transforms_for_hitbox_list_.Call<bool>(a1, a2, a3);
  }

  const auto count =
      memory::SafeRead<int>(reinterpret_cast<std::uintptr_t>(a3)).value_or(0);
  const auto shape_array =
      memory::SafeRead<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(a3) + 8)
          .value_or(0);
  const auto entity_bone_cache =
      memory::SafeRead<std::uintptr_t>(a1 + 0x1c0).value_or(0);
  const auto model_handle =
      memory::SafeRead<std::uintptr_t>(a1 + 0x1e0).value_or(0);
  const auto model =
      model_handle ? memory::SafeRead<std::uintptr_t>(model_handle).value_or(0)
                   : 0;

  if (count <= 0 || count > 256 || !shape_array || !entity_bone_cache ||
      !model) {
    return false;
  }

  static const auto get_bone_index = PATTERN(patterns::get_bone_index);
  if (!get_bone_index) {
    return false;
  }

  // The client dereferences every resolved transform without checking the
  // backing cache. Reject a stale scene node instead of faulting in it.
  for (auto i = 0; i < count; ++i) {
    const auto shape_ptr = shape_array + 16ull * i;
    const auto bone_index = memory::Call<int>(get_bone_index, model, shape_ptr);

    if (bone_index >= 0 &&
        (bone_index >= 256 ||
         !memory::SafeRead<systems::Bones::Data>(
             entity_bone_cache + sizeof(systems::Bones::Data) * bone_index))) {
      return false;
    }
  }

  const auto target_scene =
      record->game_scene_node
          ? record->game_scene_node
          : memory::Read<std::uintptr_t>(
                record->pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
  const auto target_bone_cache =
      target_scene
          ? memory::SafeRead<std::uintptr_t>(
                target_scene +
                SCHEMA("CSkeletonInstance", "m_modelState"_hash) + 0x80)
                .value_or(0)
          : 0;
  const auto result = m_get_transforms_for_hitbox_list_.Call<bool>(a1, a2, a3);

  if (!result) {
    return false;
  }

  const auto is_plausible = [](std::uintptr_t ptr) noexcept -> bool {
    return ptr >= 0x10000ull && ptr < 0x00007FFFFFFFFFFFull;
  };

  if (!is_plausible(entity_bone_cache) || !is_plausible(target_bone_cache) ||
      entity_bone_cache != target_bone_cache) {
    return result;
  }

  const auto output_array =
      memory::SafeRead<std::uintptr_t>(a2 + 16).value_or(0);

  if (!output_array || count <= 0) {
    return result;
  }

  for (auto i = 0; i < count; ++i) {
    const auto shape_ptr = shape_array + 16ull * i;
    const auto bone_index = memory::Call<int>(get_bone_index, model, shape_ptr);

    if (bone_index < 0 || bone_index >= record->bone_count) {
      continue;
    }

    const auto dst = output_array + 32ull * i;
    std::memcpy(reinterpret_cast<void*>(dst), &record->bones[bone_index], 32);
  }

  return true;
}

void __fastcall Cheat::SortPrimitives(std::uintptr_t thisptr, std::uintptr_t a2,
                                      std::uintptr_t a3, std::uint32_t a4) {
  m_sort_primitives_.Call<void>(thisptr, a2, a3, a4);

  diag::ExceptionScope exception_scope{"chams: sort primitives"};
  features::esp::player::g_chams.OnSortPrimitives(a3, a4);
}

float __fastcall Cheat::GetInaccuracy(std::uintptr_t thisptr, float* a2,
                                      float* a3) {
  const auto result = m_get_inaccuracy_.Call<float>(thisptr, a2, a3);

#if defined(__clang__) || defined(__GNUC__)
  const auto result_address =
      reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
#else
  const auto result_address =
      reinterpret_cast<std::uintptr_t>(_ReturnAddress());
#endif

  static const auto base_fire_guns_get_inaccuracy =
      PATTERN(patterns::base_fire_guns_get_inaccuracy);
  if (base_fire_guns_get_inaccuracy &&
      result_address > base_fire_guns_get_inaccuracy &&
      result_address < base_fire_guns_get_inaccuracy + 0x600) {
    features::misc::g_impacts.OnBaseFireGunsGetInaccuracy(thisptr, result);
  }

  return result;
}

float* __fastcall Cheat::GetInterpolatedShootPosition(std::uintptr_t thisptr,
                                                      float* out,
                                                      int* tick_frac) {
  const auto result =
      m_get_interpolated_shoot_position_.Call<float*>(thisptr, out, tick_frac);

  // Prediction calls this helper while building the next command as well.
  // Only the call made for an actual rage shot is its authoritative origin.
  if (features::combat::g_rage.IsFiringThisTick()) {
    features::misc::g_impacts.OnGetInterpolatedShootPosition(thisptr, out);
  }

  return result;
}

std::uintptr_t __fastcall Cheat::LevelInitialization(std::uintptr_t a1,
                                                     const char* new_map) {
  if (new_map && new_map[0]) {
    const char* leaf = std::strrchr(new_map, '/');
    rendering::g_widgets.s_map_name_ = leaf ? leaf + 1 : new_map;
  } else {
    rendering::g_widgets.s_map_name_.clear();
  }

  features::world::g_scene.ResetSkyboxState();
  features::misc::g_impacts.OnLevelChange();
  features::misc::g_scoreboard_weapons.OnLevelChange();

  return m_level_initialization_.Call<std::uintptr_t>(a1, new_map);
}

std::uintptr_t __fastcall Cheat::LevelShutdown(std::uintptr_t a1) {
  rendering::g_widgets.s_map_name_.clear();

  // Release feature-owned scene objects before Source 2 tears their parents
  // down.
  features::misc::g_dlight.OnLevelShutdown();

  // clear all local player data on level shutdown
  systems::g_local.Reset();

  return m_level_shutdown_.Call<std::uintptr_t>(a1);
}

void __fastcall Cheat::ReadFrameInput(std::uintptr_t a1, std::uint32_t a2) {
  m_read_frame_input_.Call<void>(a1, a2);
}

void __fastcall Cheat::ProcessInputEvent(std::uintptr_t csgo_input, int slot,
                                         float frametime) {
  m_process_input_event_.Call<void>(csgo_input, slot, frametime);
  // Apply queued aim to the frame the engine just created, not its predecessor.
  systems::g_legit_input.OnProcessInputEvent(csgo_input, slot);
}

std::uintptr_t __fastcall Cheat::RenderDecals(std::uintptr_t render_context,
                                              std::uintptr_t** render_view,
                                              bool pass_flag_a,
                                              bool pass_flag_b) {
  if (settings::g_misc.m_removals.decals.value) {
    return 0;
  }

  return m_render_decals_.Call<std::uintptr_t>(render_context, render_view,
                                               pass_flag_a, pass_flag_b);
}

void __fastcall Cheat::RenderSmoke(std::uintptr_t a1, std::uintptr_t a2, int a3,
                                   int a4, std::uintptr_t a5,
                                   std::uintptr_t a6) {
  static std::once_flag alright;
  std::call_once(alright, [&] {
    if (!a2) {
      return;
    }

    if (m_render_smoke_map_.Create(
            reinterpret_cast<void*>(memory::GetVfunc(a2, 32)),
            &RenderSmokeMap)) {
      m_render_smoke_map_.Enable();
    }

    if (m_render_smoke_unmap_.Create(
            reinterpret_cast<void*>(memory::GetVfunc(a2, 33)),
            &RenderSmokeUnmap)) {
      m_render_smoke_unmap_.Enable();
    }
  });

  if (settings::g_misc.m_removals.smoke.value) {
    return;
  }

  m_render_smoke_.Call<void>(a1, a2, a3, a4, a5, a6);
}

std::uintptr_t __fastcall Cheat::RenderSmokeMap(std::uintptr_t thisptr,
                                                std::size_t size,
                                                std::uintptr_t* out_ptr) {
  const auto result =
      m_render_smoke_map_.Call<std::uintptr_t>(thisptr, size, out_ptr);

  features::world::g_smoke.OnMap(result, size,
                                 out_ptr && *out_ptr ? *out_ptr : 0);

  return result;
}

void __fastcall Cheat::RenderSmokeUnmap(std::uintptr_t thisptr,
                                        std::uintptr_t ctx, std::size_t size) {
  features::world::g_smoke.OnUnmap(ctx);
  m_render_smoke_unmap_.Call<void>(thisptr, ctx, size);
}

char __fastcall Cheat::SetInfo(std::uintptr_t rcx, std::uintptr_t a2) {
  const auto& cfg = settings::g_misc.m_name_changer;
  const auto should_override = cfg.clantag.value || cfg.override_name.value ||
                               features::misc::Other::s_name_change_pending_;
  if (should_override && a2) {
    const auto arg_list =
        memory::SafeRead<std::uintptr_t>(a2 + 0x440).value_or(0);
    const auto key =
        arg_list
            ? memory::SafeRead<const char*>(arg_list + 0x8).value_or(nullptr)
            : nullptr;

    if (key && _stricmp(key, "name") == 0) {
      constexpr std::uint64_t kFcvarProtected = 1ull << 5;
      constexpr std::uint64_t kFcvarUserinfo = 1ull << 9;
      constexpr std::uint64_t kFcvarRegistryRestricted = 1ull << 10;

      if (addresses::globals::cvar) {
        if (const auto name_cvar =
                addresses::globals::cvar->Find("name"_hash)) {
          const auto flags_address =
              reinterpret_cast<std::uintptr_t>(name_cvar) +
              offsetof(CConvar, m_flags);
          if (const auto flags =
                  memory::SafeRead<std::uint64_t>(flags_address)) {
            static_cast<void>(memory::SafeWrite<std::uint64_t>(
                flags_address,
                (*flags | kFcvarUserinfo) &
                    ~(kFcvarProtected | kFcvarRegistryRestricted)));
          }
        }
      }

      const auto& display = features::misc::Other::s_display_name_;
      if (!display.empty()) {
        static_cast<void>(
            memory::SafeWrite<const char*>(arg_list + 0x10, display.c_str()));
      }
    }
  }

  return m_set_info_.Call<char>(rcx, a2);
}

void __fastcall Cheat::DrawFlashEffect(std::uintptr_t a1, int a2,
                                       std::uintptr_t* a3, std::uintptr_t a4,
                                       __m128* a5) {
  if (settings::g_misc.m_removals.flash_alpha.value < 100.0f &&
      settings::g_misc.m_removals.flash_alpha.value != 0.0f) {
    const auto view_pawn = systems::g_local.Get().ViewPawn();
    if (view_pawn) {
      const auto max =
          settings::g_misc.m_removals.flash_alpha.value / 100.0f * 255.0f;
      memory::Write<float>(
          view_pawn + SCHEMA("C_CSPlayerPawnBase", "m_flFlashMaxAlpha"_hash),
          max);
    }
  }

  if (settings::g_misc.m_removals.flash_alpha.value != 0.0f) {
    m_draw_flash_effect_.Call<void>(a1, a2, a3, a4, a5);
  }
}

}  // namespace hooks
