module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>
#include <external/xdraw/xdraw.hpp>

module velocity;

import <atomic>;
import <cstddef>;
import <filesystem>;
import <limits>;
import <mutex>;
import <optional>;
import <string_view>;
import <type_traits>;
import <utility>;

namespace features::changer::runtime {
std::vector<StickerKit> LoadStickerCatalog();
namespace {
constexpr int kFrameRenderEnd = 6;
constexpr int kFrameNetUpdateEnd = 7;

struct Configuration {
  features::changer::cosmetic_config::SkinChangerT skins;
  std::uint16_t ct_agent{};
  std::uint16_t t_agent{};
};

std::atomic_bool ready{};
std::mutex pending_mutex;
std::optional<Configuration> pending;
Configuration current;
std::vector<StickerKit> stickers;
bool was_connected{};
bool preview_initialized{};
std::uint16_t preview_definition{};

void UpdateLocalPawn() {
  const bool connected = engine::Connected();
  if (was_connected && !connected) features::changer::application::Reset();
  was_connected = connected;
}

void ConsumeConfiguration() {
  std::optional<Configuration> next;
  {
    std::scoped_lock lock(pending_mutex);
    next.swap(pending);
  }
  if (!next) return;

  current = std::move(*next);
  features::changer::cosmetic_config::skin_changer = current.skins;
  ForceUpdate();
}
}  // namespace

bool Initialize() {
  engine::allocate_attributes =
      reinterpret_cast<decltype(engine::allocate_attributes)>(
          memory::GetModuleExportWithBase(addresses::modules::tier0,
                                          "MemAlloc_AllocFunc"));
  engine::free_attributes = reinterpret_cast<decltype(engine::free_attributes)>(
      memory::GetModuleExportWithBase(addresses::modules::tier0,
                                      "MemAlloc_FreeFunc"));
  if (!engine::allocate_attributes || !engine::free_attributes ||
      g_econ_item_system.ItemDefs().empty())
    return false;
  stickers = LoadStickerCatalog();
  ready.store(true, std::memory_order_release);
  return true;
}

void InitializePreview(ID3D11Device* device, ID3D11DeviceContext* context) {
  if (!ready.load(std::memory_order_acquire) || preview_initialized) return;

  systems::model_preview::Initialize(device, context);
  preview_initialized = true;
}

void Shutdown() {
  ready.store(false, std::memory_order_release);
  systems::model_preview::Shutdown();
  preview_initialized = false;
  features::changer::application::Reset();
}

void Configure(features::changer::cosmetic_config::SkinChangerT config,
               std::uint16_t ct_agent, std::uint16_t t_agent) {
  std::scoped_lock lock(pending_mutex);
  pending = Configuration{std::move(config), ct_agent, t_agent};
}

void BeforeFrameStage(int stage) {
  if (!ready.load(std::memory_order_acquire)) return;
  if (stage == kFrameNetUpdateEnd) {
    ConsumeConfiguration();
    UpdateLocalPawn();
    features::changer::application::Run();
  }
  if (stage == kFrameRenderEnd) {
    UpdateLocalPawn();
    const auto pawn = engine::LocalPawn();
    const auto team = pawn ? pawn->GetTeam() : engine::Team::kUnassigned;
    features::changer::cosmetic_config::skin_changer.agent_definition =
        team == engine::Team::kCt ? current.ct_agent : current.t_agent;
    features::changer::application::RunAgentChanger();
  }
}

void AfterFrameStage(int stage) {
  if (!ready.load(std::memory_order_acquire)) return;
  if (stage == kFrameNetUpdateEnd) {
    features::changer::application::ProcessPendingKeychains();
    UpdateLocalPawn();
  }
  if (stage == kFrameRenderEnd) systems::model_preview::ProcessMainThread();
}

void TickRender() { systems::model_preview::TickRenderThread(); }
void SetInteractiveEdit(bool active) {
  features::changer::application::SetInteractiveEditActive(active);
}
void ForceUpdate() {
  features::changer::application::ForceUpdate();
  features::changer::application::ForceAgentUpdate();
  features::changer::glove_application::ForceUpdate();
}

const std::vector<StickerKit>& StickerKits() { return stickers; }

void PreviewItem(std::uint16_t definition,
                 const features::changer::cosmetic_config::ItemSkinT& skin,
                 bool agent, bool editing) {
  if (!ready.load(std::memory_order_acquire)) return;
  if (editing && definition == preview_definition) {
    systems::model_preview::KeepAlive();
    return;
  }
  systems::model_preview::Submit(
      agent ? systems::model_preview::MakeAgentRequest(definition)
            : systems::model_preview::MakeWeaponRequest(definition, skin));
  preview_definition = definition;
}

void ClosePreview() {
  systems::model_preview::Close();
  preview_definition = 0;
}
}  // namespace features::changer::runtime
