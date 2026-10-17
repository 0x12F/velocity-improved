#ifndef VELOCITY_CORE_FEATURES_CHANGER_GLOVE_CHANGER_H_
#define VELOCITY_CORE_FEATURES_CHANGER_GLOVE_CHANGER_H_
#include "core/features/changer/cosmetic_configuration.h"
#include "core/features/changer/engine.h"

namespace features::changer::glove_application {
void Run(features::changer::engine::PlayerItems* local_pawn);
void ForceUpdate();
void Reset();
}  // namespace features::changer::glove_application

#endif  // VELOCITY_CORE_FEATURES_CHANGER_GLOVE_CHANGER_H_
