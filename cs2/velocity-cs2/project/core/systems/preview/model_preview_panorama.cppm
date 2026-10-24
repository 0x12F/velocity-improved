module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>

export module velocity:core.systems.preview.model_preview_panorama;

import <cstddef>;
import <filesystem>;
import <string_view>;

export import :core.features.changer.cosmetic_configuration;
export import :core.systems.preview.model_preview;
export import :core.systems.systems;

export {
namespace systems::preview_panorama {
bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
void Shutdown();

void ProcessMainThread();
void TickRenderThread();

void Request(const systems::model_preview::Request& request);
void SetRotation(float rotation_x, float rotation_y);
void KeepAlive();
void Close();

[[nodiscard]] systems::model_preview::Frame GetFrame();
[[nodiscard]] systems::model_preview::Status GetStatus();
[[nodiscard]] const char* GetStatusText();
}  // namespace systems::preview_panorama
}  // export
