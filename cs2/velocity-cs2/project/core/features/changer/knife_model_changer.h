#ifndef VELOCITY_CORE_FEATURES_CHANGER_KNIFE_MODEL_CHANGER_H_
#define VELOCITY_CORE_FEATURES_CHANGER_KNIFE_MODEL_CHANGER_H_
#include <cstdint>

#include "core/features/changer/cosmetic_configuration.h"
#include "core/features/changer/engine.h"

namespace features::changer::knife_application {
[[nodiscard]] bool IsKnifeDefinition(std::uint16_t definition_index);
bool Apply(features::changer::engine::Weapon* weapon,
           features::changer::engine::ItemView* item,
           std::uint16_t target_definition_index);
}  // namespace features::changer::knife_application

#endif  // VELOCITY_CORE_FEATURES_CHANGER_KNIFE_MODEL_CHANGER_H_
