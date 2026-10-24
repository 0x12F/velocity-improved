module;

#include "platform/platform.h"

export module velocity:core.features.changer.skinchanger;

import <cstdint>;
import <vector>;

export import :core.features.changer.agent_changer;
export import :core.features.changer.cosmetic_configuration;

export {
namespace features::changer::application {
void Run();
void ForceUpdate();
void Reset();

// Hold live application until the current continuous UI edit is committed.
void SetInteractiveEditActive(bool active);
[[nodiscard]] bool IsInteractiveEditActive();
[[nodiscard]] bool IsUpdatePending();
void ProcessPendingKeychains();

[[nodiscard]] std::vector<std::uint16_t> GetOwnedWeaponDefinitionIndices();
}  // namespace features::changer::application
}  // export
