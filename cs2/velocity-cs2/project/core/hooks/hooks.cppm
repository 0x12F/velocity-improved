module;

#include "platform/platform.h"

export module velocity:core.hooks.hooks;

import <cstddef>;
import <cstdint>;
import <initializer_list>;
import <type_traits>;

export import :utilities.hooking.hooking;

export {
namespace hooks {

class Cheat {
 public:
  Cheat() = delete;

  static bool Initialize();
  static void Shutdown();

  static HRESULT __fastcall Present(IDXGISwapChain* thisptr, UINT sync_interval,
                                    UINT flags);
  static HRESULT __fastcall ResizeBuffers(IDXGISwapChain* thisptr,
                                          UINT buffer_count, UINT width,
                                          UINT height, DXGI_FORMAT new_format,
                                          UINT swap_chain_flags);
  static LRESULT __stdcall WndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                   LPARAM lparam);
  static void __stdcall OmSetRenderTargets(ID3D11DeviceContext* ctx,
                                           UINT num_views,
                                           ID3D11RenderTargetView* const* rtvs,
                                           ID3D11DepthStencilView* dsv);
  static void __fastcall CmdInterpreter(std::uintptr_t render_thread,
                                        std::uintptr_t item, std::uint8_t flag);
  static void __fastcall FrameStageNotify(std::uintptr_t thisptr, int stage);
  static void __fastcall CreateMove(std::uintptr_t thisptr, int slot,
                                    bool active);
  static void __fastcall HandleViewAngles(std::uintptr_t thisptr, int a2);
  static void __fastcall AddEntity(std::uintptr_t thisptr,
                                   std::uintptr_t entity, std::uint32_t handle);
  static void __fastcall RemoveEntity(std::uintptr_t thisptr,
                                      std::uintptr_t entity,
                                      std::uint32_t handle);
  static void __fastcall RenderView(std::uintptr_t thisptr);
  static void __fastcall DrawSkyboxArray(std::uintptr_t thisptr,
                                         std::uintptr_t a2,
                                         std::uintptr_t mesh_array,
                                         int mesh_count, int a5,
                                         std::uintptr_t a6, std::uintptr_t a7,
                                         std::uintptr_t a8);
  static std::uintptr_t __fastcall LightSceneObject(std::uintptr_t thisptr,
                                                    std::uintptr_t object,
                                                    std::uintptr_t a3);
  static void __fastcall DrawSceneObjectArray(std::uintptr_t thisptr,
                                              std::uintptr_t a2,
                                              std::uintptr_t object_array);
  static std::uintptr_t __fastcall DrawSceneObject(
      std::uintptr_t a1, std::uintptr_t a2, std::uintptr_t batch,
      int batch_count, int a5, std::uintptr_t a6, std::uintptr_t a7,
      std::uintptr_t a8);
  static bool __fastcall IsGlowing(std::uintptr_t glow_property);
  static void __fastcall GetGlowColor(std::uintptr_t glow_property,
                                      float* color);
  static void __fastcall GeneratePrimitives(std::uintptr_t thisptr,
                                            std::uintptr_t scene_object,
                                            std::uintptr_t scene_view,
                                            std::uintptr_t primitive_buffer);
  static std::uintptr_t __fastcall ParseReportHit(std::uintptr_t thisptr,
                                                  std::uint8_t deleting);
  static std::uintptr_t __fastcall SetupFog(__m128i* output, int* mode);
  static std::uintptr_t __fastcall SetShaderParam(__m128i* map,
                                                  std::uint32_t hash,
                                                  __m128i* value);
  static std::uintptr_t __fastcall SetPostprocessVec(__m128i* map,
                                                     std::uint32_t hash,
                                                     __m128i* value);
  static void __fastcall OverrideView(std::uintptr_t thisptr,
                                      std::uintptr_t view_setup);
  static void __fastcall UpdateFovSensitivity(std::uintptr_t thisptr);
  static void __fastcall RenderScope(std::uintptr_t a1, std::uintptr_t a2);
  static bool __fastcall RenderCrosshair(std::uintptr_t a1);
  static float __fastcall PrepareSceneMaterial(std::uintptr_t material,
                                               void* a2, float a3);
  static void __fastcall PostNetworkDataReceived(std::uintptr_t thisptr);
  static bool __fastcall DrawOverhead(std::uintptr_t pawn,
                                      std::uint32_t player_slot);
  static std::uintptr_t __fastcall DrawLegs(std::uintptr_t a1,
                                            std::uintptr_t a2,
                                            std::uintptr_t a3,
                                            std::uintptr_t a4,
                                            std::uintptr_t a5);
  static bool __fastcall GetTransformsForHitboxList(std::uintptr_t a1,
                                                    std::uintptr_t a2, int* a3);
  static void __fastcall SortPrimitives(std::uintptr_t thisptr,
                                        std::uintptr_t a2, std::uintptr_t a3,
                                        std::uint32_t a4);
  static float __fastcall GetInaccuracy(std::uintptr_t thisptr, float* a2,
                                        float* a3);
  static float* __fastcall GetInterpolatedShootPosition(std::uintptr_t thisptr,
                                                        float* out,
                                                        int* tick_frac);
  static std::uintptr_t __fastcall LevelInitialization(std::uintptr_t a1,
                                                       const char* new_map);
  static std::uintptr_t __fastcall LevelShutdown(std::uintptr_t a1);
  static void __fastcall ReadFrameInput(std::uintptr_t a1, std::uint32_t a2);
  static void __fastcall ProcessInputEvent(std::uintptr_t thisptr, int slot,
                                           float frametime);
  static std::uintptr_t __fastcall RenderDecals(std::uintptr_t render_context,
                                                std::uintptr_t** render_view,
                                                bool pass_flag_a,
                                                bool pass_flag_b);
  static void __fastcall RenderSmoke(std::uintptr_t a1, std::uintptr_t a2,
                                     int a3, int a4, std::uintptr_t a5,
                                     std::uintptr_t a6);
  static std::uintptr_t __fastcall RenderSmokeMap(std::uintptr_t thisptr,
                                                  std::size_t size,
                                                  std::uintptr_t* out_ptr);
  static void __fastcall RenderSmokeUnmap(std::uintptr_t thisptr,
                                          std::uintptr_t ctx, std::size_t size);
  static void __fastcall DrawFlashEffect(std::uintptr_t a1, int a2,
                                         std::uintptr_t* a3, std::uintptr_t a4,
                                         __m128* a5);
  static char __fastcall SetInfo(std::uintptr_t rcx, std::uintptr_t a2);
  static int __fastcall GetPingToDataCenter(std::uintptr_t thisptr,
                                            std::uint32_t pop,
                                            std::uint32_t* via_relay);
  static int __fastcall SendGcMessage(std::uintptr_t thisptr,
                                      std::uint32_t type, const void* data,
                                      std::uint32_t size);
  static bool ForceServerAvailable() { return m_send_gc_message_.IsEnabled(); }
  static const char* ForceServerStatus();

 private:
  inline static hooking::Jmp m_present_{};
  inline static hooking::Jmp m_resize_buffers_{};
  inline static hooking::Jmp m_wnd_proc_{};
  inline static hooking::Jmp m_om_set_render_targets_{};
  inline static hooking::Jmp m_cmd_interpreter_{};
  inline static hooking::Jmp m_frame_stage_notify_{};
  inline static hooking::Jmp m_create_move_{};
  inline static hooking::Jmp m_handle_view_angles_{};
  inline static hooking::Jmp m_add_entity_{};
  inline static hooking::Jmp m_remove_entity_{};
  inline static hooking::Jmp m_render_view_{};
  inline static hooking::Jmp m_draw_skybox_array_{};
  inline static hooking::Jmp m_light_scene_object_{};
  inline static hooking::Jmp m_draw_scene_object_array_{};
  inline static hooking::Jmp m_draw_scene_object_{};
  inline static hooking::Jmp m_is_glowing_{};
  inline static hooking::Jmp m_get_glow_color_{};
  inline static hooking::Jmp m_generate_primitives_{};
  inline static hooking::Jmp m_parse_report_hit_{};
  inline static hooking::Jmp m_setup_fog_{};
  inline static hooking::Jmp m_set_shader_param_{};
  inline static hooking::Jmp m_set_postprocess_vec_{};
  inline static hooking::Jmp m_override_view_{};
  inline static hooking::Jmp m_update_fov_sensitivity_{};
  inline static hooking::Jmp m_render_scope_{};
  inline static hooking::Jmp m_render_crosshair_{};
  inline static hooking::Jmp m_prepare_scene_material_{};
  inline static hooking::Jmp m_post_network_data_received_{};
  inline static hooking::Jmp m_draw_overhead_{};
  inline static hooking::Jmp m_draw_legs_{};
  inline static hooking::Jmp m_get_transforms_for_hitbox_list_{};
  inline static hooking::Jmp m_sort_primitives_{};
  inline static hooking::Jmp m_get_inaccuracy_{};
  inline static hooking::Jmp m_get_interpolated_shoot_position_{};
  inline static hooking::Jmp m_level_initialization_{};
  inline static hooking::Jmp m_level_shutdown_{};
  inline static hooking::Jmp m_read_frame_input_{};
  inline static hooking::Jmp m_process_input_event_{};
  inline static hooking::Jmp m_render_decals_{};
  inline static hooking::Jmp m_render_smoke_{};
  inline static hooking::Jmp m_render_smoke_map_{};
  inline static hooking::Jmp m_render_smoke_unmap_{};
  inline static hooking::Jmp m_draw_flash_effect_{};
  inline static hooking::Jmp m_set_info_{};
  inline static hooking::Jmp m_get_ping_to_data_center_{};
  inline static hooking::Jmp m_send_gc_message_{};
};

class Utility {
 public:
  Utility() = delete;

  static bool Initialize();
  static void Shutdown();

  static std::uintptr_t __fastcall ServiceRead(std::uintptr_t a1);
  static std::intptr_t __fastcall LogInternal(
      std::uintptr_t a1, std::uint32_t channel, std::int32_t severity,
      std::uintptr_t metadata, const char* message, std::intptr_t* args);

 private:
  inline static hooking::Jmp m_service_read_{};
  inline static hooking::Jmp m_log_internal_{};
};

}  // namespace hooks
}  // export
