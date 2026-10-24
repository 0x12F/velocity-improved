module;

#include "platform/platform.h"

export module velocity:core.features.changer.agent_changer;

import <cstdint>;

export {
namespace features::changer::application {
void RunAgentChanger();
void ForceAgentUpdate();
bool IsAgentDefinitionRejected(std::uint16_t definition_index);
void PrecacheAgentModels();
void ResetAgentRuntime();
}  // namespace features::changer::application
}  // export
