#ifndef VELOCITY_CORE_FEATURES_CHANGER_SKINCHANGER_H_
#define VELOCITY_CORE_FEATURES_CHANGER_SKINCHANGER_H_
#include <cstdint>
#include <vector>

#include "core/features/changer/agent_changer.h"
#include "core/features/changer/cosmetic_configuration.h"

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

#endif  // VELOCITY_CORE_FEATURES_CHANGER_SKINCHANGER_H_
