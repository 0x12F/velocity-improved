#ifndef VELOCITY_CORE_SYSTEMS_PREVIEW_MODEL_PREVIEW_PANORAMA_H_
#define VELOCITY_CORE_SYSTEMS_PREVIEW_MODEL_PREVIEW_PANORAMA_H_

#include "core/features/changer/cosmetic_configuration.h"
#include "core/systems/preview/model_preview.h"
#include "core/systems/systems.h"

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

#endif  // VELOCITY_CORE_SYSTEMS_PREVIEW_MODEL_PREVIEW_PANORAMA_H_
