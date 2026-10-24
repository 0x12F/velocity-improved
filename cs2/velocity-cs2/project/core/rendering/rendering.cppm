module;

#include "platform/platform.h"

#include <external/xdraw/xdraw.hpp>

export module velocity:core.rendering.rendering;

import <array>;
import <cstddef>;
import <cstdint>;
import <span>;
import <string>;
import <vector>;

export {
namespace rendering {

class Context {
 public:
  bool Initialize(IDXGISwapChain* swap_chain);
  void Shutdown();
  void OnPresent(IDXGISwapChain* swap_chain);
  void OnResizeBuffers();
  void OnResizeBuffersPost(IDXGISwapChain* swap_chain);

  [[nodiscard]] HWND GetWindow() const { return this->m_window_; }
  [[nodiscard]] ID3D11Device* GetDevice() const { return this->m_device_; }
  [[nodiscard]] ID3D11DeviceContext* GetContext() const {
    return this->m_context_;
  }
  [[nodiscard]] bool IsInitialized() const { return this->m_initialized_; }
  [[nodiscard]] bool UiAssetsReady() const { return this->m_ui_assets_ready_; }
  ID3D11RenderTargetView* GetRtv() const { return this->m_rtv_; }

 private:
  void CreateRtv(IDXGISwapChain* swap_chain);
  void SetupZdraw(HWND window);
  bool TryBindUiAssets();

  ID3D11Device* m_device_{nullptr};
  ID3D11DeviceContext* m_context_{nullptr};
  ID3D11RenderTargetView* m_rtv_{nullptr};
  HWND m_window_{nullptr};
  bool m_initialized_{false};
  bool m_ui_assets_ready_{false};
};

class Menu {
 public:
  void InitializeGraphics();

  void Draw();
  void Shutdown() const;

  void Toggle() { this->m_open_ = !this->m_open_; }
  [[nodiscard]] bool IsOpen() const { return this->m_open_; }
  void ApplySavedCursor();

  enum class Tab : int {
    kRagebot,
    kLegitbot,
    kPlayer,
    kWorld,
    kSkins,
    kMisc,
    kConfig,
    kCount
  };

 private:
  bool DrawIntro();
  void DrawSideBar(float h);
  void DrawTopBar(float w);
  void DrawThemeSwatches(float sb_x, float avatar_y);
  void TryLoadUserAvatar();
  void ApplyThemePreset(int preset);
  void SyncThemeStyle() const;
  void DrawSearchResults(float x, float y, float w, float h);
  void RebuildSearchIndex();
  void CloseSearch();
  void ActivateSearchResult(std::size_t index);

  void DrawRagebot(float group_w) const;
  void DrawLegitbot(float group_w) const;
  void DrawPlayer(float group_w) const;
  void DrawWorld(float group_w) const;
  void DrawSkins(float group_w) const;
  float DrawSkinEditor(float x, float y, float width, float height,
                       std::int16_t browsing_definition) const;
  void DrawMisc(float group_w) const;
  void DrawConfig(float group_w);

  bool m_open_{true};
  bool m_config_modal_open_{};
  bool m_config_cloud_refresh_pending_{};
  bool m_config_advanced_open_{};
  bool m_last_open_{true};
  float m_open_anim_{1.0f};
  std::uint8_t m_saved_relative_mouse_{};
  bool m_has_saved_cursor_{};
  int m_saved_cursor_x_{};
  int m_saved_cursor_y_{};

  float m_x_{100.0f};
  float m_y_{100.0f};
  float m_w_{900.0f};
  float m_h_{700.0f};
  float m_body_x_{};
  float m_body_y_{};
  float m_body_w_{};
  float m_body_h_{};

  int m_tab_{};
  int m_subtab_{};
  int m_subtab_pill_tab_{-1};
  float m_subtab_pill_x_{-1.0f};
  float m_intro_elapsed_{};
  float m_intro_assets_ready_at_{-1.0f};
  float m_intro_bar_phase_{};
  bool m_intro_base_graphics_ready_{};
  bool m_intro_finished_{};
  bool m_search_open_{};
  int m_theme_preset_{};
  float m_user_avatar_retry_delay_{};
  std::string m_search_query_{};
  std::vector<std::size_t> m_search_visible_indices_{};

  struct SearchEntry {
    std::string name{};
    std::string category{};
    std::string name_lower{};
    std::string category_lower{};
    int tab{};
    int subtab{};
    int bind_key{};
  };
  std::vector<SearchEntry> m_search_entries_{};

  struct Textures {
    struct Entry {
      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> resource{};
      int width{};
      int height{};
    };

    Entry logo{};
    Entry user{};
    Entry tabs[7]{};
    Entry search{};
    Entry settings{};
    Entry cfg_folder_on{};
    Entry cfg_folder_off{};
    Entry cfg_cloud_on{};
    Entry cfg_cloud_off{};
    Entry cfg_plus{};
    Entry intro_splash{};
  } m_textures_{};

  static constexpr auto kMaxSubtabs{6};

  struct SubtabInfo {
    const char* names[kMaxSubtabs]{};
    int count{};
  };

  static constexpr SubtabInfo kSubtabDefs[static_cast<int>(Tab::kCount)]{
      {{"pistol", "smg", "rifle", "shotgun", "sniper", "lmg"}, 6},
      {{"pistol", "smg", "rifle", "shotgun", "sniper", "lmg"}, 6},
      {{"enemies", "allies", "local"}, 3},
      {{"esp", "scene", "weather"}, 3},
      {{"guns", "knives", "gloves", "agents"}, 4},
      {{"main", "removals", "camera", "hud"}, 4},
      {{"general"}, 1}};
};

class Widgets {
 public:
  void Draw();

  static inline std::string s_map_name_{};

 private:
  void Watermark(xdraw::draw_list& draw_list);
  void Keybinds(xdraw::draw_list& draw_list);
};

class Fonts {
 public:
  enum class Size : std::uint8_t { kPetite, kNormal, kBig, kCount };

  struct FamilyT {
    std::array<xdraw::font*, static_cast<std::size_t>(Size::kCount)> sizes{};

    xdraw::font* operator[](Size size) const {
      return this->sizes[static_cast<std::size_t>(size)];
    }
    xdraw::font*& operator[](Size size) {
      return this->sizes[static_cast<std::size_t>(size)];
    }
  };

  void Initialize();

  FamilyT inter_medium{};
  FamilyT inter_bold{};
  FamilyT smallest_pixel7{};

 private:
  void LoadFamily(
      FamilyT& family, std::span<const std::byte> data,
      const std::array<float, static_cast<std::size_t>(Size::kCount)>& sizes);
};

Context g_context{};
Menu g_menu{};
Widgets g_widgets{};
Fonts g_fonts{};

}  // namespace rendering
}  // export
