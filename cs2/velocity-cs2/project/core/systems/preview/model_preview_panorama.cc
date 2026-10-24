module;

#include "platform/platform.h"

#include <Windows.h>
#include <d3d11.h>

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>
#include <external/xdraw/xdraw.hpp>

#include "protection/address_macros.h"

module velocity;

import <algorithm>;
import <atomic>;
import <cmath>;
import <cstddef>;
import <cstdint>;
import <cstdio>;
import <cstring>;
import <filesystem>;
import <initializer_list>;
import <limits>;
import <mutex>;
import <string_view>;
import <type_traits>;

namespace {
bool InstallHook(hooking::Jmp& hook, void* target, void* detour) {
  if (hook.Create(target, detour) && hook.Enable()) return true;
  hook.Reset();
  return false;
}

// Panorama panel/resource names.
constexpr const char* kPanelIdPrefix = "velocity_model_preview";
constexpr const char* kTextureNamePrefix = "velocity_model_preview_texture";

// CreateRenderTargets only runs when a target needs realizing. The optional
// AcquireLayerRT hook also lets us reacquire an existing target after
// reinjection.
constexpr std::uint32_t kPreviewPanelWidth = 1024;
constexpr std::uint32_t kPreviewPanelHeight = 768;

// Fallback cleanup if the menu lifecycle does not explicitly close the preview.
constexpr ULONGLONG kRequestTimeoutMs = 5000;

// Keep the old frame visible while the replacement composite finishes building.
constexpr ULONGLONG kSimpleSwapWarmupMs = 170;
constexpr ULONGLONG kComplexSwapWarmupMs = 340;

// Let the old scene retire before its temporary item slot is reused.
constexpr ULONGLONG kSimpleRetireGraceMs = 100;
constexpr ULONGLONG kComplexRetireGraceMs = 220;

struct PanoramaUiPanel;
struct PanoramaUiEngine;

struct PanoramaPanel2D {
  const void* vmt = nullptr;
  PanoramaUiPanel* ui_panel = nullptr;
};

using RunScriptFn = void(__fastcall*)(PanoramaUiEngine*, PanoramaUiPanel*,
                                      const char*, const char*, std::uint64_t);
using CreatePanoramaRenderTargetsFn = void(__fastcall*)(void*);
using AcquirePanoramaLayerRTFn = void*(__fastcall*)(void*, void*);

// Extra arguments are forwarded defensively. On Win64, passing unused register
// arguments is harmless if the current constructor only consumes RCX=this.
using PreviewPlayerConstructorFn = void*(__fastcall*)(void*, void*, void*,
                                                      void*);
using PreviewStickerInModelPanelFn = void(__fastcall*)(void*, const char*, int,
                                                       void*);
using ReleaseCompositeMaterialsFn =
    void(__fastcall*)(features::changer::engine::CompositeMaterialOwner*, bool);

hooking::Jmp g_create_panorama_render_targets_hook{};
hooking::Jmp g_acquire_panorama_layer_rt_hook{};
hooking::Jmp g_preview_player_constructor_hook{};
hooking::Jmp g_preview_sticker_in_model_panel_hook{};

// PreviewStickerInModelPanel exposes the actual CUI_Item3dPanel pointer. We
// keep the latest weapon panel so a 1 -> 0 sticker transition can release the
// stale composite on the currently displayed preview entity before the
// replacement builds.
std::atomic<void*> g_item3d_panel{nullptr};
std::atomic_int g_panel_capture_sticker_slot{-1};
std::atomic_bool g_preview_panel_capture_armed{false};
std::atomic_bool g_zero_sticker_release_pending{false};
std::atomic_int g_last_effective_sticker_count{0};
std::atomic<std::uint16_t> g_last_sticker_definition_index{0};
ReleaseCompositeMaterialsFn g_release_composite_materials = nullptr;

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;

PanoramaUiEngine** g_ui_engine_pointer = nullptr;
PanoramaPanel2D** g_main_menu_panel_pointer = nullptr;
PanoramaPanel2D** g_hud_panel_pointer = nullptr;
RunScriptFn g_run_script = nullptr;

std::atomic_bool g_initialized{false};
std::atomic<systems::model_preview::Status> g_status{
    systems::model_preview::Status::kUninitialized};

std::atomic<std::uint16_t> g_requested_definition_index{0};
std::atomic<systems::model_preview::Presentation> g_requested_presentation{
    systems::model_preview::Presentation::kWeapon};
std::atomic<ULONGLONG> g_last_request_at{0};

// Stable/displayed panel.
PanoramaUiPanel* g_current_context = nullptr;
std::uint16_t g_applied_definition_index = 0;
std::uint64_t g_applied_item_id = 0;
std::uint64_t g_applied_revision = 0;
bool g_applied_complex_cosmetics = false;
systems::model_preview::Presentation g_applied_presentation =
    systems::model_preview::Presentation::kWeapon;

std::uint64_t g_panel_session_id = 0;
char g_current_panel_id[96]{};
std::atomic_bool g_panel_active{false};
std::atomic_bool g_current_panel_parked{false};

// Hidden candidate panel.
PanoramaUiPanel* g_build_context = nullptr;
char g_build_panel_id[96]{};
char g_build_texture_name[96]{};
std::uint16_t g_build_definition_index = 0;
std::uint64_t g_build_item_id = 0;
std::atomic_uint64_t g_build_revision{0};
bool g_build_complex_cosmetics_value = false;
systems::model_preview::Presentation g_build_presentation =
    systems::model_preview::Presentation::kWeapon;
std::atomic_bool g_build_complex_cosmetics{false};
std::atomic_bool g_build_active{false};
std::atomic_bool g_build_promoted{false};

ULONGLONG g_retire_release_at = 0;

// Freeze old pixels before the candidate touches the shared vanity scene.
std::atomic_bool g_freeze_requested{false};
std::atomic_bool g_freeze_complete{false};
std::atomic_bool g_reset_frame_requested{false};

std::atomic_uint32_t g_active_generation{0};

// Raw Source2 ResourceBinding_t*. Panorama owns the strong handle. We never
// mutate its refcount; it is only used as a short pointer chain to the D3D11
// SRV.
std::atomic<void*> g_preview_binding{nullptr};

// Unlike ResourceBinding_t, the published preview SRV is COM-refcounted by us.
std::atomic<ID3D11ShaderResourceView*> g_current_texture{nullptr};
std::atomic_uint32_t g_current_texture_width{0};
std::atomic_uint32_t g_current_texture_height{0};
std::atomic<systems::model_preview::Presentation> g_published_presentation{
    systems::model_preview::Presentation::kWeapon};
std::atomic_bool g_published_presentation_valid{false};

// A newly-created MapItemPreviewPanel can briefly render the stock weapon or an
// empty item while its custom material is being precached/composited. Capture
// the new SRV immediately, but publish it to the menu only after a short
// warm-up.
std::atomic<ID3D11ShaderResourceView*> g_staged_texture{nullptr};
std::atomic_uint32_t g_staged_texture_width{0};
std::atomic_uint32_t g_staged_texture_height{0};
std::atomic<ULONGLONG> g_staged_texture_at{0};
std::atomic_uint32_t g_staged_generation{0};
std::atomic_uint32_t g_promoted_generation{0};

std::atomic<float> g_rotation_x{0.0f};
std::atomic<float> g_rotation_y{0.0f};
std::atomic_bool g_rotation_dirty{false};

// Panorama preview players are client-only and do not appear in the normal
// GameEntity enumeration. Keep only the newest constructor-captured player. Old
// preview players are destroyed/reused when the selected model changes, so
// retaining a ring of historical pointers eventually writes into retired
// objects.
std::atomic<void*> g_current_preview_player{nullptr};
std::atomic<void*> g_current_preview_player_vtable{nullptr};
std::atomic_uint32_t g_current_preview_player_generation{0};
std::atomic<ULONGLONG> g_current_preview_player_captured_at{0};

// The state below is consumed only from the main-thread preview update. A new
// constructor generation resets it before that player is touched.
std::uint32_t g_player_rotation_observed_generation = 0;
void* g_player_rotation_scene_node = nullptr;
float g_player_preview_base_yaw = 0.0f;
bool g_player_preview_base_yaw_valid = false;
float g_player_preview_last_target_yaw = 0.0f;
bool g_player_preview_last_target_yaw_valid = false;

// Wait for Valve to establish the preview player's native facing before
// treating it as the baseline for user rotation.
constexpr ULONGLONG kPlayerRotationMinSettleMs = 220;
constexpr ULONGLONG kPlayerRotationStableMs = 80;
constexpr ULONGLONG kPlayerRotationZeroFallbackMs = 550;
constexpr float kPlayerRotationStableEpsilon = 0.15f;
ULONGLONG g_player_rotation_stable_since = 0;
float g_player_rotation_stable_yaw = 0.0f;
bool g_player_rotation_stable_yaw_valid = false;

float NormalizePreviewYaw(float yaw) {
  while (yaw > 180.0f) yaw -= 360.0f;

  while (yaw < -180.0f) yaw += 360.0f;

  return yaw;
}

struct PlayerSceneNodeRotationData {
  features::changer::engine::SceneNode* scene_node = nullptr;
  float local_yaw = 0.0f;
  float absolute_yaw = 0.0f;
  std::uint8_t dirty = 0;
};

bool TryReadPlayerSceneNodeRotationData(void* entity,
                                        PlayerSceneNodeRotationData& data) {
  data = {};

  __try {
    data.scene_node =
        static_cast<features::changer::engine::BaseEntity*>(entity)
            ->MPGameSceneNode();

    if (!data.scene_node) return false;

    data.local_yaw = data.scene_node->MAngWrappedLocalRotation().y;
    data.absolute_yaw = data.scene_node->MAngAbsRotation().y;
    data.dirty = data.scene_node->TransformDirty();
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    data = {};
    return false;
  }
}

bool TryWritePlayerSceneNodeYaw(
    features::changer::engine::SceneNode* scene_node, float target_yaw) {
  if (!scene_node) return false;

  __try {
    scene_node->MAngWrappedLocalRotation().y = target_yaw;
    scene_node->TransformDirty() = 1;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool TryReadObjectVtable(void* object, void*& vtable) {
  vtable = nullptr;

  if (!object) return false;

  __try {
    vtable = *reinterpret_cast<void**>(object);
    return vtable != nullptr;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    vtable = nullptr;
    return false;
  }
}

void ResetCurrentPreviewPlayerCapture() {
  g_current_preview_player.store(nullptr, std::memory_order_release);
  g_current_preview_player_vtable.store(nullptr, std::memory_order_release);
  g_current_preview_player_captured_at.store(0, std::memory_order_release);
  g_current_preview_player_generation.fetch_add(1, std::memory_order_acq_rel);
}

void* __fastcall HkPreviewPlayerConstructor(void* preview_player, void* arg2,
                                            void* arg3, void* arg4) {
  const auto original =
      g_preview_player_constructor_hook.Original<PreviewPlayerConstructorFn>();
  void* result =
      original ? original(preview_player, arg2, arg3, arg4) : preview_player;

  if (preview_player) {
    void* vtable = nullptr;
    TryReadObjectVtable(preview_player, vtable);
    g_current_preview_player_vtable.store(vtable, std::memory_order_release);
    g_current_preview_player.store(preview_player, std::memory_order_release);
    g_current_preview_player_captured_at.store(GetTickCount64(),
                                               std::memory_order_release);
    g_current_preview_player_generation.fetch_add(1, std::memory_order_acq_rel);
  }

  return result;
}

bool ApplyPlayerSceneNodeRotation(float requested_yaw) {
  const std::uint32_t generation =
      g_current_preview_player_generation.load(std::memory_order_acquire);
  void* entity = g_current_preview_player.load(std::memory_order_acquire);
  void* expected_vtable =
      g_current_preview_player_vtable.load(std::memory_order_acquire);

  if (generation != g_player_rotation_observed_generation) {
    g_player_rotation_observed_generation = generation;
    g_player_rotation_scene_node = nullptr;
    g_player_preview_base_yaw = 0.0f;
    g_player_preview_base_yaw_valid = false;
    g_player_preview_last_target_yaw = 0.0f;
    g_player_preview_last_target_yaw_valid = false;
    g_player_rotation_stable_since = 0;
    g_player_rotation_stable_yaw = 0.0f;
    g_player_rotation_stable_yaw_valid = false;
  }

  if (!entity) return false;

  void* actual_vtable = nullptr;
  if (!TryReadObjectVtable(entity, actual_vtable) || !expected_vtable ||
      actual_vtable != expected_vtable)
    return false;

  PlayerSceneNodeRotationData data{};
  if (!TryReadPlayerSceneNodeRotationData(entity, data)) return false;

  // Values outside the expected scene-node state indicate a retired or reused
  // constructor-captured preview object. Never write through that chain.
  if (data.dirty > 1 || !std::isfinite(data.local_yaw) ||
      !std::isfinite(data.absolute_yaw) ||
      std::fabs(data.local_yaw) > 3600.0f ||
      std::fabs(data.absolute_yaw) > 3600.0f)
    return false;

  const ULONGLONG now = GetTickCount64();
  const ULONGLONG captured_at =
      g_current_preview_player_captured_at.load(std::memory_order_acquire);
  const ULONGLONG capture_age =
      captured_at && now >= captured_at ? now - captured_at : 0;

  if (g_player_rotation_scene_node != data.scene_node) {
    g_player_rotation_scene_node = data.scene_node;
    g_player_preview_base_yaw = 0.0f;
    g_player_preview_base_yaw_valid = false;
    g_player_preview_last_target_yaw = 0.0f;
    g_player_preview_last_target_yaw_valid = false;
    g_player_rotation_stable_since = now;
    g_player_rotation_stable_yaw = data.local_yaw;
    g_player_rotation_stable_yaw_valid = true;
  }

  if (!g_player_preview_base_yaw_valid) {
    if (!g_player_rotation_stable_yaw_valid ||
        std::fabs(NormalizePreviewYaw(data.local_yaw -
                                      g_player_rotation_stable_yaw)) >=
            kPlayerRotationStableEpsilon) {
      g_player_rotation_stable_yaw = data.local_yaw;
      g_player_rotation_stable_since = now;
      g_player_rotation_stable_yaw_valid = true;
    }

    const ULONGLONG stable_for = now >= g_player_rotation_stable_since
                                     ? now - g_player_rotation_stable_since
                                     : 0;
    const bool constructor_zero = std::fabs(data.local_yaw) < 0.01f &&
                                  std::fabs(data.absolute_yaw) < 0.01f;
    const bool old_enough = capture_age >= kPlayerRotationMinSettleMs;
    const bool stable_enough = stable_for >= kPlayerRotationStableMs;
    const bool zero_accepted =
        !constructor_zero || capture_age >= kPlayerRotationZeroFallbackMs;

    if (!old_enough || !stable_enough || !zero_accepted) return false;

    g_player_preview_base_yaw = data.local_yaw;
    g_player_preview_base_yaw_valid = true;
    g_player_preview_last_target_yaw = 0.0f;
    g_player_preview_last_target_yaw_valid = false;
  }

  const float target_yaw =
      NormalizePreviewYaw(g_player_preview_base_yaw + requested_yaw);
  if (!TryWritePlayerSceneNodeYaw(data.scene_node, target_yaw)) return false;

  g_player_preview_last_target_yaw = target_yaw;
  g_player_preview_last_target_yaw_valid = true;
  return true;
}

bool CopyPanoramaRenderTargetName(void* render_target, char (&output)[256]) {
  output[0] = '\0';

  if (!render_target) return false;

  __try {
    const char* name =
        static_cast<const panorama::RenderTarget*>(render_target)->name.Get();

    if (!name || !*name) return false;

    strncpy_s(output, sizeof(output), name, _TRUNCATE);
    return output[0] != '\0';
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    output[0] = '\0';
    return false;
  }
}

bool DescribeSrv(ID3D11ShaderResourceView* view, std::uint32_t& width,
                 std::uint32_t& height) {
  width = 0;
  height = 0;

  if (!view) return false;

  ID3D11Resource* resource = nullptr;
  view->GetResource(&resource);

  if (!resource) return false;

  ID3D11Texture2D* texture = nullptr;
  const HRESULT result = resource->QueryInterface(
      __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture));

  resource->Release();

  if (FAILED(result) || !texture) return false;

  D3D11_TEXTURE2D_DESC desc{};
  texture->GetDesc(&desc);
  texture->Release();

  width = desc.Width;
  height = desc.Height;
  return width != 0 && height != 0;
}

ID3D11ShaderResourceView* CreateSnapshotSrv(ID3D11ShaderResourceView* source) {
  if (!source || !g_device || !g_context) return nullptr;

  ID3D11Resource* resource = nullptr;
  source->GetResource(&resource);

  if (!resource) return nullptr;

  ID3D11Texture2D* source_texture = nullptr;

  const HRESULT query_result = resource->QueryInterface(
      __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&source_texture));

  resource->Release();

  if (FAILED(query_result) || !source_texture) return nullptr;

  D3D11_TEXTURE2D_DESC texture_desc{};
  source_texture->GetDesc(&texture_desc);

  D3D11_SHADER_RESOURCE_VIEW_DESC view_desc{};
  source->GetDesc(&view_desc);

  // Same resource shape/format as the composition RT, but owned only as a
  // shader-resource snapshot. It is never bound as Panorama's render target.
  texture_desc.Usage = D3D11_USAGE_DEFAULT;
  texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  texture_desc.CPUAccessFlags = 0;
  texture_desc.MiscFlags = 0;

  ID3D11Texture2D* snapshot_texture = nullptr;

  const HRESULT create_texture_result =
      g_device->CreateTexture2D(&texture_desc, nullptr, &snapshot_texture);

  if (FAILED(create_texture_result) || !snapshot_texture) {
    source_texture->Release();
    return nullptr;
  }

  g_context->CopyResource(snapshot_texture, source_texture);
  source_texture->Release();

  ID3D11ShaderResourceView* snapshot_view = nullptr;

  const HRESULT create_view_result = g_device->CreateShaderResourceView(
      snapshot_texture, &view_desc, &snapshot_view);

  snapshot_texture->Release();

  if (FAILED(create_view_result)) return nullptr;

  return snapshot_view;
}

void FreezeCurrentFrame() {
  ID3D11ShaderResourceView* current =
      g_current_texture.load(std::memory_order_acquire);

  if (!current) {
    g_freeze_complete.store(true, std::memory_order_release);
    return;
  }

  ID3D11ShaderResourceView* snapshot = CreateSnapshotSrv(current);

  if (!snapshot) {
    // Failing to snapshot is not fatal. Keep the existing AddRef'd SRV and
    // allow the main-thread state machine to continue.
    g_freeze_complete.store(true, std::memory_order_release);
    return;
  }

  ID3D11ShaderResourceView* previous =
      g_current_texture.exchange(snapshot, std::memory_order_acq_rel);

  if (previous) previous->Release();

  g_freeze_complete.store(true, std::memory_order_release);
}

ID3D11ShaderResourceView* ReadSrvFromBinding(void* binding, bool srgb) {
  if (!binding) return nullptr;

  __try {
    const auto* resource = static_cast<const ResourceBindingT*>(binding);
    if (resource->strong_references <= 0) return nullptr;

    const auto* texture =
        static_cast<const panorama::TextureDx11*>(resource->p_data);
    if (!texture) return nullptr;

    return srgb ? texture->srgb_view : texture->linear_view;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return nullptr;
  }
}

void ClearStagedFrame() {
  ID3D11ShaderResourceView* previous =
      g_staged_texture.exchange(nullptr, std::memory_order_acq_rel);

  g_staged_texture_width.store(0, std::memory_order_release);
  g_staged_texture_height.store(0, std::memory_order_release);
  g_staged_texture_at.store(0, std::memory_order_release);
  g_staged_generation.store(0, std::memory_order_release);

  if (previous) previous->Release();
}

void StageSrv(ID3D11ShaderResourceView* view) {
  if (!view) return;

  std::uint32_t width = 0;
  std::uint32_t height = 0;

  if (!DescribeSrv(view, width, height)) return;

  const std::uint32_t generation =
      g_active_generation.load(std::memory_order_acquire);

  // AcquireLayerRT fires continuously while the panel is alive. Once this
  // generation is already visible in the menu, ignore further staging attempts.
  if (g_promoted_generation.load(std::memory_order_acquire) == generation &&
      g_current_texture.load(std::memory_order_acquire) != nullptr) {
    return;
  }

  ID3D11ShaderResourceView* current =
      g_staged_texture.load(std::memory_order_acquire);

  if (current == view &&
      g_staged_generation.load(std::memory_order_acquire) == generation) {
    return;
  }

  view->AddRef();

  ID3D11ShaderResourceView* previous =
      g_staged_texture.exchange(view, std::memory_order_acq_rel);

  g_staged_texture_width.store(width, std::memory_order_release);
  g_staged_texture_height.store(height, std::memory_order_release);

  if (g_staged_generation.exchange(generation, std::memory_order_acq_rel) !=
          generation ||
      g_staged_texture_at.load(std::memory_order_acquire) == 0) {
    g_staged_texture_at.store(GetTickCount64(), std::memory_order_release);
  }

  if (previous) previous->Release();
}

void StageSrvFromBinding() {
  void* binding = g_preview_binding.load(std::memory_order_acquire);

  if (!binding) return;

  ID3D11ShaderResourceView* view = ReadSrvFromBinding(binding, false);

  if (!view) view = ReadSrvFromBinding(binding, true);

  if (view) StageSrv(view);
}

void PromoteStagedSrvIfReady() {
  if (features::changer::application::IsUpdatePending()) return;

  const std::uint64_t build_revision =
      g_build_revision.load(std::memory_order_acquire);

  const std::uint64_t latest_revision =
      systems::preview_item::GetLatestRevision();

  // Never publish an intermediate candidate that became obsolete while it was
  // building. The main-thread state machine will discard it and consume the
  // newest queued protobuf instead.
  if (build_revision == 0 ||
      (latest_revision != 0 && build_revision != latest_revision)) {
    return;
  }

  ID3D11ShaderResourceView* staged =
      g_staged_texture.load(std::memory_order_acquire);

  if (!staged) return;

  const std::uint32_t generation =
      g_active_generation.load(std::memory_order_acquire);

  if (g_staged_generation.load(std::memory_order_acquire) != generation) return;

  const ULONGLONG staged_at =
      g_staged_texture_at.load(std::memory_order_acquire);

  const ULONGLONG now = GetTickCount64();

  const ULONGLONG warmup_ms =
      g_build_complex_cosmetics.load(std::memory_order_acquire)
          ? kComplexSwapWarmupMs
          : kSimpleSwapWarmupMs;

  if (!staged_at || now - staged_at < warmup_ms) return;

  // Recheck immediately before publishing. Request() runs on the render/UI
  // thread too, so this closes the practical race where a new committed state
  // arrives between the first revision check and the SRV exchange below.
  if (build_revision != systems::preview_item::GetLatestRevision()) return;

  staged = g_staged_texture.exchange(nullptr, std::memory_order_acq_rel);

  if (!staged) return;

  const std::uint32_t width =
      g_staged_texture_width.exchange(0, std::memory_order_acq_rel);

  const std::uint32_t height =
      g_staged_texture_height.exchange(0, std::memory_order_acq_rel);

  g_staged_texture_at.store(0, std::memory_order_release);
  g_staged_generation.store(0, std::memory_order_release);

  // Publish presentation and texture as one logical frame. Mark it invalid
  // while replacing the SRV so a concurrent GetFrame() can only see the old
  // complete publication or the new complete publication, never a
  // cross-presentation mix.
  g_published_presentation_valid.store(false, std::memory_order_release);

  ID3D11ShaderResourceView* previous =
      g_current_texture.exchange(staged, std::memory_order_acq_rel);

  g_current_texture_width.store(width, std::memory_order_release);
  g_current_texture_height.store(height, std::memory_order_release);
  g_published_presentation.store(
      g_requested_presentation.load(std::memory_order_acquire),
      std::memory_order_release);
  g_promoted_generation.store(generation, std::memory_order_release);
  g_published_presentation_valid.store(true, std::memory_order_release);
  g_status.store(systems::model_preview::Status::kReady,
                 std::memory_order_release);

  if (previous) previous->Release();

  g_build_promoted.store(true, std::memory_order_release);
}

void* ResolvePreviewEntityFromPanel(void* panel) {
  if (!panel || !addresses::globals::game_entity_system) return nullptr;

  __try {
    const std::int32_t raw_handle =
        static_cast<const panorama::Item3dPanel*>(panel)->render_entity_handle;

    if (raw_handle == -1 || raw_handle == -2) return nullptr;

    return reinterpret_cast<void*>(systems::g_entities.GetByIndex(
        raw_handle & features::changer::engine::kEntryMask));
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return nullptr;
  }
}

void ReleasePreviewCompositeForZeroStickerTransition() {
  if (!g_zero_sticker_release_pending.exchange(false,
                                               std::memory_order_acq_rel))
    return;

  void* entity = ResolvePreviewEntityFromPanel(
      g_item3d_panel.load(std::memory_order_acquire));

  features::changer::engine::CompositeMaterialOwner* owner =
      features::changer::engine::CompositeOwner(entity);
  if (!owner || !g_release_composite_materials) return;

  __try {
    // The sticker builder returns without replacing the old material when the
    // list becomes empty. Releasing the current preview owner's composites
    // first lets the derived weapon callback reapply the clean material
    // immediately.
    g_release_composite_materials(owner, true);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

void __fastcall HkPreviewStickerInModelPanel(void* inventory_api,
                                             const char* item_id, int slot,
                                             void* panel) {
  const PreviewStickerInModelPanelFn original =
      g_preview_sticker_in_model_panel_hook
          .Original<PreviewStickerInModelPanelFn>();

  if (!original) return;

  if (panel &&
      g_preview_panel_capture_armed.exchange(false, std::memory_order_acq_rel))
    g_item3d_panel.store(panel, std::memory_order_release);

  original(inventory_api, item_id, slot, panel);
}

void CapturePanoramaRenderTarget(void* render_target) {
  if (!render_target || !g_build_active.load(std::memory_order_acquire)) return;

  char name[256]{};

  if (!CopyPanoramaRenderTargetName(render_target, name)) return;

  // panorama.dll stores exactly the unsuffixed value supplied through
  // composition-layer-texture-name in this CBufferString.
  //
  // Use a unique texture name for each generation. Panorama caches the vtex
  // binding by name, so reusing one name can leave a new panel attached to the
  // previous panel's composition/material while DeleteAsync is still retiring
  // it.
  if (g_build_texture_name[0] == '\0' ||
      std::strcmp(name, g_build_texture_name) != 0) {
    return;
  }

  __try {
    const auto* target =
        static_cast<const panorama::RenderTarget*>(render_target);
    void* binding = ((target->flags & 0x2u) && target->post_process_binding)
                        ? target->post_process_binding
                        : target->color_binding;

    if (!binding) return;

    g_preview_binding.store(binding, std::memory_order_release);
    StageSrvFromBinding();
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return;
  }
}

void __fastcall HkCreatePanoramaRenderTargets(void* render_target) {
  const CreatePanoramaRenderTargetsFn original =
      g_create_panorama_render_targets_hook
          .Original<CreatePanoramaRenderTargetsFn>();

  if (!original) return;

  original(render_target);
  CapturePanoramaRenderTarget(render_target);
}

void* __fastcall HkAcquirePanoramaLayerRt(void* cache, void* layer_desc) {
  const AcquirePanoramaLayerRTFn original =
      g_acquire_panorama_layer_rt_hook.Original<AcquirePanoramaLayerRTFn>();

  if (!original) return nullptr;

  void* render_target = original(cache, layer_desc);
  CapturePanoramaRenderTarget(render_target);
  return render_target;
}

PanoramaPanel2D* SafeReadPanel(PanoramaPanel2D** panel_pointer) {
  if (!panel_pointer) return nullptr;

  __try {
    return *panel_pointer;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return nullptr;
  }
}

PanoramaUiPanel* SafeGetUiPanel(PanoramaPanel2D* panel) {
  if (!panel) return nullptr;

  __try {
    return panel->ui_panel;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return nullptr;
  }
}

PanoramaUiPanel* GetActiveContext() {
  const bool in_game =
      addresses::globals::source2engine_to_client &&
      memory::CallVfunc<bool>(addresses::globals::source2engine_to_client,
                              40) &&
      memory::CallVfunc<bool>(addresses::globals::source2engine_to_client, 39);

  PanoramaPanel2D* root = nullptr;

  if (in_game) root = SafeReadPanel(g_hud_panel_pointer);

  if (!root) root = SafeReadPanel(g_main_menu_panel_pointer);

  if (!root) root = SafeReadPanel(g_hud_panel_pointer);

  return SafeGetUiPanel(root);
}

bool RunScript(PanoramaUiPanel* context, const char* script) {
  if (!context || !script || !g_ui_engine_pointer || !g_run_script)
    return false;

  __try {
    PanoramaUiEngine* engine = *g_ui_engine_pointer;

    if (!engine) return false;

    static const char origin_file = '\0';
    g_run_script(engine, context, script, &origin_file, 1);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool CreatePanel(PanoramaUiPanel* context, const char* panel_id,
                 const char* texture_name, std::uint64_t item_id,
                 systems::model_preview::Presentation presentation,
                 const std::string& model_path, bool counter_terrorist,
                 int capture_sticker_slot) {
  if (presentation == systems::model_preview::Presentation::kPlayer)
    ResetCurrentPreviewPlayerCapture();

  char script[18432]{};

  const float rotation_x = g_rotation_x.load(std::memory_order_acquire);
  const float rotation_y = g_rotation_y.load(std::memory_order_acquire);
  // CS2's current MapPlayerPreviewPanel loadout scene uses the CT loadout
  // camera for both T and CT player models. The T-specific camera does not
  // reliably produce a composition layer on this build.
  (void)counter_terrorist;
  constexpr const char* kPlayerCamera = "cam_loadoutmenu_ct";

  const int written = std::snprintf(
      script, sizeof(script),
      R"JS((function(){
const root=$.GetContextPanel();

if(typeof InventoryAPI==='undefined'){
    return;
}

if(typeof GameInterfaceAPI==='undefined' || typeof GameInterfaceAPI.GetSettingString!=='function'){
    return;
}

const panelId='%s';
const containerId=panelId+'_container';
const textureName='%s';
const itemId=BigInt('%llu');
const rotationX=%f;
const rotationY=%f;
const playerPreview=%s;
const playerModel='%s';
const playerCamera='%s';
const captureStickerSlot=%d;

if(!InventoryAPI.IsValidItemID(itemId)){
    return;
}

function createContainer(){
    const container=$.CreatePanel('Panel',root,containerId,{
        hittest:false,
        style:'width: %upx; height: %upx; opacity: 0.01; brightness: 0.0; wash-color: #00000000; x: 0px; y: 0px; z-index: -99999;'
    });

    if(container){
        container.hittest=false;
        container.hittestchildren=false;
    }

    return container;
}

function configurePanel(panel){
    if(!panel){
        return;
    }

    panel.hittest=false;
    panel.hittestchildren=false;

    try{
        if(typeof panel.SetRenderInterval==='function'){
            panel.SetRenderInterval(1);
        }
    }catch(e){
    }

    panel.SetAttributeString('velocity_model_preview_source','native-model-preview');

    try{
        panel.SetHideStaticGeometry(true);
        panel.SetHideParticles(true);
        panel.SetTransparentBackground(true);
    }catch(e){
    }
}

function applyRotation(panel,entityName){
    if(!panel){
        return;
    }

    try{
        panel.SetRotation(rotationX,rotationY,0.0);
    }catch(e){
    }

    if(entityName){
        try{
            panel.FireEntityInput(entityName,'SetAngles',String(rotationY)+' '+String(rotationX)+' 0');
        }catch(e){
        }
    }
}

function createPlayerPreview(){
    if(!playerModel){
        return;
    }

    const container=createContainer();

    if(!container){
        return;
    }

    const panel=$.CreatePanel('MapPlayerPreviewPanel',container,panelId,{
        'require-composition-layer':'true',
        'composition-layer-texture-name':textureName,
        'transparent-background':'false',
        'disable-depth-of-field':'false',
        map:'ui/buy_menu',
        camera:playerCamera,
        playermodel:playerModel,
        playername:'vanity_character',
        initial_entity:'vanity_character',
        animgraphcharactermode:'buy-menu',
        player:'true',
        mouse_rotate:'true',
        sync_spawn_addons:'true',
        'pin-fov':'vertical',
        csm_split_plane0_distance_override:'250.0',
        style:'width: 100%%; height: 100%%; opacity: 1.0;'
    });

    if(!panel){
        try{container.DeleteAsync(0.0);}catch(e){}
        return;
    }

    configurePanel(panel);
    panel.Data().loadedMap='ui/buy_menu';

    // Match the native player-preview setup. This is important for T models as
    // some of their scene/addon state is not completed by playermodel alone.
    try{
        if(typeof panel.EquipPlayerWithItem==='function'){
            panel.EquipPlayerWithItem(itemId);
        }
    }catch(e){
    }

    try{
        panel.SetReadyForDisplay(true);
    }catch(e){
    }
}

function getMapName(){
    let mapName=GameInterfaceAPI.GetSettingString('ui_inspect_bkgnd_map');

    if(!mapName || mapName==='mainmenu'){
        mapName=GameInterfaceAPI.GetSettingString('ui_mainmenu_bkgnd_movie');
    }

    if(!mapName){
        mapName='de_dust2';
    }

    if(!mapName.endsWith('_vanity')){
        mapName=mapName+'_vanity';
    }

    return mapName;
}

function bindItem(panel,ready){
    if(!panel || !panel.IsValid()){
        return;
    }

    panel.Data().itemId=itemId;
    panel.Data().active_item_idx=0;

    try{
        panel.SetActiveItem(0);
        panel.SetItemItemId(itemId,'');
    }catch(e){
    }

    applyRotation(panel,'item');

    try{
        panel.SetReadyForDisplay(ready);
    }catch(e){
    }
}

function createWeaponPreview(){
    const mapName=getMapName();

    const container=createContainer();

    if(!container){
        return;
    }

    const panel=$.CreatePanel('MapItemPreviewPanel',container,panelId,{
        'require-composition-layer':'true',
        'composition-layer-texture-name':textureName,
        'transparent-background':'false',
        'disable-depth-of-field':'false',
        hide_while_waiting_for_composite_materials:'true',
        'pin-fov':'vertical',
        camera:'cam_default',
        player:'true',
        map:mapName,
        initial_entity:'item',
        mouse_rotate:'false',
        rotation_limit_x:'360',
        rotation_limit_y:'90',
        auto_rotate_x:'0',
        auto_rotate_y:'0',
        auto_rotate_period_x:'15',
        auto_rotate_period_y:'25',
        auto_recenter:false,
        panzoom_enabled:'true',
        tabindex:'auto',
        selectionpos:'auto',
        style:'width: 100%%; height: 100%%; opacity: 1.0;'
    });

    if(!panel){
        try{container.DeleteAsync(0.0);}catch(e){}
        return;
    }

    configurePanel(panel);
    panel.Data().loadedMap=mapName;

    try{
        panel.SetReadyForDisplay(false);
    }catch(e){
    }

    // Capture the native CUI_Item3dPanel before binding the item. Once bound, CS2 can
    // skip PreviewStickerInModelPanel when the cached sticker kit already matches, which
    // would leave us without the panel pointer needed for the final-sticker cleanup.
    if(captureStickerSlot>=0){
        try{
            if(typeof InventoryAPI.PreviewStickerInModelPanel==='function'){
                InventoryAPI.PreviewStickerInModelPanel(String(itemId),captureStickerSlot,panel);
            }
        }catch(e){
        }
    }

    // Bind exactly once. Repeated SetItemItemId calls restart/overlap Source 2's
    // asynchronous composite-material work and can transiently drop stickers or show
    // the base weapon. The panel's native waiting flag handles the unfinished period.
    bindItem(panel,true);

    for(let i=0;i<=10;i++){
        const suffix=i===0?'':String(i);

        if(i!==0){
            panel.FireEntityInput('light_item'+suffix,'Disable');
            panel.FireEntityInput('light_item_new'+suffix,'Disable');
        }else{
            panel.FireEntityInput('light_item_new','Disable');
        }
    }

    try{
        const category=InventoryAPI.GetLoadoutCategory(itemId);

        if(category==='secondary'){
            panel.SetCSMSplitPlane0DistanceOverride(30.0);
        }else if(category==='smg'){
            panel.SetCSMSplitPlane0DistanceOverride(40.0);
        }else if(category==='rifle'){
            panel.SetCSMSplitPlane0DistanceOverride(55.0);
        }

        if(mapName==='warehouse_vanity' || mapName==='de_train_vanity' || mapName==='ui/acknowledge_item'){
            panel.SetBarnlightShadowScaleOverride(1.0);
        }else{
            panel.SetBarnlightShadowScaleOverride(4.0);
        }
    }catch(e){
    }

    function getWeaponCamera(){
        let defName='';

        try{
            defName=InventoryAPI.GetItemDefinitionName(itemId);
        }catch(e){
        }

        const cameras={
            weapon_awp:'7',
            weapon_aug:'3',
            weapon_sg556:'4',
            weapon_ssg08:'6',
            weapon_ak47:'4',
            weapon_m4a1_silencer:'6',
            weapon_famas:'4',
            weapon_g3sg1:'5',
            weapon_galilar:'3',
            weapon_m4a1:'4',
            weapon_scar20:'5',
            weapon_mp5sd:'3',
            weapon_xm1014:'4',
            weapon_m249:'6',
            weapon_ump45:'3',
            weapon_bizon:'3',
            weapon_mag7:'3',
            weapon_nova:'5',
            weapon_sawedoff:'3',
            weapon_negev:'5',
            weapon_usp_silencer:'2',
            weapon_elite:'2',
            weapon_tec9:'2',
            weapon_revolver:'2',
            weapon_c4:'3',
            weapon_taser:'0'
        };

        if(cameras[defName]!==undefined){
            return cameras[defName];
        }

        let category='';

        try{
            category=InventoryAPI.GetLoadoutCategory(itemId);
        }catch(e){
        }

        if(category==='secondary'){
            return '0';
        }

        if(category==='smg'){
            return '2';
        }

        return '3';
    }

    const camera=getWeaponCamera();

    try{
        panel.TransitionToCamera('cam_'+camera,0);
    }catch(e){
    }
}

function createNow(){
    if(root.FindChildTraverse(panelId) || root.FindChildTraverse(containerId)){
        return;
    }

    if(playerPreview){
        createPlayerPreview();
    }else{
        createWeaponPreview();
    }
}

createNow();
})();)JS",
      panel_id, texture_name, static_cast<unsigned long long>(item_id),
      rotation_x, rotation_y,
      presentation == systems::model_preview::Presentation::kPlayer ? "true"
                                                                    : "false",
      model_path.c_str(), kPlayerCamera, capture_sticker_slot,
      kPreviewPanelWidth, kPreviewPanelHeight);

  if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(script))
    return false;

  return RunScript(context, script);
}

bool ApplyRotation(PanoramaUiPanel* context, const char* panel_id,
                   systems::model_preview::Presentation presentation,
                   float rotation_x, float rotation_y) {
  if (!context || !panel_id || !*panel_id) return false;

  const bool player_preview =
      presentation == systems::model_preview::Presentation::kPlayer;

  if (player_preview) {
    // CUI_Player3dPanel has no continuous SetRotation method. Drive the
    // constructor-captured preview player's
    // features::changer::engine::scene_node local yaw directly and mark the
    // transform dirty so Source 2 rebuilds its world transform.
    return ApplyPlayerSceneNodeRotation(rotation_x);
  }

  char script[1536]{};

  const int written = std::snprintf(script, sizeof(script),
                                    R"JS((function(){
const p=$.GetContextPanel().FindChildTraverse('%s');
if(!p||!p.IsValid())return;
const yaw=%f;
const pitch=%f;
try{p.SetRotation(yaw,pitch,0.0);}catch(e){}
try{p.SetRotation(yaw,pitch);}catch(e){}
try{p.FireEntityInput('item','SetAngles',String(pitch)+' '+String(yaw)+' 0');}catch(e){}
})();)JS",
                                    panel_id, rotation_x, rotation_y);

  if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(script))
    return false;

  return RunScript(context, script);
}

bool ParkPanel(PanoramaUiPanel* context, const char* panel_id) {
  if (!context || !panel_id || !*panel_id) return false;

  char script[1024]{};

  const int written = std::snprintf(script, sizeof(script),
                                    R"JS((function(){
const root=$.GetContextPanel();
const panelId='%s';
const p=root.FindChildTraverse(panelId);
const c=root.FindChildTraverse(panelId+'_container');
if(p && p.IsValid()){
    try{p.SetReadyForDisplay(false);}catch(e){}
}
if(c && c.IsValid()){
    try{c.style.visibility='collapse';}catch(e){}
}else if(p && p.IsValid()){
    try{p.style.visibility='collapse';}catch(e){}
}
})();)JS",
                                    panel_id);

  if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(script))
    return false;

  return RunScript(context, script);
}

void DeletePanel(PanoramaUiPanel* context, const char* panel_id) {
  if (!context || !panel_id || !*panel_id) return;

  char script[512]{};

  const int written = std::snprintf(script, sizeof(script),
                                    R"JS((function(){
const root=$.GetContextPanel();
const panelId='%s';
const c=root.FindChildTraverse(panelId+'_container');
if(c){
    c.DeleteAsync(0.0);
    return;
}
const p=root.FindChildTraverse(panelId);
if(p)p.DeleteAsync(0.0);
})();)JS",
                                    panel_id);

  if (written > 0 && static_cast<std::size_t>(written) < sizeof(script))
    RunScript(context, script);
}

void ResetActiveFrame() {
  // Invalidate the publication metadata first so GetFrame() can never associate
  // an old presentation's texture with a newly-requested presentation.
  g_published_presentation_valid.store(false, std::memory_order_release);
  g_preview_binding.store(nullptr, std::memory_order_release);
  ClearStagedFrame();

  ID3D11ShaderResourceView* previous =
      g_current_texture.exchange(nullptr, std::memory_order_acq_rel);

  g_current_texture_width.store(0, std::memory_order_release);
  g_current_texture_height.store(0, std::memory_order_release);
  g_promoted_generation.store(0, std::memory_order_release);

  if (previous) previous->Release();
}

template <typename T>
void RemoveHookIfInstalled(T& hook) {
  hook.Reset();
}

}  // namespace

bool systems::preview_panorama::Initialize(ID3D11Device* device,
                                           ID3D11DeviceContext* context) {
  if (!device || !context) return false;

  Shutdown();

  g_panel_session_id = (static_cast<std::uint64_t>(GetTickCount64()) << 16) ^
                       static_cast<std::uint64_t>(GetCurrentProcessId());

  g_device = device;
  g_context = context;
  g_device->AddRef();
  g_context->AddRef();

  const std::uint8_t* ui_engine_pattern =
      reinterpret_cast<std::uint8_t*>(PATTERN(patterns::ui_engine_pointer));
  const std::uint8_t* main_menu_pattern = reinterpret_cast<std::uint8_t*>(
      PATTERN(patterns::main_menu_panel_pointer));
  const std::uint8_t* hud_pattern =
      reinterpret_cast<std::uint8_t*>(PATTERN(patterns::hud_panel_pointer));
  const std::uint8_t* run_script_pattern =
      reinterpret_cast<std::uint8_t*>(PATTERN(patterns::run_panorama_script));

  if (ui_engine_pattern)
    g_ui_engine_pointer = reinterpret_cast<PanoramaUiEngine**>(
        features::changer::engine::RelativeAddress(
            const_cast<std::uint8_t*>(ui_engine_pattern), 7));

  if (main_menu_pattern)
    g_main_menu_panel_pointer = reinterpret_cast<PanoramaPanel2D**>(
        features::changer::engine::RelativeAddress(
            const_cast<std::uint8_t*>(main_menu_pattern), 7));

  if (hud_pattern)
    g_hud_panel_pointer = reinterpret_cast<PanoramaPanel2D**>(
        features::changer::engine::RelativeAddress(
            const_cast<std::uint8_t*>(hud_pattern), 3));

  if (run_script_pattern)
    g_run_script = reinterpret_cast<RunScriptFn>(
        const_cast<std::uint8_t*>(run_script_pattern));

  if (!g_ui_engine_pointer || !g_run_script ||
      (!g_main_menu_panel_pointer && !g_hud_panel_pointer)) {
    g_status.store(systems::model_preview::Status::kMissingPatterns,
                   std::memory_order_release);
    return false;
  }

  if (!systems::preview_item::Initialize()) {
    g_status.store(systems::model_preview::Status::kMissingPatterns,
                   std::memory_order_release);
    return false;
  }

  const std::uint8_t* constructor_pattern = reinterpret_cast<std::uint8_t*>(
      PATTERN(patterns::preview_player_constructor));
  void* constructor_address =
      constructor_pattern ? const_cast<std::uint8_t*>(constructor_pattern)
                          : nullptr;

  if (constructor_address) {
    MEMORY_BASIC_INFORMATION memory_info{};
    const bool executable_target =
        VirtualQuery(constructor_address, &memory_info, sizeof(memory_info)) ==
            sizeof(memory_info) &&
        memory_info.State == MEM_COMMIT &&
        (memory_info.Protect &
         (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
          PAGE_EXECUTE_WRITECOPY)) != 0;

    if (executable_target)
      InstallHook(g_preview_player_constructor_hook, constructor_address,
                  reinterpret_cast<void*>(&HkPreviewPlayerConstructor));
  }

  const std::uint8_t* preview_sticker_in_model_panel_pattern =
      reinterpret_cast<std::uint8_t*>(
          PATTERN(patterns::preview_sticker_in_model_panel));

  g_release_composite_materials = reinterpret_cast<ReleaseCompositeMaterialsFn>(
      reinterpret_cast<std::uint8_t*>(
          PATTERN(patterns::release_composite_materials)));

  if (preview_sticker_in_model_panel_pattern) {
    InstallHook(
        g_preview_sticker_in_model_panel_hook,
        const_cast<std::uint8_t*>(preview_sticker_in_model_panel_pattern),
        reinterpret_cast<void*>(&HkPreviewStickerInModelPanel));
  }

  const std::uint8_t* create_render_targets_pattern =
      reinterpret_cast<std::uint8_t*>(
          PATTERN(patterns::panorama_create_render_targets));

  const std::uint8_t* acquire_layer_rt_pattern =
      reinterpret_cast<std::uint8_t*>(
          PATTERN(patterns::panorama_acquire_layer_rt));

  if (!create_render_targets_pattern) {
    g_status.store(systems::model_preview::Status::kMissingPatterns,
                   std::memory_order_release);
    return false;
  }

  if (!InstallHook(g_create_panorama_render_targets_hook,
                   const_cast<std::uint8_t*>(create_render_targets_pattern),
                   reinterpret_cast<void*>(&HkCreatePanoramaRenderTargets))) {
    g_status.store(systems::model_preview::Status::kMissingPatterns,
                   std::memory_order_release);
    return false;
  }

  // Optional cache-hit hook; the creation hook remains sufficient if
  // unavailable.
  if (acquire_layer_rt_pattern) {
    InstallHook(g_acquire_panorama_layer_rt_hook,
                const_cast<std::uint8_t*>(acquire_layer_rt_pattern),
                reinterpret_cast<void*>(&HkAcquirePanoramaLayerRt));
  }

  g_initialized.store(true, std::memory_order_release);
  g_status.store(systems::model_preview::Status::kWaitingForRequest,
                 std::memory_order_release);

  return true;
}

void systems::preview_panorama::Shutdown() {
  g_initialized.store(false, std::memory_order_release);

  // Do not call Panorama RunScript from the DLL unload thread. All V8
  // interaction is confined to ProcessMainThread(); the hidden preview panel
  // can be discarded by Panorama/root teardown naturally during unload.
  g_panel_active.store(false, std::memory_order_release);
  g_current_panel_parked.store(false, std::memory_order_release);
  g_status.store(systems::model_preview::Status::kUninitialized,
                 std::memory_order_release);

  ResetActiveFrame();

  RemoveHookIfInstalled(g_acquire_panorama_layer_rt_hook);
  RemoveHookIfInstalled(g_create_panorama_render_targets_hook);
  RemoveHookIfInstalled(g_preview_player_constructor_hook);
  RemoveHookIfInstalled(g_preview_sticker_in_model_panel_hook);

  systems::preview_item::Shutdown();

  if (g_context) {
    g_context->Release();
    g_context = nullptr;
  }

  if (g_device) {
    g_device->Release();
    g_device = nullptr;
  }

  g_ui_engine_pointer = nullptr;
  g_main_menu_panel_pointer = nullptr;
  g_hud_panel_pointer = nullptr;
  g_run_script = nullptr;

  g_current_context = nullptr;
  g_applied_definition_index = 0;
  g_applied_item_id = 0;
  g_applied_revision = 0;
  g_applied_complex_cosmetics = false;
  g_applied_presentation = systems::model_preview::Presentation::kWeapon;
  g_panel_session_id = 0;
  g_current_panel_id[0] = '\0';

  g_build_context = nullptr;
  g_build_panel_id[0] = '\0';
  g_build_texture_name[0] = '\0';
  g_build_definition_index = 0;
  g_build_item_id = 0;
  g_build_revision.store(0, std::memory_order_release);
  g_build_complex_cosmetics_value = false;
  g_build_presentation = systems::model_preview::Presentation::kWeapon;
  g_build_complex_cosmetics.store(false, std::memory_order_release);
  g_build_active.store(false, std::memory_order_release);
  g_build_promoted.store(false, std::memory_order_release);
  g_retire_release_at = 0;

  g_rotation_x.store(0.0f, std::memory_order_release);
  g_rotation_y.store(0.0f, std::memory_order_release);
  g_rotation_dirty.store(false, std::memory_order_release);
  g_current_preview_player.store(nullptr, std::memory_order_release);
  g_current_preview_player_vtable.store(nullptr, std::memory_order_release);
  g_current_preview_player_generation.store(0, std::memory_order_release);
  g_current_preview_player_captured_at.store(0, std::memory_order_release);
  g_player_rotation_observed_generation = 0;
  g_player_rotation_scene_node = nullptr;
  g_player_preview_base_yaw = 0.0f;
  g_player_preview_base_yaw_valid = false;
  g_player_preview_last_target_yaw = 0.0f;
  g_player_preview_last_target_yaw_valid = false;
  g_player_rotation_stable_since = 0;
  g_player_rotation_stable_yaw = 0.0f;
  g_player_rotation_stable_yaw_valid = false;

  g_published_presentation.store(systems::model_preview::Presentation::kWeapon,
                                 std::memory_order_release);
  g_published_presentation_valid.store(false, std::memory_order_release);

  g_requested_definition_index.store(0, std::memory_order_release);
  g_requested_presentation.store(systems::model_preview::Presentation::kWeapon,
                                 std::memory_order_release);
  g_last_request_at.store(0, std::memory_order_release);
  g_active_generation.store(0, std::memory_order_release);
  g_promoted_generation.store(0, std::memory_order_release);
  g_freeze_requested.store(false, std::memory_order_release);
  g_freeze_complete.store(false, std::memory_order_release);
  g_reset_frame_requested.store(false, std::memory_order_release);

  g_item3d_panel.store(nullptr, std::memory_order_release);
  g_panel_capture_sticker_slot.store(-1, std::memory_order_release);
  g_preview_panel_capture_armed.store(false, std::memory_order_release);
  g_zero_sticker_release_pending.store(false, std::memory_order_release);
  g_last_effective_sticker_count.store(0, std::memory_order_release);
  g_last_sticker_definition_index.store(0, std::memory_order_release);
  g_release_composite_materials = nullptr;
}

void systems::preview_panorama::ProcessMainThread() {
  if (!g_initialized.load(std::memory_order_acquire)) return;

  ReleasePreviewCompositeForZeroStickerTransition();

  const ULONGLONG now = GetTickCount64();
  const ULONGLONG last_request =
      g_last_request_at.load(std::memory_order_acquire);

  if (g_retire_release_at != 0 && now >= g_retire_release_at) {
    g_retire_release_at = 0;
    systems::preview_item::ReleaseForNextRequest();
  }

  if (!last_request || now - last_request > kRequestTimeoutMs) {
    PanoramaUiPanel* active_context = GetActiveContext();

    if (g_build_active.exchange(false, std::memory_order_acq_rel) &&
        g_build_context == active_context) {
      DeletePanel(g_build_context, g_build_panel_id);
    }

    if (g_panel_active.exchange(false, std::memory_order_acq_rel) &&
        g_current_context == active_context) {
      DeletePanel(g_current_context, g_current_panel_id);
    }

    g_build_promoted.store(false, std::memory_order_release);
    g_build_complex_cosmetics.store(false, std::memory_order_release);
    g_current_panel_parked.store(false, std::memory_order_release);

    g_current_context = nullptr;
    g_build_context = nullptr;

    g_applied_definition_index = 0;
    g_applied_item_id = 0;
    g_applied_revision = 0;
    g_applied_complex_cosmetics = false;
    g_applied_presentation = systems::model_preview::Presentation::kWeapon;

    g_build_definition_index = 0;
    g_build_item_id = 0;
    g_build_revision.store(0, std::memory_order_release);
    g_build_complex_cosmetics_value = false;
    g_build_presentation = systems::model_preview::Presentation::kWeapon;

    g_current_panel_id[0] = '\0';
    g_build_panel_id[0] = '\0';
    g_build_texture_name[0] = '\0';

    g_retire_release_at = 0;
    g_preview_binding.store(nullptr, std::memory_order_release);
    g_item3d_panel.store(nullptr, std::memory_order_release);
    g_panel_capture_sticker_slot.store(-1, std::memory_order_release);
    g_preview_panel_capture_armed.store(false, std::memory_order_release);
    g_zero_sticker_release_pending.store(false, std::memory_order_release);
    g_last_effective_sticker_count.store(0, std::memory_order_release);
    g_last_sticker_definition_index.store(0, std::memory_order_release);
    ClearStagedFrame();

    g_reset_frame_requested.store(true, std::memory_order_release);
    systems::preview_item::ReleaseForNextRequest();
    g_status.store(systems::model_preview::Status::kWaitingForRequest,
                   std::memory_order_release);
    return;
  }

  // Hold player yaw every main-thread update, not only while the mouse is
  // moving. The preview animation can rewrite local yaw between updates, so the
  // requested rotation has to be reapplied while the player preview is active.
  const bool player_preview_active =
      (g_panel_active.load(std::memory_order_acquire) &&
       g_applied_presentation ==
           systems::model_preview::Presentation::kPlayer) ||
      (g_build_active.load(std::memory_order_acquire) &&
       g_build_presentation == systems::model_preview::Presentation::kPlayer);

  if (player_preview_active)
    ApplyPlayerSceneNodeRotation(g_rotation_x.load(std::memory_order_acquire));

  // Freeze the current preview while the live weapon is refreshing. Both paths
  // touch custom-material state, so keep the last completed SRV visible until
  // the replacement preview has finished building.
  if (features::changer::application::IsUpdatePending()) {
    const std::uint64_t latest_revision =
        systems::preview_item::GetLatestRevision();
    const bool has_pending_preview_change =
        latest_revision != 0 && latest_revision != g_applied_revision;

    if (has_pending_preview_change &&
        g_current_texture.load(std::memory_order_acquire) &&
        g_panel_active.load(std::memory_order_acquire) &&
        !g_current_panel_parked.load(std::memory_order_acquire)) {
      // Request() normally performs this snapshot immediately from Present.
      // Keep this fallback in case the request arrived before a valid displayed
      // SRV.
      if (!g_freeze_complete.load(std::memory_order_acquire)) {
        g_freeze_requested.store(true, std::memory_order_release);
      } else {
        PanoramaUiPanel* active_context = GetActiveContext();

        if (g_current_context && active_context &&
            g_current_context == active_context &&
            g_current_panel_id[0] != '\0') {
          ParkPanel(g_current_context, g_current_panel_id);
          g_current_panel_parked.store(true, std::memory_order_release);
          g_freeze_complete.store(false, std::memory_order_release);
        }
      }
    }

    if (g_current_texture.load(std::memory_order_acquire))
      g_status.store(systems::model_preview::Status::kReady,
                     std::memory_order_release);
    else
      g_status.store(systems::model_preview::Status::kWaitingForItem,
                     std::memory_order_release);

    return;
  }

  PanoramaUiPanel* context = GetActiveContext();

  if (!context) {
    g_status.store(systems::model_preview::Status::kWaitingForPanel,
                   std::memory_order_release);
    return;
  }

  // A newer request can arrive while the hidden candidate is still compositing.
  // Drop that candidate, keep the current frame visible and let ProcessPending
  // build the latest request instead.
  if (g_build_active.load(std::memory_order_acquire)) {
    const std::uint64_t build_revision =
        g_build_revision.load(std::memory_order_acquire);

    const std::uint64_t latest_revision =
        systems::preview_item::GetLatestRevision();

    if (build_revision != 0 && latest_revision != 0 &&
        build_revision != latest_revision) {
      const bool stale_was_complex = g_build_complex_cosmetics_value;

      if (g_build_context == context && g_build_panel_id[0] != '\0')
        DeletePanel(g_build_context, g_build_panel_id);

      const ULONGLONG grace_ms =
          stale_was_complex ? kComplexRetireGraceMs : kSimpleRetireGraceMs;

      g_build_promoted.store(false, std::memory_order_release);
      g_build_active.store(false, std::memory_order_release);
      g_build_complex_cosmetics.store(false, std::memory_order_release);
      g_build_complex_cosmetics_value = false;
      g_build_presentation = systems::model_preview::Presentation::kWeapon;
      g_build_context = nullptr;
      g_build_panel_id[0] = '\0';
      g_build_texture_name[0] = '\0';
      g_build_definition_index = 0;
      g_build_item_id = 0;
      g_build_revision.store(0, std::memory_order_release);
      g_preview_binding.store(nullptr, std::memory_order_release);
      ClearStagedFrame();

      // DeleteAsync retires the scene asynchronously. Keep the temp-item slot
      // locked for a short grace period so rapid sticker edits cannot reuse it
      // while the discarded scene still references its material state.
      const ULONGLONG release_at = now + grace_ms;
      if (g_retire_release_at == 0 || release_at > g_retire_release_at)
        g_retire_release_at = release_at;

      if (g_current_texture.load(std::memory_order_acquire))
        g_status.store(systems::model_preview::Status::kReady,
                       std::memory_order_release);
      else
        g_status.store(systems::model_preview::Status::kWaitingForItem,
                       std::memory_order_release);

      return;
    }
  }

  if (g_build_promoted.exchange(false, std::memory_order_acq_rel)) {
    const bool old_was_complex = g_applied_complex_cosmetics;
    const bool had_old_panel = g_panel_active.load(std::memory_order_acquire);

    if (had_old_panel && g_current_context &&
        g_current_context == g_build_context && g_current_panel_id[0] != '\0') {
      DeletePanel(g_current_context, g_current_panel_id);
    }

    g_current_context = g_build_context;
    g_applied_definition_index = g_build_definition_index;
    g_applied_item_id = g_build_item_id;
    g_applied_revision = g_build_revision.load(std::memory_order_acquire);
    g_applied_complex_cosmetics = g_build_complex_cosmetics_value;
    g_applied_presentation = g_build_presentation;

    strncpy_s(g_current_panel_id, sizeof(g_current_panel_id), g_build_panel_id,
              _TRUNCATE);

    g_panel_active.store(true, std::memory_order_release);
    g_current_panel_parked.store(false, std::memory_order_release);
    g_build_active.store(false, std::memory_order_release);
    g_build_complex_cosmetics.store(false, std::memory_order_release);
    g_preview_binding.store(nullptr, std::memory_order_release);

    g_build_context = nullptr;
    g_build_panel_id[0] = '\0';
    g_build_texture_name[0] = '\0';
    g_build_definition_index = 0;
    g_build_item_id = 0;
    g_build_revision.store(0, std::memory_order_release);
    g_build_complex_cosmetics_value = false;
    g_build_presentation = systems::model_preview::Presentation::kWeapon;

    const bool complex_retirement =
        had_old_panel && (old_was_complex || g_applied_complex_cosmetics);

    const ULONGLONG grace_ms =
        complex_retirement ? kComplexRetireGraceMs : kSimpleRetireGraceMs;

    g_retire_release_at = now + grace_ms;

    g_status.store(systems::model_preview::Status::kReady,
                   std::memory_order_release);
    return;
  }

  if (g_build_active.load(std::memory_order_acquire)) {
    if (g_rotation_dirty.exchange(false, std::memory_order_acq_rel)) {
      const float rotation_x = g_rotation_x.load(std::memory_order_acquire);
      const float rotation_y = g_rotation_y.load(std::memory_order_acquire);

      if (g_panel_active.load(std::memory_order_acquire))
        ApplyRotation(g_current_context, g_current_panel_id,
                      g_applied_presentation, rotation_x, rotation_y);

      ApplyRotation(g_build_context, g_build_panel_id, g_build_presentation,
                    rotation_x, rotation_y);
    }

    if (g_current_texture.load(std::memory_order_acquire))
      g_status.store(systems::model_preview::Status::kReady,
                     std::memory_order_release);
    else
      g_status.store(systems::model_preview::Status::kWaitingForTexture,
                     std::memory_order_release);

    return;
  }

  const std::uint16_t requested_definition_index =
      g_requested_definition_index.load(std::memory_order_acquire);

  if (requested_definition_index == 0) {
    g_status.store(systems::model_preview::Status::kWaitingForRequest,
                   std::memory_order_release);
    return;
  }

  const systems::preview_item::Result native_item =
      systems::preview_item::GetResult();

  if (!native_item.valid || native_item.item_id == 0) {
    if (!g_panel_active.load(std::memory_order_acquire))
      g_status.store(systems::model_preview::Status::kWaitingForItem,
                     std::memory_order_release);

    return;
  }

  if (!native_item.current) {
    // A stale candidate may already be inside DeleteAsync retirement. Keep the
    // native result locked until that grace expires; otherwise the next
    // creation can reuse one of Valve's two temp slots while the old scene
    // still owns it.
    if (g_retire_release_at != 0) {
      if (g_current_texture.load(std::memory_order_acquire))
        g_status.store(systems::model_preview::Status::kReady,
                       std::memory_order_release);
      else
        g_status.store(systems::model_preview::Status::kWaitingForItem,
                       std::memory_order_release);

      return;
    }

    // If Panorama never consumed the obsolete item, the next temp-item
    // allocation can rotate back onto the ID used by the displayed panel.
    // Freeze/park it first so that reuse cannot change the visible frame.
    const bool has_displayed_frame =
        g_current_texture.load(std::memory_order_acquire) != nullptr;

    if (has_displayed_frame && g_panel_active.load(std::memory_order_acquire) &&
        !g_current_panel_parked.load(std::memory_order_acquire)) {
      if (!g_freeze_complete.load(std::memory_order_acquire)) {
        g_freeze_requested.store(true, std::memory_order_release);
        return;
      }

      if (g_freeze_complete.exchange(false, std::memory_order_acq_rel) &&
          g_current_context == context) {
        ParkPanel(g_current_context, g_current_panel_id);
        g_current_panel_parked.store(true, std::memory_order_release);
      }
    }

    if (systems::preview_item::IsResultLocked()) {
      systems::preview_item::ReleaseForNextRequest();
    }

    if (g_current_texture.load(std::memory_order_acquire))
      g_status.store(systems::model_preview::Status::kReady,
                     std::memory_order_release);
    else
      g_status.store(systems::model_preview::Status::kWaitingForItem,
                     std::memory_order_release);

    return;
  }

  const bool needs_candidate =
      !g_panel_active.load(std::memory_order_acquire) ||
      context != g_current_context ||
      native_item.item_id != g_applied_item_id ||
      native_item.revision != g_applied_revision ||
      native_item.definition_index != g_applied_definition_index ||
      native_item.presentation != g_applied_presentation;

  if (needs_candidate) {
    const bool has_displayed_frame =
        g_current_texture.load(std::memory_order_acquire) != nullptr;

    if (has_displayed_frame && g_panel_active.load(std::memory_order_acquire) &&
        !g_current_panel_parked.load(std::memory_order_acquire) &&
        !g_freeze_complete.load(std::memory_order_acquire)) {
      g_freeze_requested.store(true, std::memory_order_release);
      return;
    }

    if (g_freeze_complete.exchange(false, std::memory_order_acq_rel) &&
        g_panel_active.load(std::memory_order_acquire) &&
        g_current_context == context) {
      ParkPanel(g_current_context, g_current_panel_id);
      g_current_panel_parked.store(true, std::memory_order_release);
    }

    g_build_context = context;
    g_build_definition_index = native_item.definition_index;
    g_build_item_id = native_item.item_id;
    g_build_revision.store(native_item.revision, std::memory_order_release);
    g_build_complex_cosmetics_value = native_item.complex_cosmetics;
    g_build_presentation = native_item.presentation;

    g_build_complex_cosmetics.store(g_build_complex_cosmetics_value,
                                    std::memory_order_release);

    const std::uint32_t generation =
        g_active_generation.fetch_add(1, std::memory_order_acq_rel) + 1;

    std::snprintf(g_build_panel_id, sizeof(g_build_panel_id), "%s_%llX_%u",
                  kPanelIdPrefix,
                  static_cast<unsigned long long>(g_panel_session_id),
                  generation);

    std::snprintf(g_build_texture_name, sizeof(g_build_texture_name),
                  "%s_%llX_%u", kTextureNamePrefix,
                  static_cast<unsigned long long>(g_panel_session_id),
                  generation);

    g_preview_binding.store(nullptr, std::memory_order_release);
    ClearStagedFrame();
    g_build_promoted.store(false, std::memory_order_release);
    g_build_active.store(true, std::memory_order_release);

    const int capture_sticker_slot =
        g_panel_capture_sticker_slot.load(std::memory_order_acquire);

    g_preview_panel_capture_armed.store(capture_sticker_slot >= 0,
                                        std::memory_order_release);

    const bool panel_created = CreatePanel(
        context, g_build_panel_id, g_build_texture_name, native_item.item_id,
        native_item.presentation, native_item.model_path,
        native_item.counter_terrorist, capture_sticker_slot);

    if (!panel_created) {
      g_preview_panel_capture_armed.store(false, std::memory_order_release);
      g_build_active.store(false, std::memory_order_release);
      g_build_complex_cosmetics.store(false, std::memory_order_release);
      g_build_panel_id[0] = '\0';
      g_build_texture_name[0] = '\0';
      g_build_context = nullptr;
      g_build_revision.store(0, std::memory_order_release);
      g_build_complex_cosmetics_value = false;
      g_build_presentation = systems::model_preview::Presentation::kWeapon;

      if (g_current_texture.load(std::memory_order_acquire))
        g_status.store(systems::model_preview::Status::kReady,
                       std::memory_order_release);
      else
        g_status.store(systems::model_preview::Status::kWaitingForPanel,
                       std::memory_order_release);

      return;
    }

    if (g_current_texture.load(std::memory_order_acquire))
      g_status.store(systems::model_preview::Status::kReady,
                     std::memory_order_release);
    else
      g_status.store(systems::model_preview::Status::kWaitingForTexture,
                     std::memory_order_release);

    return;
  }

  if (g_rotation_dirty.exchange(false, std::memory_order_acq_rel)) {
    ApplyRotation(g_current_context, g_current_panel_id, g_applied_presentation,
                  g_rotation_x.load(std::memory_order_acquire),
                  g_rotation_y.load(std::memory_order_acquire));
  }

  if (g_current_texture.load(std::memory_order_acquire))
    g_status.store(systems::model_preview::Status::kReady,
                   std::memory_order_release);
  else
    g_status.store(systems::model_preview::Status::kWaitingForTexture,
                   std::memory_order_release);
}

void systems::preview_panorama::TickRenderThread() {
  if (!g_initialized.load(std::memory_order_acquire)) return;

  if (g_reset_frame_requested.exchange(false, std::memory_order_acq_rel))
    ResetActiveFrame();

  if (g_freeze_requested.load(std::memory_order_acquire)) {
    FreezeCurrentFrame();
    g_freeze_requested.store(false, std::memory_order_release);
  }

  // The hook callbacks usually stage immediately. This fallback handles
  // cache-hit cases where the ResourceBinding exists but no fresh
  // CreateRenderTargets callback occurs in the current Present interval.
  StageSrvFromBinding();
  PromoteStagedSrvIfReady();
}

void systems::preview_panorama::Request(
    const systems::model_preview::Request& request) {
  const systems::model_preview::Presentation previous_presentation =
      g_requested_presentation.exchange(request.presentation,
                                        std::memory_order_acq_rel);
  const std::uint16_t previous_definition_index =
      g_requested_definition_index.exchange(request.definition_index,
                                            std::memory_order_acq_rel);

  const bool presentation_changed =
      previous_presentation != request.presentation;

  g_last_request_at.store(GetTickCount64(), std::memory_order_release);

  int capture_sticker_slot = -1;
  int effective_sticker_count = 0;

  if (request.presentation == systems::model_preview::Presentation::kWeapon) {
    for (std::size_t slot = 0; slot < request.stickers.size(); ++slot) {
      if (!request.stickers[slot].enabled || request.stickers[slot].kit <= 0)
        continue;

      ++effective_sticker_count;

      if (capture_sticker_slot < 0)
        capture_sticker_slot = static_cast<int>(slot);
    }
  }

  const int previous_sticker_count = g_last_effective_sticker_count.exchange(
      effective_sticker_count, std::memory_order_acq_rel);
  const std::uint16_t previous_sticker_definition =
      g_last_sticker_definition_index.exchange(request.definition_index,
                                               std::memory_order_acq_rel);

  // CS2's sticker builder does not replace the existing composite when the new
  // sticker list is empty, so queue one native release for a same-weapon N ->
  // 0.
  const bool final_sticker_removed =
      request.presentation == systems::model_preview::Presentation::kWeapon &&
      previous_presentation == systems::model_preview::Presentation::kWeapon &&
      previous_definition_index == request.definition_index &&
      previous_sticker_definition == request.definition_index &&
      previous_sticker_count > 0 && effective_sticker_count == 0;

  if (final_sticker_removed) {
    g_zero_sticker_release_pending.store(true, std::memory_order_release);
  }

  g_panel_capture_sticker_slot.store(capture_sticker_slot,
                                     std::memory_order_release);

  // A texture from a different presentation must never be reused as a
  // placeholder. For example, switching from a weapon editor to a player/model
  // editor used to show the last gun for a frame or two while the new Panorama
  // composition layer started. Clear only on a presentation-class change;
  // same-class requests still retain the previous completed frame while their
  // replacement builds.
  if (presentation_changed) {
    g_freeze_requested.store(false, std::memory_order_release);
    g_freeze_complete.store(false, std::memory_order_release);
    ResetActiveFrame();
    g_status.store(systems::model_preview::Status::kWaitingForItem,
                   std::memory_order_release);
  }

  const std::uint64_t previous_revision =
      systems::preview_item::GetLatestRevision();
  systems::preview_item::Request(request);
  const std::uint64_t latest_revision =
      systems::preview_item::GetLatestRevision();

  // Request() runs from Present, so snapshot the displayed RT before the next
  // client frame can start rebuilding shared custom-material state.
  // Same-presentation requests keep that completed frame until the replacement
  // is promoted.
  if (!presentation_changed && latest_revision != 0 &&
      latest_revision != previous_revision &&
      g_current_texture.load(std::memory_order_acquire) &&
      g_panel_active.load(std::memory_order_acquire) &&
      !g_current_panel_parked.load(std::memory_order_acquire)) {
    g_freeze_requested.store(false, std::memory_order_release);
    g_freeze_complete.store(false, std::memory_order_release);
    FreezeCurrentFrame();
  }
}

void systems::preview_panorama::SetRotation(float rotation_x,
                                            float rotation_y) {
  rotation_x = std::fmod(rotation_x, 360.0f);

  if (rotation_x < -180.0f)
    rotation_x += 360.0f;
  else if (rotation_x > 180.0f)
    rotation_x -= 360.0f;

  rotation_y = std::clamp(rotation_y, -80.0f, 80.0f);

  g_rotation_x.store(rotation_x, std::memory_order_release);
  g_rotation_y.store(rotation_y, std::memory_order_release);
  g_rotation_dirty.store(true, std::memory_order_release);
}

void systems::preview_panorama::KeepAlive() {
  if (!g_initialized.load(std::memory_order_acquire)) return;

  g_last_request_at.store(GetTickCount64(), std::memory_order_release);
}

void systems::preview_panorama::Close() {
  if (!g_initialized.load(std::memory_order_acquire)) return;

  // Request immediate teardown on the next main-thread preview tick. Reuse the
  // existing timeout cleanup path instead of touching Panorama from the
  // WndProc.
  g_last_request_at.store(0, std::memory_order_release);
}

systems::model_preview::Frame systems::preview_panorama::GetFrame() {
  systems::model_preview::Frame frame{};

  if (!g_published_presentation_valid.load(std::memory_order_acquire))
    return frame;

  const systems::model_preview::Presentation requested_presentation =
      g_requested_presentation.load(std::memory_order_acquire);

  const systems::model_preview::Presentation published_presentation =
      g_published_presentation.load(std::memory_order_acquire);

  if (published_presentation != requested_presentation) return frame;

  frame.texture = g_current_texture.load(std::memory_order_acquire);
  frame.width = g_current_texture_width.load(std::memory_order_acquire);
  frame.height = g_current_texture_height.load(std::memory_order_acquire);

  // Recheck publication validity after reading the SRV/dimensions. Promotion
  // and reset explicitly invalidate before mutating these fields.
  frame.valid =
      g_published_presentation_valid.load(std::memory_order_acquire) &&
      frame.texture != nullptr && frame.width != 0 && frame.height != 0;

  return frame;
}

systems::model_preview::Status systems::preview_panorama::GetStatus() {
  const systems::model_preview::Status status =
      g_status.load(std::memory_order_acquire);

  if (status == systems::model_preview::Status::kReady) {
    if (!g_published_presentation_valid.load(std::memory_order_acquire) ||
        g_published_presentation.load(std::memory_order_acquire) !=
            g_requested_presentation.load(std::memory_order_acquire)) {
      return systems::model_preview::Status::kWaitingForTexture;
    }
  }

  return status;
}

const char* systems::preview_panorama::GetStatusText() {
  switch (GetStatus()) {
    case systems::model_preview::Status::kMissingPatterns:
      return "3D preview: Panorama signatures are out of date.";
    case systems::model_preview::Status::kWaitingForRequest:
      return "3D preview: waiting for preview request...";
    case systems::model_preview::Status::kWaitingForItem:
      return "3D preview: building native preview item...";
    case systems::model_preview::Status::kWaitingForPanel:
      return "3D preview: waiting for active Panorama root...";
    case systems::model_preview::Status::kWaitingForTexture:
      return "3D preview: waiting for Panorama composition texture...";
    case systems::model_preview::Status::kReady:
      return "3D preview: ready";
    default:
      return "3D preview: not initialized.";
  }
}
