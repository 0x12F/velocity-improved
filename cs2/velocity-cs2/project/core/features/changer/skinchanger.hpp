#pragma once
#include "agent_changer.hpp"
#include <core/features/changer/cosmetic_configuration.hpp>

#include <cstdint>
#include <vector>

namespace features::changer::application
{
    void Run();
    void ForceUpdate();
    void Reset();

    // Hold live application until the current continuous UI edit is committed.
    void SetInteractiveEditActive(bool active);
    [[nodiscard]] bool IsInteractiveEditActive();
    [[nodiscard]] bool IsUpdatePending();
    void ProcessPendingKeychains();

    [[nodiscard]] std::vector<std::uint16_t> GetOwnedWeaponDefinitionIndices();
} // namespace features::changer::application
