module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>

export module velocity:core.systems.preview.model_preview_item;

import <cstddef>;
import <cstdint>;
import <filesystem>;
import <string>;
import <string_view>;

export import :core.features.changer.cosmetic_configuration;
export import :core.systems.preview.model_preview;
export import :core.systems.systems;

export {
namespace systems::preview_item {
struct Result {
  std::uint64_t item_id = 0;
  std::uint16_t definition_index = 0;
  std::string model_path;
  bool counter_terrorist = false;
  int paint_kit = 0;
  std::uint64_t revision = 0;
  bool complex_cosmetics = false;
  systems::model_preview::Presentation presentation =
      systems::model_preview::Presentation::kWeapon;

  bool valid = false;
  bool current = false;
};

bool Initialize();
void Shutdown();

// Render/UI thread only. Serializes and copies request data, no game econ
// calls.
void Request(const systems::model_preview::Request& request);

// Main/client thread only.
void ProcessPending();
void ReleaseForNextRequest();

[[nodiscard]] bool IsResultLocked();
[[nodiscard]] std::uint64_t GetLatestRevision();
[[nodiscard]] Result GetResult();
}  // namespace systems::preview_item
}  // export
