module;

#include "platform/platform.h"

export module velocity:core.features.changer.agent_changer;

import <cstdint>;
import <string>;

export import :core.features.changer.engine;

export {
namespace features::changer::agent_application {
std::string ModelName(engine::PlayerItems* pawn);
bool SetSharedModel(engine::PlayerItems* pawn, const std::string& model);
}  // namespace features::changer::agent_application

namespace features::changer::application {
void RunAgentChanger();
void ForceAgentUpdate();
bool IsAgentDefinitionRejected(std::uint16_t definition_index);
void PrecacheAgentModels();
void ResetAgentRuntime();
}  // namespace features::changer::application
}  // export
