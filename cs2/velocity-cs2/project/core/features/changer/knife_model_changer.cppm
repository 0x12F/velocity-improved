module;

#include "platform/platform.h"

#include <external/config.hpp>
#include <external/nlohmann/json.hpp>

export module velocity:core.features.changer.knife_model_changer;

import <cstddef>;
import <cstdint>;
import <filesystem>;
import <string_view>;
import <type_traits>;

export import :core.features.changer.cosmetic_configuration;
export import :core.features.changer.engine;

export {
namespace features::changer::knife_application {
[[nodiscard]] bool IsKnifeDefinition(std::uint16_t definition_index);
bool Apply(features::changer::engine::Weapon* weapon,
           features::changer::engine::ItemView* item,
           std::uint16_t target_definition_index);
}  // namespace features::changer::knife_application
}  // export
