#ifndef VELOCITY_CORE_FEATURES_CHANGER_RUNTIME_H_
#define VELOCITY_CORE_FEATURES_CHANGER_RUNTIME_H_

#include <cstdint>
#include <string>
#include <vector>

#include "core/features/changer/cosmetic_configuration.h"
#include "core/systems/preview/model_preview.h"

namespace features::changer::runtime {

struct StickerKit {
  int id{};
  std::string name;
};

bool Initialize();
void InitializePreview(ID3D11Device* device, ID3D11DeviceContext* context);
void Shutdown();
void BeforeFrameStage(int stage);
void AfterFrameStage(int stage);
void TickRender();

// Published by the render thread; consumed only on the client thread.
void Configure(features::changer::cosmetic_config::SkinChangerT config,
               std::uint16_t ct_agent, std::uint16_t t_agent);
void SetInteractiveEdit(bool active);
void ForceUpdate();
const std::vector<StickerKit>& StickerKits();

void PreviewItem(std::uint16_t definition,
                 const features::changer::cosmetic_config::ItemSkinT& skin,
                 bool agent, bool editing);
void ClosePreview();

}  // namespace features::changer::runtime

#endif  // VELOCITY_CORE_FEATURES_CHANGER_RUNTIME_H_
