module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>

export module velocity:core.systems.preview.model_preview;

import <array>;
import <cstddef>;
import <cstdint>;
import <filesystem>;
import <string>;
import <string_view>;

export import :core.features.changer.cosmetic_configuration;
export import :core.systems.systems;

export {

namespace features::changer::cosmetic_config {
struct ItemSkinT;
}

namespace systems::model_preview {
enum class Presentation : std::uint8_t { kWeapon = 0, kPlayer };

enum class Status : std::uint8_t {
  kUninitialized = 0,
  kMissingPatterns,
  kWaitingForRequest,
  kWaitingForItem,
  kWaitingForPanel,
  kWaitingForTexture,
  kReady
};

struct Sticker {
  bool enabled = false;
  int kit = 0;
  float wear = 0.0f;
  float scale = 1.0f;
  float rotation = 0.0f;
  float offset_x = 0.0f;
  float offset_y = 0.0f;
};

struct Keychain {
  bool enabled = false;
  int id = 0;
  int seed = 0;
  float offset_x = 0.0f;
  float offset_y = 0.0f;
  float offset_z = 0.0f;
};

struct Request {
  Presentation presentation = Presentation::kWeapon;
  std::uint16_t definition_index = 0;
  std::string model_path;
  bool counter_terrorist = false;

  int paint_kit = 0;
  int rarity = 0;
  int seed = 0;
  float wear = 0.0001f;

  bool stat_trak_enabled = false;
  int stat_trak_kills = 0;
  std::string custom_name;

  std::array<Sticker, 5> stickers{};
  Keychain keychain{};
};

struct Frame {
  ID3D11ShaderResourceView* texture = nullptr;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  bool valid = false;
};

bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
void Shutdown();

// Main/client thread only. Handles native temp-item creation and Panorama/V8.
void ProcessMainThread();

// Present/render thread only. Handles D3D11 SRV snapshot/promotion work.
void TickRenderThread();

// Generic preview request entry point.
void Submit(const Request& request);
void KeepAlive();
void Close();
void SetRotation(float rotation_x, float rotation_y);

[[nodiscard]] Frame GetFrame();
[[nodiscard]] Status GetStatus();
[[nodiscard]] const char* GetStatusText();

// Convenience request builders for the current skinchanger callers.
[[nodiscard]] Request MakeWeaponRequest(
    std::uint16_t definition_index,
    const features::changer::cosmetic_config::ItemSkinT& skin);
[[nodiscard]] Request MakeAgentRequest(std::uint16_t definition_index);
}  // namespace systems::model_preview
}  // export
