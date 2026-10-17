#ifndef VELOCITY_CORE_SYSTEMS_PREVIEW_MODEL_PREVIEW_ITEM_H_
#define VELOCITY_CORE_SYSTEMS_PREVIEW_MODEL_PREVIEW_ITEM_H_

#include <cstdint>
#include <string>

#include "core/features/changer/cosmetic_configuration.h"
#include "core/systems/preview/model_preview.h"
#include "core/systems/systems.h"

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

#endif  // VELOCITY_CORE_SYSTEMS_PREVIEW_MODEL_PREVIEW_ITEM_H_
