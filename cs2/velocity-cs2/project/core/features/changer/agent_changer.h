#ifndef VELOCITY_CORE_FEATURES_CHANGER_AGENT_CHANGER_H_
#define VELOCITY_CORE_FEATURES_CHANGER_AGENT_CHANGER_H_

#include <cstdint>

namespace features::changer::application {
void RunAgentChanger();
void ForceAgentUpdate();
bool IsAgentDefinitionRejected(std::uint16_t definition_index);
void PrecacheAgentModels();
void ResetAgentRuntime();
}  // namespace features::changer::application

#endif  // VELOCITY_CORE_FEATURES_CHANGER_AGENT_CHANGER_H_
