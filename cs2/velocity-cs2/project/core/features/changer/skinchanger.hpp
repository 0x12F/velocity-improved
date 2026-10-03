#pragma once
#include <core/features/changer/cosmetic_configuration.hpp>

#include <cstdint>
#include <vector>

namespace features::changer::application
{
    void Run();
    void RunAgentChanger();
    void ForceUpdate();
    void ForceAgentUpdate();
    bool IsAgentDefinitionRejected(std::uint16_t definitionIndex);
    void PrecacheAgentModels();
    void Reset();

    // Hold live application until the current continuous UI edit is committed.
    void SetInteractiveEditActive(bool active);
    [[nodiscard]] bool IsInteractiveEditActive();
    [[nodiscard]] bool IsUpdatePending();
    void ProcessPendingKeychains();

    [[nodiscard]] std::vector<std::uint16_t> GetOwnedWeaponDefinitionIndices();
}
