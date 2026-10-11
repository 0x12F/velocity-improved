#pragma once

#include <cstdint>

namespace features::changer::application
{
    void RunAgentChanger();
    void ForceAgentUpdate();
    bool IsAgentDefinitionRejected(std::uint16_t definitionIndex);
    void PrecacheAgentModels();
    void ResetAgentRuntime();
} // namespace features::changer::application
